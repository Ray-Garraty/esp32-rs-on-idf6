#include <cstdio>
#include <cstring>
#include <string>
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "application/command.hpp"
#include "application/dispatch.hpp"
#include "application/motor_controller.hpp"
#include "application/response.hpp"
#include "application/scheduler.hpp"
#include "application/state_machine.hpp"
#include "diag/black_box.hpp"
#include "diag/ffi_guard.hpp"
#include "diag/heap_monitor.hpp"
#include "diag/heap_snapshot.hpp"
#include "diag/rtc_watchdog.hpp"
#include "diag/stack_monitor.hpp"
#include "diag/tick_watchdog.hpp"
#include "domain/broadcast_helpers.hpp"
#include "domain/calibration.hpp"
#include "domain/log_buffer.hpp"
#include "infrastructure/cal_cache.hpp"
#include "infrastructure/config.hpp"
#include "infrastructure/drivers/adc.hpp"
#include "infrastructure/drivers/rgb_led.hpp"
#include "infrastructure/memory/psram_resource.hpp"
#include "infrastructure/motor_controller_impl.hpp"
#include "infrastructure/motor_task.hpp"
#include "infrastructure/network/ble.hpp"
#include "infrastructure/network/ble_notify_thread.hpp"
#include "infrastructure/network/http_server.hpp"
#include "infrastructure/network/wifi.hpp"
#include "infrastructure/storage/nvs.hpp"
#include "infrastructure/temp_thread.hpp"
#include "interface/broadcast.hpp"
#include "interface/serial.hpp"
#include "version.h"

#include "gpio_config.hpp"
#include "log_capture.hpp"
#include "net_owner.hpp"

using namespace ecotiter;

