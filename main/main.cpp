#include <cstdio>
#include <cstring>
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs_flash.h"

#include "application/command.hpp"
#include "application/dispatch.hpp"
#include "application/scheduler.hpp"
#include "diag/black_box.hpp"
#include "diag/ffi_guard.hpp"
#include "diag/heap_monitor.hpp"
#include "diag/rtc_watchdog.hpp"
#include "diag/stack_monitor.hpp"
#include "diag/tick_watchdog.hpp"
#include "infrastructure/config.hpp"
#include "infrastructure/memory/psram_resource.hpp"
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
#include "version.h"
#include "interface/serial.hpp"
#include "domain/log_buffer.hpp"

// Manual JSON id extractor — avoids nlohmann dependency in main.cpp
static uint64_t extractCmdId(const char* data) {
    const char* p = std::strstr(data, "\"id\":");
    if (!p) return 0;
    p += 5;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p < '0' || *p > '9') return 0;
    uint64_t val = 0;
    while (*p >= '0' && *p <= '9') {
        val = val * 10 + static_cast<uint64_t>(*p - '0');
        ++p;
    }
    return val;
}

static constexpr auto TAG = "main";

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

// Struct for passing parameters to net_owner task
struct NetTaskParams {
    ecotiter::infrastructure::network::BleManager* bleManager;
};

// Global pointer for WebSocket broadcast — set by net_owner task after init
// Must be atomic for cross-task visibility (GR-1: no blocking, no mutex)
namespace { std::atomic<ecotiter::infrastructure::network::HttpServer*> gHttpServerForWs{nullptr}; }

// ws_send_queue: log_worker pushes JSON, net_owner drains and broadcasts via WebSocket
struct WsSendEntry {
    char data[384];
    size_t len;
};
static QueueHandle_t gWsSendQueue = nullptr;

// ADC driver pointer for dispatch.cpp sample read callback
namespace { ecotiter::infrastructure::drivers::AdcDriver* gAdcDriver{nullptr}; }

static uint16_t adcSampleRead() {
    if (gAdcDriver) {
        auto raw = gAdcDriver->readRaw();
        if (raw) return *raw;
    }
    return 0;
}

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
    // LL-026: use fwrite/fflush instead of uart_write_bytes — the UART driver
    // (esp_driver_uart) is not installed at the time ESP_LOGI is first called
    // from app_main. uart_write_bytes requires uart_driver_install(), while
    // fwrite/fflush uses the VFS layer with uart_tx_chars (HAL, no driver needed).
    if (n > 0) {
        fwrite(buf, 1, static_cast<size_t>(n), stdout);
        fflush(stdout);
    }
    return n;
}

// WebSocket log push callback — formats JSON and pushes to ws_send_queue
// net_owner drains the queue and calls broadcastWsEvent (GR-14)
static void wsLogCallback(const ecotiter::domain::LogEntry& entry) {
    char buf[384];
    int n = std::snprintf(buf, sizeof(buf),
        R"({"event":"log","data":{"level":"%s","msg":")", entry.level);

    const char* src = entry.message;
    while (*src && static_cast<size_t>(n) < sizeof(buf) - 6) {
        if (*src == '"') { buf[n++] = '\''; }
        else if (*src == '\\') { buf[n++] = '/'; }
        else if (*src == '\n') { buf[n++] = '\\'; buf[n++] = 'n'; }
        else if (*src == '\r') { buf[n++] = '\\'; buf[n++] = 'r'; }
        else if (*src == '\t') { buf[n++] = '\\'; buf[n++] = 't'; }
        else { buf[n++] = *src; }
        ++src;
    }
    buf[n] = '\0';

    n = std::snprintf(buf + n, sizeof(buf) - static_cast<size_t>(n),
        R"("}})");
    if (n < 0) return;
    n = static_cast<int>(std::strlen(buf));
    if (n <= 0) return;

    WsSendEntry wsEntry;
    size_t copyLen = static_cast<size_t>(n) > sizeof(wsEntry.data) - 1
                     ? sizeof(wsEntry.data) - 1
                     : static_cast<size_t>(n);
    std::memcpy(wsEntry.data, buf, copyLen);
    wsEntry.data[copyLen] = '\0';
    wsEntry.len = copyLen;
    if (gWsSendQueue) {
        xQueueSend(gWsSendQueue, &wsEntry, 0);
    }
}

