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
#include "interface/broadcast.hpp"
#include "interface/serial.hpp"

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

    // GR-3: HTTP server init BEFORE BLE (DRAM is most abundant at this point)
    HttpServer httpServer;
    auto httpResult = httpServer.init();
    if (httpResult) {
        httpServer.registerRoutes();
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
        } else {
            ESP_LOGW("net_owner", "BLE init skipped (insufficient heap or HW error)");
        }
    }

    // LL-031: PHY calibration holds gpio_spinlock for 10-200ms after BT init.
    // Wait for it to release before any gpio_config/gpio_set_level operations.
    // GR-1: Blocking delay OK in worker thread (not main loop).
    ensureGpioReady();

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

    // ====== Step 1: NVS ======
    puts("DBG: step 1 - nvs_flash_init");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::Nvs, std::memory_order_release);
    nvs_flash_init();

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
            ecotiter::interface::BroadcastEvent evt{
                .tick = ecotiter::application::gTick.load(std::memory_order_acquire),
                .tempCX100 = ecotiter::domain::gTempCX100.load(std::memory_order_acquire),
                .mv = ecotiter::domain::gLastMv.load(std::memory_order_acquire),
                .vlv = ecotiter::domain::gValvePosition.load(std::memory_order_acquire),
                .brt = ecotiter::domain::gBuretteState.load(std::memory_order_acquire),
                .dir = ecotiter::domain::gDirection.load(std::memory_order_acquire),
                .speed = ecotiter::domain::gSpeed.load(std::memory_order_acquire),
                .accel = ecotiter::domain::gAccel.load(std::memory_order_acquire),
                .volumeMl = ecotiter::domain::gVolumeMl.load(std::memory_order_acquire),
                .dispensedSteps =
                    ecotiter::domain::gDispensedSteps.load(std::memory_order_acquire)};

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
