#include <cstdio>
#include <cstring>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "application/command.hpp"
#include "application/dispatch.hpp"
#include "application/scheduler.hpp"
#include "diag/black_box.hpp"
#include "diag/ffi_guard.hpp"
#include "diag/rtc_watchdog.hpp"
#include "diag/stack_monitor.hpp"
#include "diag/tick_watchdog.hpp"
#include "infrastructure/config.hpp"
#include "infrastructure/drivers/adc.hpp"
#include "infrastructure/drivers/rgb_led.hpp"
#include "infrastructure/motor_task.hpp"
#include "infrastructure/network/ble.hpp"
#include "infrastructure/network/wifi.hpp"
#include "infrastructure/network/http_server.hpp"
#include "infrastructure/temp_thread.hpp"
#include "application/state_machine.hpp"
#include "infrastructure/network/ble_notify_thread.hpp"
#include "infrastructure/storage/nvs.hpp"
#include "interface/broadcast.hpp"
#include "interface/serial.hpp"
#include "domain/log_buffer.hpp"

static constexpr auto TAG = "main";

static constexpr auto BOOT_OK_MARKER = "BOOT_OK_MARKER";

// LL-031: PHY RF calibration holds gpio_spinlock for 10-200ms asynchronously
// after BT init. This constant controls how long we wait for it to complete
// before any GPIO operations (motor task, LED, etc.).
static constexpr TickType_t PHY_CALIBRATION_WAIT_TICKS = pdMS_TO_TICKS(1000);

// Fix 2: DRAM pre-check — must be called before every xTaskCreate
static void logDramBeforeTask(const char* name, size_t stackSize)
{
    auto largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "Creating %s, stack=%lu, largest_free=%lu", name, (unsigned long)stackSize,
             (unsigned long)largest);
    if (largest < stackSize + 4096)
    {
        ESP_LOGW(TAG, "DRAM pressure! Largest free block %lu < needed %lu", (unsigned long)largest,
                 (unsigned long)(stackSize + 4096));
    }
}

// Fix 1: GPIO spinlock deadlock prevention
// Wait for asynchronous PHY calibration to release gpio_spinlock before any
// gpio_config/gpio_set_direction/gpio_set_level call (LL-031).
// GR-1: blocking delay OK at boot (not in main loop).
static void ensureGpioReady()
{
    ESP_LOGI(TAG, "PHY wait: sleeping %lu ms for gpio_spinlock release...",
             (unsigned long)(PHY_CALIBRATION_WAIT_TICKS * portTICK_PERIOD_MS));
    puts("DBG: PHY wait start");
    fflush(stdout);
    vTaskDelay(PHY_CALIBRATION_WAIT_TICKS);
    puts("DBG: PHY wait done");
    fflush(stdout);
    ESP_LOGI(TAG, "PHY wait complete — GPIO spinlock should be released");
}

// Struct for passing parameters to net_owner task
struct NetTaskParams {
    ecotiter::infrastructure::network::BleManager* bleManager;
};

// Global pointer for WebSocket broadcast — set by net_owner task after init
// Must be atomic for cross-task visibility (GR-1: no blocking, no mutex)
namespace { std::atomic<ecotiter::infrastructure::network::HttpServer*> gHttpServerForWs{nullptr}; }

// ── ESP_LOG capture → LogBuffer ───────────────────────────────────
static int logVprintf(const char* fmt, va_list args) {
    char buf[384];
    int n = std::vsnprintf(buf, sizeof(buf), fmt, args);
    if (n > 0 && static_cast<size_t>(n) < sizeof(buf)) {
        const char* level = "INFO";
        if (buf[0] == 'W' && buf[1] == ' ') level = "WARN";
        else if (buf[0] == 'E' && buf[1] == ' ') level = "ERROR";
        else if (buf[0] == 'D' && buf[1] == ' ') level = "DEBUG";
        uint32_t ts = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
        ecotiter::domain::LogBuffer::instance().push(ts, level, buf);
    }
    // Forward to UART through stdio
    if (n > 0) {
        fwrite(buf, 1, static_cast<size_t>(n), stdout);
        fflush(stdout);
    }
    return n;
}

