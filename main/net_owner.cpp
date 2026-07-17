#include "net_owner.hpp"

#include <cstdio>
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "infrastructure/network/wifi.hpp"
#include "infrastructure/network/http_server.hpp"
#include "infrastructure/network/ble.hpp"
#include "infrastructure/network/ble_notify_thread.hpp"
#include "domain/types.hpp"
#include "domain/log_buffer.hpp"
#include "diag/stack_monitor.hpp"

#include "log_capture.hpp"

using namespace ecotiter;

std::atomic<ecotiter::infrastructure::network::HttpServer*> gHttpServerForWs{nullptr};
QueueHandle_t gWsSendQueue = nullptr;
QueueHandle_t gWsBroadcastQueue = nullptr;

extern "C" void netTaskEntry(void* pvParameters) { // NOLINT(readability-function-cognitive-complexity) // reason: WiFi init -> HTTP -> BLE -> queue drain loop
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

    ecotiter::diag::StackMonitor::instance().registerLazyTasks();

    // Create ws_send_queue for log_worker → net_owner log forwarding (GR-14)
    gWsSendQueue = xQueueCreate(config::WS_SEND_QUEUE_DEPTH, sizeof(WsSendEntry));
    if (gWsSendQueue == nullptr) {
        ESP_LOGE("net_owner", "Failed to create ws_send_queue");
    }

    gWsBroadcastQueue = xQueueCreate(config::WS_BROADCAST_QUEUE_DEPTH, sizeof(WsBroadcastEntry));
    if (gWsBroadcastQueue == nullptr) {
        ESP_LOGE("net_owner", "Failed to create ws_broadcast_queue");
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
    vTaskDelay(pdMS_TO_TICKS(100));
    ecotiter::diag::StackMonitor::instance().logAllWatermarks();

    while (true) {
        esp_task_wdt_reset();

        // Drain ws_send_queue: broadcast log messages via WebSocket (GR-14)
        auto* hs = gHttpServerForWs.load(std::memory_order_acquire);
        if (hs) {
            WsSendEntry wsEntry;
            while (gWsSendQueue && xQueueReceive(gWsSendQueue, &wsEntry, 0) == pdTRUE) {
                hs->broadcastWsEvent(wsEntry.data, wsEntry.len);
            }
            // Drain ws_broadcast_queue
            static WsBroadcastEntry bcEntry;
            while (gWsBroadcastQueue && xQueueReceive(gWsBroadcastQueue, &bcEntry, 0) == pdTRUE) {
                hs->broadcastWsEvent(bcEntry.data, bcEntry.len);
            }
        }

        wifiManager.process();
        if (params && params->bleManager) {
            params->bleManager->process();
        }
        vTaskDelay(pdMS_TO_TICKS(config::NET_OWNER_POLL_MS)); // nosemgrep: art-I-no-vtaskdelay
    }
}
