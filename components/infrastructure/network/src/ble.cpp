#include "infrastructure/network/ble.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "diag/ffi_guard.hpp"
#include "diag/heap_snapshot.hpp"
#include "domain/types.hpp"
#include "infrastructure/config.hpp"
#include "esp_coexist.h"

static constexpr auto TAG = "ble";

extern "C" void ble_store_config_init(void);

namespace ecotiter::infrastructure::network
{

// NUS UUIDs — 6e400001/2/3-b5a3-f393-e0a9-e50e24dc0000
// Stored in little-endian byte order per NimBLE convention
namespace
{

const ble_uuid128_t NUS_SVC_UUID = {.u = {.type = BLE_UUID_TYPE_128},
                                    .value = {0x00, 0x00, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93,
                                              0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e}};

const ble_uuid128_t NUS_RX_UUID = {.u = {.type = BLE_UUID_TYPE_128},
                                   .value = {0x00, 0x00, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93,
                                             0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e}};

const ble_uuid128_t NUS_TX_UUID = {.u = {.type = BLE_UUID_TYPE_128},
                                   .value = {0x00, 0x00, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93,
                                             0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e}};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

const ble_gap_adv_params ADV_PARAMS = {
    .conn_mode = BLE_GAP_CONN_MODE_UND,
    .disc_mode = BLE_GAP_DISC_MODE_GEN,
};

const ble_gatt_chr_def NUS_CHRS[] = {
    {
        .uuid = &NUS_RX_UUID.u,
        .access_cb = BleManager::gattEventCallback,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        .uuid = &NUS_TX_UUID.u,
        .access_cb = BleManager::gattEventCallback,
        .flags = BLE_GATT_CHR_F_NOTIFY,
    },
    {0},
};

const ble_gatt_svc_def GATT_SVCS[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &NUS_SVC_UUID.u,
        .characteristics = NUS_CHRS,
    },
    {0},
};

#pragma GCC diagnostic pop

} // anonymous namespace

BleManager* BleManager::s_instance = nullptr;

BleManager::BleManager()
{
    s_instance = this;
}

namespace
{

struct QueuePair
{
    QueueHandle_t cmdQueue;
    QueueHandle_t notifyQueue;
};

std::expected<QueuePair, domain::AppError> createQueues()
{
    size_t freeHeap = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (freeHeap < 30 * 1024)
    {
        ESP_LOGW(TAG, "Insufficient DRAM for BLE: %zu bytes < 30 KB", freeHeap);
        return std::unexpected(domain::AppError::Resource);
    }

    QueuePair q{};
    q.cmdQueue = xQueueCreate(BLE_CMD_QUEUE_SIZE, sizeof(BleCmdItem));
    if (q.cmdQueue == nullptr)
    {
        ESP_LOGE(TAG, "Failed to create command queue");
        return std::unexpected(domain::AppError::Resource);
    }

    if (!diag::HeapSnapshot::assertCanAllocate(BLE_NOTIFY_QUEUE_SIZE * sizeof(BleNotifyItem)))
    {
        ESP_LOGW(TAG, "Low DRAM before BLE notify queue creation");
    }
    q.notifyQueue = xQueueCreate(BLE_NOTIFY_QUEUE_SIZE, sizeof(BleNotifyItem));
    if (q.notifyQueue == nullptr)
    {
        ESP_LOGE(TAG, "Failed to create notify queue");
        vQueueDelete(q.cmdQueue);
        return std::unexpected(domain::AppError::Resource);
    }

    return q;
}

} // anonymous namespace

BleManager::~BleManager()
{
    if (cmdQueue_ != nullptr)
    {
        vQueueDelete(cmdQueue_);
        cmdQueue_ = nullptr;
    }
    if (notifyQueue_ != nullptr)
    {
        vQueueDelete(notifyQueue_);
        notifyQueue_ = nullptr;
    }
    initialized_ = false;
    s_instance = nullptr;
}