// WebSocket log push callback — registered after HTTP server init
static void wsLogCallback(const ecotiter::domain::LogEntry& entry) {
    // LL-038: stack guard — skip if < 1024 bytes remain on calling task's stack
    // (uxTaskGetStackHighWaterMark returns words; 1 word = 4 bytes on ESP32)
    if (uxTaskGetStackHighWaterMark(nullptr) < 256)
        return;
    auto* hs = gHttpServerForWs.load(std::memory_order_acquire);
    if (!hs) return;

    char buf[384];
    int n = std::snprintf(buf, sizeof(buf),
        R"({"event":"log","data":{"level":"%s","msg":")", entry.level);

    const char* src = entry.message;
    while (*src && static_cast<size_t>(n) < sizeof(buf) - 6) {
        if (*src == '"') { buf[n++] = '\''; }
        else if (*src == '\\') { buf[n++] = '/'; }
        else { buf[n++] = *src; }
        ++src;
    }
    buf[n] = '\0';

    n = std::snprintf(buf + n, sizeof(buf) - static_cast<size_t>(n),
        R"("}})");
    if (n < 0) return;
    n = static_cast<int>(std::strlen(buf));
    if (n > 0) {
        hs->broadcastWsEvent(buf, static_cast<size_t>(n));
    }
}

