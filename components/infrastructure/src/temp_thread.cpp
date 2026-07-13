#include "infrastructure/temp_thread.hpp"

#include <cstdint>
#include <limits>
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "infrastructure/config.hpp"
#include "infrastructure/drivers/onewire.hpp"
#include "domain/types.hpp"
#include "diag/stack_monitor.hpp"
#include "diag/ffi_guard.hpp"

static constexpr auto TAG = "temp_thread";

namespace {

void run_temp_loop() {
    ecotiter::diag::StackMonitor::instance().registerThread(
        "temp", ecotiter::domain::TEMP_THREAD_STACK);
    esp_task_wdt_add(NULL);

    ecotiter::infrastructure::drivers::OneWireBus bus(
        ecotiter::config::PIN_DS18B20);

    while (true) {
        esp_task_wdt_reset();
        {
            ecotiter::diag::FfiGuard guard(40);
            auto tempOpt = ecotiter::infrastructure::drivers::readSensor(bus);
            if (tempOpt.has_value()) {
                int32_t cx100 = static_cast<int32_t>(tempOpt.value() * 100.0f);
                ecotiter::domain::gTempCX100.store(cx100,
                    std::memory_order_release);
            } else {
                ecotiter::domain::gTempCX100.store(-99999,
                    std::memory_order_release);
                ESP_LOGW(TAG, "Temperature read failed, sentinel stored");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

} // anonymous namespace

extern "C" void tempTaskEntry(void* pvParameters) {
    (void)pvParameters;
    run_temp_loop();
}
