#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "domain/errors.hpp"
#include "domain/memory.hpp"

struct ble_gap_event;
struct ble_gatt_access_ctxt;

namespace ecotiter::infrastructure::network
{

static constexpr size_t BLE_CMD_QUEUE_SIZE = 8;
static constexpr size_t BLE_NOTIFY_QUEUE_SIZE = 4;
static constexpr size_t BLE_NOTIFY_BUF_SIZE = domain::memory::MAX_RSP_SIZE;

struct BleCmdItem
{
    char data[domain::memory::MAX_CMD_SIZE];
};

struct BleNotifyItem
{
    char data[BLE_NOTIFY_BUF_SIZE];
    size_t len;
};

class BleManager
{
public:
    BleManager();
    ~BleManager();

    BleManager(const BleManager&) = delete;
    BleManager& operator=(const BleManager&) = delete;
    BleManager(BleManager&&) = delete;
    BleManager& operator=(BleManager&&) = delete;

    [[nodiscard]] std::expected<void, domain::AppError> init();
    void process();

    [[nodiscard]] bool isConnected() const noexcept;
    [[nodiscard]] bool isInitialized() const noexcept;

    [[nodiscard]] bool sendNotification(std::string_view data);

    QueueHandle_t commandQueue() const noexcept { return cmdQueue_; }
    QueueHandle_t notifyQueue() const noexcept { return notifyQueue_; }

    // C callbacks — public because referenced from C-style GATT/GAP defs
    static int gapEventCallback(struct ble_gap_event* event, void* arg);
    static int gattEventCallback(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt* ctxt, void* arg);

private:
    static void onHostSync();
    static void onHostReset(int reason);
    static void hostTaskEntry(void* param);

    static BleManager* s_instance;

    bool initialized_{false};
    bool connected_{false};
    uint16_t connHandle_{0};
    uint16_t txAttrHandle_{0};
    uint8_t ownAddrType_{0};
    uint8_t consecutiveFailures_{0};
    int64_t connectUs_{0};
    QueueHandle_t cmdQueue_{nullptr};
    QueueHandle_t notifyQueue_{nullptr};
};

} // namespace ecotiter::infrastructure::network
