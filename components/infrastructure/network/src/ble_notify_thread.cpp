#include "infrastructure/network/ble_notify_thread.hpp"
#include "infrastructure/network/ble.hpp"

#include <cstring>
#include <thread>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "diag/stack_monitor.hpp"
#include "domain/types.hpp"

static constexpr auto TAG = "ble_notify";

namespace
{

void bleNotifyLoop(ecotiter::infrastructure::network::BleManager* manager)
{ // NOLINT(readability-function-cognitive-complexity) // reason: BLE notification queue drain with
  // MTU split
    ecotiter::diag::StackMonitor::instance().registerThread("ble_notify",
                                                            ecotiter::domain::BLE_NOTIFY_STACK);

    while (true)
    {
        ecotiter::infrastructure::network::BleNotifyItem item;
        if (xQueueReceive(manager->notifyQueue(), &item, pdMS_TO_TICKS(10)))
        {
            if (item.len == 0)
                continue;

            if (manager->isConnected())
            {
                bool ok = manager->sendNotification({item.data, item.len});
                if (!ok)
                {
                    ESP_LOGW(TAG, "Notify failed (len=%zu)", item.len);
                }
            }
        }
    }
}

} // anonymous namespace

namespace ecotiter::infrastructure::network
{

void startBleNotifyThread(BleManager& manager)
{
    std::thread t([&manager]() { bleNotifyLoop(&manager); });
    t.detach();
    ESP_LOGI(TAG, "Notify thread started");
}

} // namespace ecotiter::infrastructure::network
