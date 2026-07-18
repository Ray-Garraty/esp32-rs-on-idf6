#include "net_owner.hpp"

#include <cstdio>
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "diag/stack_monitor.hpp"
#include "domain/log_buffer.hpp"
#include "domain/memory.hpp"
#include "domain/types.hpp"
#include "infrastructure/network/ble.hpp"
#include "infrastructure/network/ble_notify_thread.hpp"
#include "infrastructure/network/http_server.hpp"
#include "infrastructure/network/wifi.hpp"
#include "esp_system.h"

#include "log_capture.hpp"

using namespace ecotiter;

std::atomic<ecotiter::infrastructure::network::HttpServer*> gHttpServerForWs{nullptr};
QueueHandle_t gWsSendQueue = nullptr;
QueueHandle_t gWsBroadcastQueue = nullptr;
QueueHandle_t gNetOwnerCmdQueue = nullptr;

namespace
{

using namespace ecotiter::infrastructure::network;

void drainWsSendQueue(HttpServer* hs)
{
    WsSendEntry wsEntry;
    while (gWsSendQueue && xQueueReceive(gWsSendQueue, &wsEntry, 0) == pdTRUE)
    {
        hs->broadcastWsEvent(wsEntry.data, wsEntry.len);
    }
}

void drainWsBroadcastQueue(HttpServer* hs)
{
    WsBroadcastEntry bcEntry;
    while (gWsBroadcastQueue && xQueueReceive(gWsBroadcastQueue, &bcEntry, 0) == pdTRUE)
    {
        hs->broadcastWsEvent(bcEntry.data, bcEntry.len);
    }
}

void handleWifiConnectCommand(WifiManager& wifiManager, NetTaskParams* params)
{
    WifiConnectCommand wifiCmd;
    if (!gNetOwnerCmdQueue || xQueueReceive(gNetOwnerCmdQueue, &wifiCmd, 0) != pdTRUE)
        return;

    ESP_LOGI("net_owner", "Processing WiFi connect to SSID: %s", wifiCmd.ssid);
    bool connected = wifiManager.connectSTA(wifiCmd.ssid, wifiCmd.password, 15000);

    WsBroadcastEntry wifiBcBuf;
    int n = std::snprintf(wifiBcBuf.data, sizeof(wifiBcBuf.data),
                          R"({"event":"wifi_connect_result","success":%s,"ssid":"%s"})",
                          connected ? "true" : "false", wifiCmd.ssid);
    if (n > 0 && static_cast<size_t>(n) < sizeof(wifiBcBuf.data))
    {
        wifiBcBuf.len = static_cast<size_t>(n);
        if (gWsBroadcastQueue)
            xQueueSend(gWsBroadcastQueue, &wifiBcBuf, 0);
    }

    if (connected)
    {
        ESP_LOGI("net_owner", "WiFi connected, restarting in STA mode");
        auto* hs = gHttpServerForWs.load(std::memory_order_acquire);
        if (hs)
            drainWsBroadcastQueue(hs);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    else
    {
        ESP_LOGW("net_owner", "WiFi connect failed for: %s", wifiCmd.ssid);
    }
}

namespace
{

/// Initialize WiFi: call init(), try STA, fall back to AP.
/// Returns false on fatal error (caller should vTaskDelete).
bool initWiFi(WifiManager& wifiManager, bool& staConnected)
{
    auto wifiResult = wifiManager.init();
    if (!wifiResult)
    {
        ESP_LOGE("net_owner", "WiFi init failed");
        return false;
    }
    staConnected = wifiManager.tryStartSTA();
    if (!staConnected)
        wifiManager.startAP();
    return true;
}

/// Initialize and configure the HTTP server, wire up the WS broadcast queue.
void initHttpServer(HttpServer& httpServer, WifiManager& wifiManager, bool staConnected)
{
    httpServer.setWifiManager(&wifiManager);
    auto httpResult = httpServer.init();
    if (httpResult)
    {
        httpServer.registerRoutes();
        gHttpServerForWs.store(&httpServer, std::memory_order_release);
        ecotiter::domain::LogBuffer::instance().setCallback(wsLogCallback);
        ESP_LOGI("net_owner", "HTTP server ready (%s mode)", staConnected ? "STA" : "AP");
    }
    else
    {
        ESP_LOGW("net_owner", "HTTP server init failed");
    }
}

/// Initialize BLE manager and start the notify thread.
void initBle(NetTaskParams* params)
{
    if (params && params->bleManager)
    {
        auto bleResult = params->bleManager->init();
        if (bleResult)
        {
            ESP_LOGI("net_owner", "BLE initialized successfully");
            startBleNotifyThread(*params->bleManager);
        }
        else
        {
            ESP_LOGW("net_owner", "BLE init skipped");
        }
    }
}

} // anonymous namespace

void netInitPhase(WifiManager& wifiManager, HttpServer& httpServer, NetTaskParams* params,
                  bool& staConnected)
{
    ecotiter::diag::StackMonitor::instance().registerThread("net_owner",
                                                             ecotiter::domain::NET_OWNER_STACK);
    esp_task_wdt_add(nullptr);

    if (!initWiFi(wifiManager, staConnected))
    {
        vTaskDelete(nullptr);
        return;
    }

    initHttpServer(httpServer, wifiManager, staConnected);
    initBle(params);
    ecotiter::diag::StackMonitor::instance().registerLazyTasks();
}

void createNetQueues()
{
    gWsSendQueue = xQueueCreate(config::WS_SEND_QUEUE_DEPTH, sizeof(WsSendEntry));
    if (!gWsSendQueue)
        ESP_LOGE("net_owner", "Failed to create ws_send_queue");

    gWsBroadcastQueue = xQueueCreate(config::WS_BROADCAST_QUEUE_DEPTH, sizeof(WsBroadcastEntry));
    if (!gWsBroadcastQueue)
        ESP_LOGE("net_owner", "Failed to create ws_broadcast_queue");

    gNetOwnerCmdQueue = xQueueCreate(config::NET_OWNER_CMD_QUEUE_DEPTH, sizeof(WifiConnectCommand));
    if (!gNetOwnerCmdQueue)
        ESP_LOGE("net_owner", "Failed to create net_owner command queue");
}

void startLogWorker()
{
    TaskHandle_t logWorkerHandle = nullptr;
    xTaskCreate(ecotiter::domain::LogBuffer::workerTaskEntry, "log_worker",
                ecotiter::domain::LOG_WORKER_STACK / sizeof(configSTACK_DEPTH_TYPE), nullptr, 0,
                &logWorkerHandle);
    if (logWorkerHandle != nullptr)
    {
        ecotiter::diag::StackMonitor::instance().registerByHandle(
            logWorkerHandle, "log_worker", ecotiter::domain::LOG_WORKER_STACK);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    ecotiter::diag::StackMonitor::instance().logAllWatermarks();
}

} // anonymous namespace

extern "C" void netTaskEntry(void* pvParameters)
{
    auto* params = static_cast<NetTaskParams*>(pvParameters);
    puts("DBG: netTaskEntry START");
    fflush(stdout);

    WifiManager wifiManager;
    HttpServer httpServer;
    bool staConnected = false;
    netInitPhase(wifiManager, httpServer, params, staConnected);
    createNetQueues();
    startLogWorker();

    while (true)
    {
        esp_task_wdt_reset();

        auto* hs = gHttpServerForWs.load(std::memory_order_acquire);
        if (hs)
        {
            drainWsSendQueue(hs);
            drainWsBroadcastQueue(hs);
        }

        handleWifiConnectCommand(wifiManager, params);

        wifiManager.process();
        if (params && params->bleManager)
            params->bleManager->process();

        vTaskDelay(pdMS_TO_TICKS(config::NET_OWNER_POLL_MS)); // nosemgrep: art-I-no-vtaskdelay
    }
}
