#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "domain/memory.hpp"
#include "infrastructure/config.hpp"

namespace ecotiter::infrastructure::network
{
class BleManager;
class HttpServer;
} // namespace ecotiter::infrastructure::network

struct NetTaskParams
{
    ecotiter::infrastructure::network::BleManager* bleManager;
};

extern std::atomic<ecotiter::infrastructure::network::HttpServer*> gHttpServerForWs;

struct WsSendEntry
{
    char data[ecotiter::config::WS_BUF_SIZE];
    size_t len;
};

struct WifiConnectCommand
{
    char ssid[32];
    char password[64];
};

struct WsBroadcastEntry
{
    char data[ecotiter::domain::memory::MAX_RSP_SIZE];
    size_t len;
};

extern QueueHandle_t gWsSendQueue;
extern QueueHandle_t gWsBroadcastQueue;
extern QueueHandle_t gNetOwnerCmdQueue;

extern "C" void netTaskEntry(void* pvParameters);