std::expected<void, domain::AppError> BleManager::init()
{
    puts("DBG: BLE init ENTER");
    fflush(stdout);
    if (initialized_)
        return {};

    auto queues = createQueues();
    if (!queues)
        return std::unexpected(queues.error());
    cmdQueue_ = queues->cmdQueue;
    notifyQueue_ = queues->notifyQueue;

    // Fix 3: Diagnostic markers around PHY-calibration-triggering call
    {
        puts("DBG: BLE - nimble_port_init (triggers async PHY calibration)");
        fflush(stdout);
        diag::FfiGuard guard(60);
        int rc = nimble_port_init();
        if (rc != 0)
        {
            ESP_LOGE(TAG, "nimble_port_init failed: %d", rc);
            return std::unexpected(domain::AppError::Resource);
        }
        puts("DBG: BLE - nimble_port_init done");
        fflush(stdout);
    }

    ble_att_set_preferred_mtu(config::BLE_PREFERRED_MTU);

    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_hs_cfg.sync_cb = onHostSync;
    ble_hs_cfg.reset_cb = onHostReset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_svc_gap_device_name_set("EcoTiter");

    {
        diag::FfiGuard guard(61);
        int rc = ble_gatts_count_cfg(GATT_SVCS);
        if (rc != 0)
        {
            ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
            return std::unexpected(domain::AppError::Resource);
        }
        rc = ble_gatts_add_svcs(GATT_SVCS);
        if (rc != 0)
        {
            ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
            return std::unexpected(domain::AppError::Resource);
        }
    }

    ble_store_config_init();

    {
        diag::FfiGuard guard(62);
        nimble_port_freertos_init(hostTaskEntry);
    }

    initialized_ = true;
    ESP_LOGI(TAG, "BLE initialized");
    return {};
}

void BleManager::process()
{
    if (!initialized_)
        return;

    {
        diag::FfiGuard guard(64);

        if (connected_)
        {
            int64_t now = esp_timer_get_time();
            int64_t elapsed = now - connectUs_;
            struct ble_gap_conn_desc desc;
            int rc = ble_gap_conn_find(connHandle_, &desc);
            if (rc != 0 && elapsed > 500000)
            {
                ESP_LOGW(TAG, "Zombie connection detected (L2)");
                connected_ = false;
                connHandle_ = 0;
                consecutiveFailures_ = 0;
                // Restart advertising so device becomes visible again
                ble_gap_adv_start(ownAddrType_, nullptr, BLE_HS_FOREVER, &ADV_PARAMS,
                                  gapEventCallback, nullptr);
                domain::gBleError.store(false, std::memory_order_release);
            }
        }

        if (connected_ && consecutiveFailures_ >= 5)
        {
            ESP_LOGW(TAG, "Too many notify failures (L1), disconnecting");
            ble_gap_terminate(connHandle_, BLE_ERR_REM_USER_CONN_TERM);
            connected_ = false;
            connHandle_ = 0;
            consecutiveFailures_ = 0;
            domain::gBleError.store(true, std::memory_order_release);
        }
    }
}

bool BleManager::isConnected() const noexcept
{
    return initialized_ && connected_;
}

bool BleManager::isInitialized() const noexcept
{
    return initialized_;
}

bool BleManager::sendNotification(std::string_view data)
{
    if (!initialized_ || !connected_)
        return false;

    {
        diag::FfiGuard guard(63);

        {
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(connHandle_, &desc) != 0)
            {
                return false;
            }
        }

        struct os_mbuf* om =
            ble_hs_mbuf_from_flat(reinterpret_cast<const uint8_t*>(data.data()), data.size());
        if (om == nullptr)
        {
            ESP_LOGW(TAG, "Failed to allocate mbuf for notify");
            consecutiveFailures_++;
            return false;
        }

        int rc = ble_gatts_notify_custom(connHandle_, txAttrHandle_, om);
        if (rc != 0)
        {
            os_mbuf_free_chain(om);
            consecutiveFailures_++;
            return false;
        }
    }

    consecutiveFailures_ = 0;
    return true;
}

namespace
{

bool syncGattHandles(uint16_t& txAttrHandle)
{
    int rc = ble_gatts_find_chr(&NUS_SVC_UUID.u, &NUS_TX_UUID.u, nullptr, &txAttrHandle);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "find TX chr failed: %d", rc);
        return false;
    }
    ESP_LOGI(TAG, "TX attr handle=%d", txAttrHandle);
    return true;
}

bool configureAdvertising(uint8_t& ownAddrType)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ensure addr failed: %d", rc);
        return false;
    }

    rc = ble_hs_id_infer_auto(0, &ownAddrType);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "infer addr failed: %d", rc);
        return false;
    }

    uint8_t addrVal[6] = {};
    rc = ble_hs_id_copy_addr(ownAddrType, addrVal, nullptr);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "copy addr failed: %d", rc);
        return false;
    }

    char deviceName[20];
    int nameLen = std::snprintf(deviceName, sizeof(deviceName), "EcoTiter-%02X%02X", addrVal[1],
                                addrVal[0]);
    if (nameLen < 0)
        return false;

    ESP_LOGI(TAG, "Device Address: %02X:%02X:%02X:%02X:%02X:%02X", addrVal[5], addrVal[4],
             addrVal[3], addrVal[2], addrVal[1], addrVal[0]);

    struct ble_hs_adv_fields fields;
    std::memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name = reinterpret_cast<uint8_t*>(deviceName);
    fields.name_len = static_cast<uint8_t>(nameLen);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "adv set fields failed: %d", rc);
        return false;
    }
    return true;
}