// GR-3: net_owner thread — WiFi init → HTTP server → BLE init → PHY wait → process loop
extern "C" void netTaskEntry(void* pvParameters) {
    auto* params = static_cast<NetTaskParams*>(pvParameters);
    puts("DBG: netTaskEntry START"); fflush(stdout);

    using namespace ecotiter::infrastructure::network;
    ecotiter::diag::StackMonitor::instance().registerThread(
        "net_owner", ecotiter::domain::NET_OWNER_STACK);

    WifiManager wifiManager;
    auto wifiResult = wifiManager.init();
    if (!wifiResult) {
        ESP_LOGE("net_owner", "WiFi init failed");
        vTaskDelete(nullptr);
        return;
    }
    wifiManager.startAP();

    // Attempt STA connection from NVS credentials
    bool staStarted = wifiManager.tryStartSTA();
    if (staStarted) {
        ESP_LOGI("net_owner", "STA connection attempt in progress");
    }

    // [INVESTIGATION] Exp 9 — FULL: routes + gHttpServerForWs + LogBuffer callback + worker
    HttpServer httpServer;
    httpServer.setWifiManager(&wifiManager);
    auto httpResult = httpServer.init();
    if (httpResult) {
        httpServer.registerRoutes();
        gHttpServerForWs.store(&httpServer, std::memory_order_release);
        ecotiter::domain::LogBuffer::instance().setCallback(wsLogCallback);

        ESP_LOGI("net_owner", "HTTP server ready on 192.168.4.1:80");
    } else {
        ESP_LOGW("net_owner", "HTTP server init failed");
    }

    // BLE init after HTTP server — ensures 12KB+ contiguous DRAM is available
    // (GR-3: WiFi → HTTP → BLE)
    if (params && params->bleManager) {
        auto bleResult = params->bleManager->init();
        if (bleResult) {
            ESP_LOGI("net_owner", "BLE initialized successfully");
            startBleNotifyThread(*params->bleManager);
        } else {
            ESP_LOGW("net_owner", "BLE init skipped (insufficient heap or HW error)");
        }
    }

    // LL-031: PHY calibration holds gpio_spinlock for 10-200ms after BT init.
    // Wait for it to release before any gpio_config/gpio_set_level operations.
    // GR-1: Blocking delay OK in worker thread (not main loop).
    ensureGpioReady();

    // LL-038: async log worker — deferred to after all boot init is complete
    // to avoid race with stale LogBuffer entries queued during early boot.
    // LL-038: async log worker — 8KB stack (LWIP send() via httpd_ws_send_frame_async
    // is MISLEADING — it's synchronous and uses ~2KB stack internally, LL-043).
    xTaskCreate(ecotiter::domain::LogBuffer::workerTaskEntry,
                "log_worker", 8192 / sizeof(configSTACK_DEPTH_TYPE),
                nullptr, 0, nullptr);

    while (true) {
        wifiManager.process();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

extern "C" void app_main(void)
{
    puts(BOOT_OK_MARKER);
    fflush(stdout);

    using namespace ecotiter;

    // Warmup: flush any stale serial data, ensure stdout works
    fflush(stdout);

    // Install ESP_LOG capture → LogBuffer (captures all subsequent ESP_LOGI/LOGW/LOGE calls)
    esp_log_set_vprintf(logVprintf);

    ESP_LOGI(TAG, "Build: %s (git: %s)", BUILD_DATE, GIT_HASH);

    // ====== Step 1: NVS ======
    puts("DBG: step 1 - nvs_flash_init");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::Nvs, std::memory_order_release);
    nvs_flash_init();
    infrastructure::storage::nvsInit();
    domain::gStallGuardThreshold.store(
        infrastructure::storage::stallguardReadThreshold(),
        std::memory_order_release);

    // ====== Step 2: BlackBox ======
    puts("DBG: step 2 - blackbox");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::BlackBox, std::memory_order_release);
    auto& blackbox = diag::BlackBox::instance();
    blackbox.init();

    // ====== Step 3: StackMonitor ======
    puts("DBG: step 3 - stack_monitor");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::StackMonitor, std::memory_order_release);
    auto& stackmon = diag::StackMonitor::instance();
    stackmon.registerMainTask();

    // ====== Step 4: Serial ======
    puts("DBG: step 4 - serial");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::Serial, std::memory_order_release);
    interface::SerialReader serial;
    auto initResult = serial.init();
    if (!initResult)
    {
        ESP_LOGE(TAG, "Serial init failed: %d", static_cast<int>(initResult.error()));
    }

    // ====== Step 5: RWDT (RTC Watchdog) — ultimate hang protection ======
    // RWDT runs on RTC slow clock, independent of CPU interrupts.
    // If ANY task holds a spinlock > 6s (or the system freezes entirely),
    // RWDT fires → RESET_SYSTEM → reboot with RTCWDT_SYS_RESET in boot log.
    // Configured BEFORE BLE init so it covers the PHY calibration window too.
    puts("DBG: step 5 - RWDT init");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::RtcWdt, std::memory_order_release);
    diag::RtcWatchdog rtcWdt;
    (void)rtcWdt;

    // ====== Step 6: BLE object construction (init deferred to net_owner) ======
    // GR-3: BLE init moved to net_owner thread after HTTP server to ensure
    // 12KB+ contiguous DRAM is available for httpd_start(). The BleManager
    // object is constructed here so the main loop can access it, but init()
    // is called from netTaskEntry after WiFi + HTTP.
    puts("DBG: step 6 - BLE object created");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::BleInit, std::memory_order_release);
    infrastructure::network::BleManager bleManager;

    // ====== Step 7: PHY wait deferred to net_owner ======
    // PHY calibration wait (ensureGpioReady) now runs in net_owner thread
    // after BLE init, since BLE init itself is now in net_owner.
    puts("DBG: step 7 - PHY wait deferred to net_owner");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::PhyWait, std::memory_order_release);

    // ====== Step 7b: RGB LED ======
    infrastructure::drivers::RgbLed rgbLed(config::PIN_LED_RGB);
    // Default to advertising (blue) — LED state machine will converge to correct
    // state (error red if BLE init fails, connected green if BLE connected, etc.)
    domain::TransportMode currentLedMode = domain::TransportMode::BleAdvertising;
    bool currentLedError = false;
    rgbLed.setTransportMode(currentLedMode, currentLedError);

    // ====== Step 7c: Temperature thread ======
    puts("DBG: step 7c - xTaskCreate temp");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::TempTask, std::memory_order_release);
    logDramBeforeTask("temp", domain::TEMP_THREAD_STACK);
    BaseType_t tt = xTaskCreate(tempTaskEntry, "temp",
                                domain::TEMP_THREAD_STACK / sizeof(configSTACK_DEPTH_TYPE),
                                nullptr, 1, nullptr);
    {
        char dbg[64];
        std::snprintf(dbg, sizeof(dbg), "DBG: xTaskCreate temp returned=%d\n", (int)tt);
        puts(dbg);
        fflush(stdout);
    }

    // ====== Step 8: Motor task ======
    puts("DBG: step 8 - xTaskCreate motor");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::MotorTask, std::memory_order_release);
    logDramBeforeTask("motor", domain::MOTOR_THREAD_STACK);
    BaseType_t mt = xTaskCreate(motorTaskEntry, "motor",
                                domain::MOTOR_THREAD_STACK / sizeof(configSTACK_DEPTH_TYPE),
                                nullptr, 1, nullptr);
    {
        char dbg[64];
        std::snprintf(dbg, sizeof(dbg), "DBG: xTaskCreate motor returned=%d\n", (int)mt);
        puts(dbg);
        fflush(stdout);
    }

    // ====== Step 8b: Net owner task ======
    puts("DBG: step 8b - xTaskCreate net_owner");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::NetOwner, std::memory_order_release);
    logDramBeforeTask("net_owner", domain::NET_OWNER_STACK);
    NetTaskParams netParams{&bleManager};
    BaseType_t nt = xTaskCreate(netTaskEntry, "net_owner",
                                domain::NET_OWNER_STACK / sizeof(configSTACK_DEPTH_TYPE),
                                &netParams, 1, nullptr);
    {
        char dbg[64];
        std::snprintf(dbg, sizeof(dbg), "DBG: xTaskCreate net_owner returned=%d\n", (int)nt);
        puts(dbg);
        fflush(stdout);
    }

    domain::gBootProgress.store(domain::BootProgress::Running, std::memory_order_release);
    puts("DBG: step 9 - RUNNING");
    fflush(stdout);

    diag::FfiGuard guard(50);
    infrastructure::drivers::AdcDriver adc(ADC_UNIT_1, ADC_CHANNEL_3);

    application::TickScheduler scheduler;
    application::ApplicationStateMachine appStateMachine;
    TickType_t lastWake = xTaskGetTickCount();
    constexpr TickType_t PACING_TICK = pdMS_TO_TICKS(10);

    auto sendResponse = [&](const ecotiter::application::CommandResponse& rsp) {
        ecotiter::domain::memory::ResponseBuffer buf{};
        auto written = ecotiter::application::serializeToBuffer(rsp, buf);
        if (written && *written > 0)
        {
            size_t len = *written;
            if (len < buf.size())
            {
                buf[len++] = '\n';
            }
            serial.write({buf.data(), len});
            // Also send over BLE if connected
            if (bleManager.isConnected())
            {
                std::ignore = bleManager.sendNotification({buf.data(), len});
            }
        }
    };

    while (true)
    {
        ecotiter::diag::TickWatchdog watchdog;
        (void)watchdog;

        // Feed RWDT every main loop iteration (10ms).
        // If ANY task holds a spinlock > 6s, RWDT fires → RESET_SYSTEM.
        rtcWdt.feed();

        scheduler.tick();

        if (scheduler.shouldCheckWatermarks()) {
            stackmon.logAllWatermarks();
        }
        appStateMachine.tick(application::gTick.load(std::memory_order_acquire));

        if (scheduler.shouldSample())
        {
            ecotiter::diag::FfiGuard guard(50);
            int16_t mv = adc.calibratedMv();
            if (mv < 0)
            {
                mv = 0;
            }
            ecotiter::domain::gLastMv.store(static_cast<uint16_t>(mv), std::memory_order_release);
        }

        if (scheduler.shouldBroadcast())
        {
            auto brtState = ecotiter::domain::gBuretteState.load(std::memory_order_acquire);
            bool motorMoving = brtState != ecotiter::domain::BuretteState::Idle;
            ecotiter::domain::gMotorIsMoving.store(motorMoving, std::memory_order_release);

            ecotiter::interface::BroadcastEvent evt{
                .tick = ecotiter::application::gTick.load(std::memory_order_acquire),
                .tempCX100 = ecotiter::domain::gTempCX100.load(std::memory_order_acquire),
                .mv = ecotiter::domain::gLastMv.load(std::memory_order_acquire),
                .electrodeMv = ecotiter::domain::gLastMv.load(std::memory_order_acquire),
                .vlv = ecotiter::domain::gValvePosition.load(std::memory_order_acquire),
                .brt = brtState,
                .volumeMl = ecotiter::domain::gVolumeMl.load(std::memory_order_acquire),
                .speedMlMin = 0.0f,
                .limitFull = ecotiter::domain::gStopFull.load(std::memory_order_acquire),
                .limitEmpty = ecotiter::domain::gStopEmpty.load(std::memory_order_acquire),
                .usbSerialConnected = ecotiter::domain::gUsbHandshakeReceived.load(std::memory_order_acquire),
                .bleConnected = bleManager.isConnected(),
                .stepperDrvConnected = true,
                .stepperDrvOtpw = false,
                .stepperDrvOt = false,
                .stallGuardValue = 0,
                .isStalled = false,
                .stallGuardThreshold = ecotiter::domain::gStallGuardThreshold.load(std::memory_order_acquire),
                .motorIsMoving = motorMoving,
                .stepsTaken = ecotiter::domain::gDispensedSteps.load(std::memory_order_acquire)};

            ecotiter::domain::memory::ResponseBuffer buf{};
            auto sv = ecotiter::interface::serializeBroadcast(evt, buf);
            if (!sv.empty())
            {
                serial.write(sv);
                serial.write({"\n", 1});
                if (bleManager.isConnected())
                {
                    std::string_view bleSv(sv.data(), sv.size());
                    std::ignore = bleManager.sendNotification(bleSv);
                    std::ignore = bleManager.sendNotification({"\n", 1});
                }
                {
                    auto* hs = gHttpServerForWs.load(std::memory_order_acquire);
                    if (hs) {
                        hs->broadcastWsEvent(sv.data(), sv.size());
                    }
                }
            }
        }

        // Process BLE zombie detection
        bleManager.process();

        // Update RGB LED based on BLE/USB state
        {
            domain::TransportMode newLedMode;
            bool newLedError;

            if (domain::gUsbHandshakeReceived.load(std::memory_order_acquire))
            {
                // USB active takes priority — LED off
                newLedMode = domain::TransportMode::UsbActive;
                newLedError = false;
            }
            else if (domain::gBleError.load(std::memory_order_acquire))
            {
                // BLE error — red
                newLedMode = domain::TransportMode::BleAdvertising;
                newLedError = true;
            }
            else if (bleManager.isConnected())
            {
                // BLE connected — green
                newLedMode = domain::TransportMode::BleConnected;
                newLedError = false;
            }
            else
            {
                // Advertising (not connected, no error) — blue
                newLedMode = domain::TransportMode::BleAdvertising;
                newLedError = false;
            }

            if (newLedMode != currentLedMode || newLedError != currentLedError)
            {
                char dbg[80];
                std::snprintf(dbg, sizeof(dbg), "DBG: LED %d->%d err=%d isConnected=%d gBleErr=%d",
                              static_cast<int>(currentLedMode),
                              static_cast<int>(newLedMode),
                              static_cast<int>(newLedError),
                              static_cast<int>(bleManager.isConnected()),
                              static_cast<int>(domain::gBleError.load(std::memory_order_acquire)));
                puts(dbg); fflush(stdout);
                currentLedMode = newLedMode;
                currentLedError = newLedError;
                rgbLed.setTransportMode(currentLedMode, currentLedError);
            }
        }

        // Update transport state in appStateMachine
        if (domain::gUsbHandshakeReceived.load(std::memory_order_acquire)) {
            appStateMachine.setTransportState(application::TransportState::UsbActive);
        } else if (bleManager.isConnected()) {
            appStateMachine.setTransportState(application::TransportState::BleConnected);
        } else {
            appStateMachine.setTransportState(application::TransportState::BleDisconnected);
        }

        // Drain BLE command queue (only if initialized — commandQueue is created during init())
        if (bleManager.isInitialized())
        {
            ecotiter::infrastructure::network::BleCmdItem bleItem;
            while (xQueueReceive(bleManager.commandQueue(), &bleItem, 0) == pdTRUE)
            {
                ESP_LOGI(TAG, "BLE RX: %s", bleItem.data);

                std::string_view line(bleItem.data);
                auto cmd = ecotiter::application::parseCommand(line);
                if (cmd)
                {
                    auto rsp = ecotiter::application::dispatch(*cmd);
                    if (rsp)
                    {
                        sendResponse(*rsp);
                    }
                    else
                    {
                        sendResponse(ecotiter::application::makeErrorResponse("dispatch failed"));
                    }
                }
                else
                {
                    sendResponse(ecotiter::application::makeErrorResponse("parse failed"));
                }
            }
        }

        // Process serial commands
        auto line = serial.process();
        if (line.has_value())
        {
            ESP_LOGI(TAG, "RX: %.*s", static_cast<int>(line->size()), line->data());

            ecotiter::domain::gUsbHandshakeReceived.store(true, std::memory_order_release);

            auto cmd = ecotiter::application::parseCommand(*line);
            if (cmd)
            {
                auto rsp = ecotiter::application::dispatch(*cmd);
                if (rsp)
                {
                    sendResponse(*rsp);
                }
                else
                {
                    sendResponse(ecotiter::application::makeErrorResponse("dispatch failed"));
                }
            }
            else
            {
                sendResponse(ecotiter::application::makeErrorResponse("parse failed"));
            }
        }

        vTaskDelayUntil(&lastWake, PACING_TICK);
    }
}