// Manual JSON id extractor — avoids nlohmann dependency in main.cpp
static uint64_t extractCmdId(const char* data)
{
    const char* p = std::strstr(data, "\"id\":");
    if (!p)
        return 0;
    p += 5;
    while (*p == ' ' || *p == '\t')
        ++p;
    if (*p < '0' || *p > '9')
        return 0;
    uint64_t val = 0;
    while (*p >= '0' && *p <= '9')
    {
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
    if (largest < stackSize + config::DRAM_SAFETY_MARGIN)
    {
        ESP_LOGW(TAG, "DRAM pressure! Largest free block %lu < needed %lu", (unsigned long)largest,
                 (unsigned long)(stackSize + config::DRAM_SAFETY_MARGIN));
    }
}

// ADC driver pointer for temp_thread — set in app_main after AdcDriver creation
using ecotiter::infrastructure::drivers::gAdcDriver;

static uint16_t adcSampleRead()
{
    if (gAdcDriver)
    {
        auto raw = gAdcDriver->readRaw();
        if (raw)
            return *raw;
    }
    return 0;
}

extern "C" void app_main(void) // NOLINT(readability-function-cognitive-complexity) // reason: boot
                               // sequence 9 steps + event loop
{
    std::printf("BOOT OK: ecotiter v%s [%s] (git: %s)\n", ecotiter::app_version,
                ecotiter::build_date, ecotiter::git_hash);
    fflush(stdout);

    using namespace ecotiter;

    // Warmup: flush any stale serial data, ensure stdout works
    fflush(stdout);

    // Initialize LogBuffer with PSRAM backing (before log hook!)
    ecotiter::domain::LogBuffer::init(ecotiter::domain::memory::LOG_BUF_ENTRIES,
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
    domain::gStallGuardThreshold.store(infrastructure::storage::stallguardReadThreshold(),
                                       std::memory_order_release);

    // Populate NVS calibration cache once at boot (Fix 5)
    {
        auto bootCal = infrastructure::storage::calibrationRead();
        if (bootCal)
        {
            // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // reason: heap-allocated CalibrationData stored in gCalCache
            infrastructure::gCalCache.store(new domain::CalibrationData(*bootCal),
                                    std::memory_order_release);
        }
    }

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
    puts("DBG: step 5 - RWDT configured");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::RtcWdt, std::memory_order_release);
    diag::RtcWatchdog rtcWdt;
    diag::gRtcWdt = &rtcWdt;

    // ====== Step 6: BLE object construction (init deferred to net_owner) ======
    puts("DBG: step 6 - BLE object created");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::BleInit, std::memory_order_release);
    infrastructure::network::BleManager bleManager;

    // ====== Step 7: GPIO init (all gpio_config/gpio_set_direction) ======
    puts("DBG: step 7 - configureGpioPins");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::PhyWait, std::memory_order_release);
    configureGpioPins();
    puts("DBG: gpio config done");
    fflush(stdout);

    // ====== Step 7b: RGB LED ======
    infrastructure::drivers::RgbLed rgbLed(config::PIN_LED_RGB);
    domain::TransportMode currentLedMode = domain::TransportMode::BleAdvertising;
    bool currentLedError = false;
    rgbLed.setTransportMode(currentLedMode, currentLedError);

    // ====== Step 7c: Temperature thread ======
    puts("DBG: step 7c - xTaskCreate temp");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::TempTask, std::memory_order_release);
    logDramBeforeTask("temp", domain::TEMP_THREAD_STACK);
    BaseType_t tt = xTaskCreate(tempTaskEntry, "temp",
                                domain::TEMP_THREAD_STACK / sizeof(configSTACK_DEPTH_TYPE), nullptr,
                                1, nullptr);
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
    if (!diag::HeapSnapshot::assertCanAllocate(domain::NET_OWNER_STACK +
                                               config::DRAM_SAFETY_MARGIN))
    {
        ESP_LOGW(TAG, "Low DRAM before net_owner task creation");
    }
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

    // Instantiate motor controller (wraps gMotorCmdQueue / gSmResultQueue globals)
    // Must be done after motor task creates the queues but before dispatch uses them.
    {
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // reason: heap-allocated
        auto* mc = new infrastructure::MotorControllerImpl();
        application::setMotorController(mc);
        ESP_LOGI(TAG, "MotorControllerImpl created and registered with dispatch");
    }

    diag::FfiGuard guard(config::FFI_BOOT_SEQUENCE);
    infrastructure::drivers::AdcDriver adc(static_cast<adc_unit_t>(config::ADC_UNIT),
                                           static_cast<adc_channel_t>(config::ADC_CHANNEL));
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
            // Also send over BLE if connected (non-blocking queue push)
            if (bleManager.notifyQueue())
            {
                ecotiter::infrastructure::network::BleNotifyItem item;
                size_t copyLen = std::min(len, sizeof(item.data));
                std::memcpy(item.data, buf.data(), copyLen);
                item.len = copyLen;
                xQueueSend(bleManager.notifyQueue(), &item, 0);
            }
        }
    };

    esp_task_wdt_add(NULL);

    while (true)
    {
        // Feed RWDT every main loop iteration (10ms).
        rtcWdt.feed();
        esp_task_wdt_reset();

        // TickWatchdog measures body execution time only (excludes vTaskDelayUntil sleep)
        {
            ecotiter::diag::TickWatchdog watchdog;
            (void)watchdog;

            scheduler.tick();

            appStateMachine.tick(application::gTick.load(std::memory_order_acquire));

            if (scheduler.shouldBroadcast())
            {
                domain::updateBroadcastState();

                ecotiter::interface::BroadcastEvent evt{
                    .tick = ecotiter::application::gTick.load(std::memory_order_acquire),
                    .tempCX100 = ecotiter::domain::gTempCX100.load(std::memory_order_acquire),
                    .mv = ecotiter::domain::gLastMv.load(std::memory_order_acquire),
                    .vlv = ecotiter::domain::gValvePosition.load(std::memory_order_acquire),
                    .brt = ecotiter::domain::gBuretteState.load(std::memory_order_acquire),
                    .volumeMl = ecotiter::domain::gVolumeMl.load(std::memory_order_acquire),
                    .speedMlMin =
                        ecotiter::domain::gSpeedMlMin.load(std::memory_order_acquire),
                    .limitFull = ecotiter::domain::gStopFull.load(std::memory_order_acquire),
                    .limitEmpty = ecotiter::domain::gStopEmpty.load(std::memory_order_acquire),
                    .usbSerialConnected =
                        ecotiter::domain::gUsbHandshakeReceived.load(std::memory_order_acquire),
                    .bleConnected = bleManager.isConnected(),
                    .stepperDrvConnected = true,
                    .stepperDrvOtpw = false,
                    .stepperDrvOt = false,
                    .stallGuardValue = 0,
                    .isStalled = false,
                    .stallGuardThreshold =
                        ecotiter::domain::gStallGuardThreshold.load(std::memory_order_acquire),
                    .motorIsMoving = ecotiter::domain::gMotorIsMoving.load(std::memory_order_acquire),
                    .stepsTaken =
                        ecotiter::domain::gDispensedSteps.load(std::memory_order_acquire)};

                // Compact format → Serial + BLE
                {
                    ecotiter::domain::memory::ResponseBuffer buf{};
                    auto sv = ecotiter::interface::serializeBroadcastCompact(evt, buf);
                    if (!sv.empty())
                    {
                        serial.write(sv);
                        serial.write({"\n", 1});
                        if (bleManager.notifyQueue())
                        {
                            ecotiter::infrastructure::network::BleNotifyItem item;
                            std::memcpy(item.data, sv.data(),
                                        std::min(sv.size(), sizeof(item.data)));
                            if (sv.size() < sizeof(item.data))
                            {
                                item.data[sv.size()] = '\n';
                                item.len = sv.size() + 1;
                            }
                            else
                            {
                                item.len = sv.size();
                            }
                            xQueueSend(bleManager.notifyQueue(), &item, 0);
                        }
                    }
                }

                // Extended format → WebSocket
                {
                    ecotiter::domain::memory::ResponseBuffer buf{};
                    auto sv = ecotiter::interface::serializeBroadcastExtended(evt, buf);
                    if (!sv.empty())
                    {
                        if (gWsBroadcastQueue)
                        {
                            WsBroadcastEntry entry;
                            size_t copyLen = std::min(sv.size(), sizeof(entry.data));
                            std::memcpy(entry.data, sv.data(), copyLen);
                            entry.len = copyLen;
                            xQueueSend(gWsBroadcastQueue, &entry, 0);
                        }
                    }
                }
            }

            // Update RGB LED based on BLE/USB state
            {
                domain::TransportMode newLedMode;
                bool newLedError;

                if (domain::gUsbHandshakeReceived.load(std::memory_order_acquire))
                {
                    newLedMode = domain::TransportMode::UsbActive;
                    newLedError = false;
                }
                else if (domain::gBleError.load(std::memory_order_acquire))
                {
                    newLedMode = domain::TransportMode::BleAdvertising;
                    newLedError = true;
                }
                else if (bleManager.isConnected())
                {
                    newLedMode = domain::TransportMode::BleConnected;
                    newLedError = false;
                }
                else
                {
                    newLedMode = domain::TransportMode::BleAdvertising;
                    newLedError = false;
                }

                if (newLedMode != currentLedMode || newLedError != currentLedError)
                {
                    char dbg[80];
                    std::snprintf(
                        dbg, sizeof(dbg), "DBG: LED %d->%d err=%d isConnected=%d gBleErr=%d",
                        static_cast<int>(currentLedMode), static_cast<int>(newLedMode),
                        static_cast<int>(newLedError), static_cast<int>(bleManager.isConnected()),
                        static_cast<int>(domain::gBleError.load(std::memory_order_acquire)));
                    puts(dbg);
                    fflush(stdout);
                    currentLedMode = newLedMode;
                    currentLedError = newLedError;
                    rgbLed.setTransportMode(currentLedMode, currentLedError);
                }
            }

            // Update transport state in appStateMachine
            if (domain::gUsbHandshakeReceived.load(std::memory_order_acquire))
            {
                appStateMachine.setTransportState(application::TransportState::UsbActive);
            }
            else if (bleManager.isConnected())
            {
                appStateMachine.setTransportState(application::TransportState::BleConnected);
            }
            else
            {
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
                                ecotiter::domain::gLastCmdId.store(cmd->id,
                                                                   std::memory_order_release);
                            }
                            sendResponse(*rsp);
                        }
                        else
                        {
                            sendResponse(
                                ecotiter::application::makeErrorResponse("dispatch failed"));
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
                auto lineStr = std::string(line->data(), line->size());
                ESP_LOGI(TAG, "RX: %s", lineStr.c_str());

                ecotiter::domain::gUsbHandshakeReceived.store(true, std::memory_order_release);

                auto cmd = ecotiter::application::parseCommand(*line);
                if (cmd)
                {
                    auto rsp = ecotiter::application::dispatch(*cmd);
                    if (rsp)
                    {
                        if (rsp->kind == ecotiter::application::ResponseKind::AckThen)
                        {
                            ecotiter::domain::gLastCmdId.store(cmd->id, std::memory_order_release);
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
                    errRsp.id = extractCmdId(lineStr.c_str());
                    sendResponse(errRsp);
                }
            }

            {
                auto* controller = ecotiter::application::getMotorController();
                if (controller)
                {
                    auto smResultOpt = controller->waitResult(0);  // non-blocking
                    if (smResultOpt.has_value())
                    {
                        auto& smResult = *smResultOpt;
                        uint64_t resultId =
                            ecotiter::domain::gLastCmdId.load(std::memory_order_acquire);

                        ecotiter::domain::memory::ResponseBuffer buf{};
                        size_t off = ecotiter::application::formatSmResult(buf, resultId, smResult);

                        if (off > 0 && off < buf.size())
                        {
                            buf[off++] = '\n';
                            serial.write({buf.data(), off});
                            if (bleManager.notifyQueue())
                            {
                                ecotiter::infrastructure::network::BleNotifyItem item;
                                size_t copyLen = std::min(off, sizeof(item.data));
                                std::memcpy(item.data, buf.data(), copyLen);
                                item.len = copyLen;
                                xQueueSend(bleManager.notifyQueue(), &item, 0);
                            }
                        }
                    }
                }
            }

        } // TickWatchdog scope end

        vTaskDelayUntil(&lastWake, PACING_TICK);
    }
}