// Centralised GPIO init — runs in app_main BEFORE any task creation.
// All gpio_config/gpio_set_direction calls are here to avoid spinlock
// deadlock with PHY calibration (LL-031). Drivers only use gpio_set_level
// and gpio_get_level which do not take gpio_spinlock.
static void configureGpioPins() {
    using namespace ecotiter;
    // DIR pin (GPIO5) — safe
    gpio_set_direction(config::PIN_DIR, GPIO_MODE_OUTPUT);
    gpio_set_level(config::PIN_DIR, 0);

    // EN pin (GPIO13) — safe (moved from GPIO27: LL-027 PSRAM D3)
    gpio_set_direction(config::PIN_EN, GPIO_MODE_OUTPUT);
    gpio_set_level(config::PIN_EN, 0);  // Active LOW: enable driver

    // VALVE pin (GPIO14)
    gpio_set_direction(config::PIN_VALVE, GPIO_MODE_OUTPUT);
    gpio_set_level(config::PIN_VALVE, 0);  // Default: input position

    // FULL endstop (GPIO7) — input with pull-down, pos-edge interrupt
    gpio_config_t fullConf = {};
    fullConf.pin_bit_mask = (1ULL << config::PIN_LIMIT_FULL);
    fullConf.mode = GPIO_MODE_INPUT;
    fullConf.pull_up_en = GPIO_PULLUP_DISABLE;
    fullConf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    fullConf.intr_type = GPIO_INTR_POSEDGE;
    ESP_ERROR_CHECK(gpio_config(&fullConf));

    // EMPTY endstop (GPIO15) — input, floating, pos-edge interrupt
    gpio_config_t emptyConf = {};
    emptyConf.pin_bit_mask = (1ULL << config::PIN_LIMIT_EMPTY);
    emptyConf.mode = GPIO_MODE_INPUT;
    emptyConf.pull_up_en = GPIO_PULLUP_DISABLE;
    emptyConf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    emptyConf.intr_type = GPIO_INTR_POSEDGE;
    ESP_ERROR_CHECK(gpio_config(&emptyConf));

    // Install ISR service once (for all limit switch pins)
    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_IRAM));
}