bool startAdvertising(uint8_t ownAddrType)
{
    // Set scan response data with NUS service UUID so scanners can find
    // the device by service UUID (ble_test.py checks SERVICE_UUID in svc_uuids)
    {
        struct ble_hs_adv_fields rsp_fields;
        std::memset(&rsp_fields, 0, sizeof(rsp_fields));
        rsp_fields.uuids128 = const_cast<ble_uuid128_t*>(&NUS_SVC_UUID);
        rsp_fields.num_uuids128 = 1;
        rsp_fields.uuids128_is_complete = 1;
        int rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
        if (rc != 0)
        {
            ESP_LOGE(TAG, "adv rsp set fields failed: %d", rc);
            return false;
        }
    }

    int rc = ble_gap_adv_start(ownAddrType, nullptr, BLE_HS_FOREVER, &ADV_PARAMS,
                               BleManager::gapEventCallback, nullptr);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "adv start failed: %d", rc);
        return false;
    }

    ESP_LOGI(TAG, "Advertising");
    domain::gBleError.store(false, std::memory_order_release);
    return true;
}

} // anonymous namespace

void BleManager::onHostSync()
{
    if (s_instance == nullptr)
        return;

    diag::FfiGuard guard(65);

    uint16_t txAttrHandle;
    if (!syncGattHandles(txAttrHandle))
        return;
    s_instance->txAttrHandle_ = txAttrHandle;

    uint8_t ownAddrType;
    if (!configureAdvertising(ownAddrType))
        return;
    s_instance->ownAddrType_ = ownAddrType;

    if (!startAdvertising(ownAddrType))
        return;
}

void BleManager::onHostReset(int reason)
{
    ESP_LOGW(TAG, "Host reset, reason=%d", reason);
}

void BleManager::hostTaskEntry(void* param)
{
    (void)param;
    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

int BleManager::gapEventCallback(struct ble_gap_event* event, void* arg)
{
    (void)arg;
    if (s_instance == nullptr)
        return 0;

    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT: {
        if (event->connect.status == 0)
        {
            s_instance->connected_ = true;
            s_instance->connHandle_ = event->connect.conn_handle;
            s_instance->consecutiveFailures_ = 0;
            s_instance->connectUs_ = esp_timer_get_time();
            domain::gBleError.store(false, std::memory_order_release);
            puts("DBG: BLE CONNECTED");
            fflush(stdout);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            esp_coex_preference_set(ESP_COEX_PREFER_BT);
#pragma GCC diagnostic pop
        }
        else
        {
            puts("DBG: BLE CONNECT FAILED");
            fflush(stdout);
        }
        return 0;
    }

    case BLE_GAP_EVENT_DISCONNECT: {
        s_instance->connected_ = false;
        s_instance->connHandle_ = 0;
        s_instance->consecutiveFailures_ = 0;
        domain::gBleError.store(false, std::memory_order_release);
        puts("DBG: BLE DISCONNECTED");
        fflush(stdout);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
#pragma GCC diagnostic pop

        diag::FfiGuard guard(65);
        ble_gap_adv_start(s_instance->ownAddrType_, nullptr, BLE_HS_FOREVER, &ADV_PARAMS,
                          gapEventCallback, nullptr);
        return 0;
    }

    default:
        return 0;
    }
}

int BleManager::gattEventCallback(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt* ctxt, void* arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (s_instance == nullptr)
        return 0;

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        uint16_t len = 0;
        BleCmdItem item{};
        int rc = ble_hs_mbuf_to_flat(ctxt->om, item.data, sizeof(item.data) - 1, &len);
        if (rc == 0 && len > 0)
        {
            item.data[len] = '\0';
            if (xQueueSend(s_instance->cmdQueue_, &item, 0) != pdTRUE)
            {
                ESP_LOGW(TAG, "Command queue full, dropping BLE write");
            }
        }
        return 0;
    }

    return 0;
}

} // namespace ecotiter::infrastructure::network