// GR-3: net_owner thread — WiFi init → HTTP server → BLE init → process loop
extern "C" void netTaskEntry(void* pvParameters) {
    auto* params = static_cast<NetTaskParams*>(pvParameters);
    puts("DBG: netTaskEntry START"); fflush(stdout);

    using namespace ecotiter::infrastructure::network;
    ecotiter::diag::StackMonitor::instance().registerThread(
        "net_owner", ecotiter::domain::NET_OWNER_STACK);
    esp_task_wdt_add(NULL);

    WifiManager wifiManager;
    auto wifiResult = wifiManager.init();
    if (!wifiResult) {
        ESP_LOGE("net_owner", "WiFi init failed");
        vTaskDelete(nullptr);
        return;
    }

    // Try STA first — iterates through saved NVS networks, blocks per slot
    bool staConnected = wifiManager.tryStartSTA();
    if (!staConnected) {
        wifiManager.startAP();
    }

    // GR-3: HTTP server immediately after WiFi. All tasks independent.
    HttpServer httpServer;
    httpServer.setWifiManager(&wifiManager);
    auto httpResult = httpServer.init();
    if (httpResult) {
        httpServer.registerRoutes();
        gHttpServerForWs.store(&httpServer, std::memory_order_release);
        ecotiter::domain::LogBuffer::instance().setCallback(wsLogCallback);

        if (staConnected) {
            ESP_LOGI("net_owner", "HTTP server ready (STA mode)");
        } else {
            ESP_LOGI("net_owner", "HTTP server ready on 192.168.4.1:80 (AP mode)");
        }
    } else {
        ESP_LOGW("net_owner", "HTTP server init failed");
    }

    // GR-3: BLE init after HTTP server — ensures 12KB+ contiguous DRAM is available
    if (params && params->bleManager) {
        auto bleResult = params->bleManager->init();
        if (bleResult) {
            ESP_LOGI("net_owner", "BLE initialized successfully");
            startBleNotifyThread(*params->bleManager);
        } else {
            ESP_LOGW("net_owner", "BLE init skipped (insufficient heap or HW error)");
        }
    }

    // Create ws_send_queue for log_worker → net_owner log forwarding (GR-14)
    gWsSendQueue = xQueueCreate(16, sizeof(WsSendEntry));
    if (gWsSendQueue == nullptr) {
        ESP_LOGE("net_owner", "Failed to create ws_send_queue");
    }

    // LL-038: async log worker
    TaskHandle_t logWorkerHandle = nullptr;
    xTaskCreate(ecotiter::domain::LogBuffer::workerTaskEntry,
                "log_worker", ecotiter::domain::LOG_WORKER_STACK / sizeof(configSTACK_DEPTH_TYPE),
                nullptr, 0, &logWorkerHandle);
    if (logWorkerHandle != nullptr) {
        ecotiter::diag::StackMonitor::instance().registerByHandle(
            logWorkerHandle, "log_worker", ecotiter::domain::LOG_WORKER_STACK);
    }

    while (true) {
        esp_task_wdt_reset();

        // Drain ws_send_queue: broadcast log messages via WebSocket (GR-14)
        auto* hs = gHttpServerForWs.load(std::memory_order_acquire);
        if (hs) {
            WsSendEntry wsEntry;
            while (gWsSendQueue && xQueueReceive(gWsSendQueue, &wsEntry, 0) == pdTRUE) {
                hs->broadcastWsEvent(wsEntry.data, wsEntry.len);
            }
        }

        wifiManager.process();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

extern "C" void app_main(void)
{
    std::printf("BOOT OK: ecotiter v%s [%s] (git: %s)\n", ecotiter::app_version, ecotiter::build_date, ecotiter::git_hash);
    fflush(stdout);

    using namespace ecotiter;

    // Warmup: flush any stale serial data, ensure stdout works
    fflush(stdout);

    // Initialize LogBuffer with PSRAM backing (before log hook!)
    ecotiter::domain::LogBuffer::init(
        ecotiter::domain::memory::LOG_BUF_ENTRIES,
        &ecotiter::memory::psram_resource());

    // Install ESP_LOG capture → LogBuffer (captures all subsequent ESP_LOGI/LOGW/LOGE calls)
    esp_log_set_vprintf(logVprintf);

    ESP_LOGI(TAG, "Build: %s (git: %s)", ecotiter::build_date, ecotiter::git_hash);

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
    puts("DBG: step 5 - RWDT configured");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::RtcWdt, std::memory_order_release);
    diag::RtcWatchdog rtcWdt;
    diag::gRtcWdt = &rtcWdt;

    // ====== Step 6: BLE object construction (init deferred to net_owner) ======
    // GR-3: BLE init moved to net_owner thread after HTTP server to ensure
    // 12KB+ contiguous DRAM is available for httpd_start(). The BleManager
    // object is constructed here so the main loop can access it, but init()
    // is called from netTaskEntry after WiFi + HTTP.
    puts("DBG: step 6 - BLE object created");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::BleInit, std::memory_order_release);
    infrastructure::network::BleManager bleManager;

    // ====== Step 7: GPIO init (all gpio_config/gpio_set_direction) ======
    // Centralized before any task creation to avoid PHY calibration
    // gpio_spinlock deadlock (LL-031).
    puts("DBG: step 7 - configureGpioPins");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::PhyWait, std::memory_order_release);
    configureGpioPins();
    puts("DBG: gpio config done");
    fflush(stdout);

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

    ecotiter::diag::print_heap_stats();

    diag::FfiGuard guard(50);
    infrastructure::drivers::AdcDriver adc(ADC_UNIT_1, ADC_CHANNEL_3);
    gAdcDriver = &adc;
    ecotiter::application::setAdcSampleReadCb(adcSampleRead);

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

    esp_task_wdt_add(NULL);

    while (true)
    {
        ecotiter::diag::TickWatchdog watchdog;
        (void)watchdog;

        // Feed RWDT every main loop iteration (10ms).
        // If ANY task holds a spinlock > 6s, RWDT fires → RESET_SYSTEM.
        rtcWdt.feed();
        esp_task_wdt_reset();

        scheduler.tick();

        static TickType_t last_heap_log = 0;
        TickType_t now_ticks = xTaskGetTickCount();
        if (now_ticks - last_heap_log > pdMS_TO_TICKS(60000)) {
            ecotiter::diag::print_heap_stats();
            last_heap_log = now_ticks;
        }

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
            ecotiter::domain::gSpeedMlMin.store(
                ecotiter::domain::gSpeed.load(std::memory_order_acquire) > 0 ? 10.0f : 0.0f,
                std::memory_order_release);

            ecotiter::interface::BroadcastEvent evt{
                .tick = ecotiter::application::gTick.load(std::memory_order_acquire),
                .tempCX100 = ecotiter::domain::gTempCX100.load(std::memory_order_acquire),
                .mv = ecotiter::domain::gLastMv.load(std::memory_order_acquire),
                .vlv = ecotiter::domain::gValvePosition.load(std::memory_order_acquire),
                .brt = brtState,
                .volumeMl = ecotiter::domain::gVolumeMl.load(std::memory_order_acquire),
                .speedMlMin = motorMoving
                    ? ecotiter::domain::gSpeedMlMin.load(std::memory_order_acquire)
                    : 0.0f,
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

            // Compact format → Serial + BLE
            {
                ecotiter::domain::memory::ResponseBuffer buf{};
                auto sv = ecotiter::interface::serializeBroadcastCompact(evt, buf);
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
                }
            }

            // Extended format → WebSocket
            {
                ecotiter::domain::memory::ResponseBuffer buf{};
                auto sv = ecotiter::interface::serializeBroadcastExtended(evt, buf);
                if (!sv.empty())
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
                        // Save cmd id for result correlation
                        if (rsp->kind == ecotiter::application::ResponseKind::AckThen)
                        {
                            ecotiter::domain::gLastCmdId.store(
                                cmd->id, std::memory_order_release);
                        }
                        sendResponse(*rsp);
                    }
                    else
                    {
                        sendResponse(ecotiter::application::makeErrorResponse("dispatch failed"));
                    }
                }
                else
                {
                    auto errRsp = ecotiter::application::makeErrorResponse("invalid_params");
                    errRsp.id = extractCmdId(bleItem.data);
                    sendResponse(errRsp);
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
                    if (rsp->kind == ecotiter::application::ResponseKind::AckThen)
                    {
                        ecotiter::domain::gLastCmdId.store(
                            cmd->id, std::memory_order_release);
                    }
                    sendResponse(*rsp);
                }
                else
                {
                    sendResponse(ecotiter::application::makeErrorResponse("dispatch failed"));
                }
            }
            else
            {
                auto errRsp = ecotiter::application::makeErrorResponse("invalid_params");
                errRsp.id = extractCmdId(line->data());
                sendResponse(errRsp);
            }
        }

        // Deliver pending motor result over Serial/BLE
        if (ecotiter::domain::gHasPendingResult.load(std::memory_order_acquire))
        {
            uint64_t resultId = ecotiter::domain::gLastCmdId.load(std::memory_order_acquire);
            auto& smResult = infrastructure::gSmResult;

            ecotiter::domain::memory::ResponseBuffer buf{};
            size_t off = 0;

            // Use None type as generic "move done" for basic operations
            if (smResult.type == infrastructure::SmResult::Type::None && smResult.stepsTaken > 0)
            {
                auto cal = ecotiter::infrastructure::storage::calibrationRead();
                float volDispensed = cal
                    ? static_cast<float>(smResult.stepsTaken) / cal->stepsPerMl
                    : 0.0f;
                off = static_cast<size_t>(
                    std::snprintf(buf.data(), buf.size(),
                        R"({"id":%llu,"status":"ok","data":{"volume_dispensed_ml":%.2f}})",
                        static_cast<unsigned long long>(resultId),
                        static_cast<double>(volDispensed)));
            }
            else if (smResult.type == infrastructure::SmResult::Type::RinseComplete)
            {
                off = static_cast<size_t>(
                    std::snprintf(buf.data(), buf.size(),
                        R"({"id":%llu,"status":"ok","data":{"cycles_completed":1}})",
                        static_cast<unsigned long long>(resultId)));
            }
            else if (smResult.type == infrastructure::SmResult::Type::CalDoseComplete)
            {
                auto cal = ecotiter::infrastructure::storage::calibrationRead();
                float volDispensed = cal
                    ? static_cast<float>(smResult.stepsTaken) / cal->stepsPerMl
                    : 0.0f;
                off = static_cast<size_t>(
                    std::snprintf(buf.data(), buf.size(),
                        R"({"id":%llu,"status":"ok","data":{"volume_dispensed_ml":%.2f}})",
                        static_cast<unsigned long long>(resultId),
                        static_cast<double>(volDispensed)));
            }
            else if (smResult.type == infrastructure::SmResult::Type::CalSpeedComplete)
            {
                off = static_cast<size_t>(
                    std::snprintf(buf.data(), buf.size(),
                        R"({"id":%llu,"status":"ok","data":{"speed_ml_min":%.2f}})",
                        static_cast<unsigned long long>(resultId),
                        static_cast<double>(smResult.measuredSpeedMlMin)));
            }
            else if (smResult.type == infrastructure::SmResult::Type::CalSpeedSeqComplete)
            {
                off = static_cast<size_t>(
                    std::snprintf(buf.data(), buf.size(),
                        R"({"id":%llu,"status":"ok","data":{"status":"complete"}})",
                        static_cast<unsigned long long>(resultId)));
            }
            else
            {
                off = static_cast<size_t>(
                    std::snprintf(buf.data(), buf.size(),
                        R"({"id":%llu,"status":"ok","data":{"status":"complete"}})",
                        static_cast<unsigned long long>(resultId)));
            }

            if (off > 0 && off < buf.size())
            {
                buf[off++] = '\n';
                serial.write({buf.data(), off});
                if (bleManager.isConnected())
                {
                    std::ignore = bleManager.sendNotification({buf.data(), off});
                }
            }

            // Clear result
            ecotiter::domain::gHasPendingResult.store(false, std::memory_order_release);
            smResult.type = infrastructure::SmResult::Type::None;
            smResult.stepsTaken = 0;
        }

        vTaskDelayUntil(&lastWake, PACING_TICK);
    }
}

