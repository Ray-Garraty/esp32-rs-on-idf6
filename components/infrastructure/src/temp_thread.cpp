#include "infrastructure/temp_thread.hpp"

#include <cstdint>
#include <limits>
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "diag/ffi_guard.hpp"
#include "diag/stack_monitor.hpp"
#include "domain/types.hpp"
#include "infrastructure/config.hpp"
#include "infrastructure/drivers/adc.hpp"
#include "infrastructure/drivers/onewire.hpp"

static constexpr auto TAG = "temp_thread";

namespace
{

void run_temp_loop()
{
    ecotiter::diag::StackMonitor::instance().registerThread("temp",
                                                            ecotiter::domain::TEMP_THREAD_STACK);
    esp_task_wdt_add(NULL);

    ecotiter::infrastructure::drivers::OneWireBus bus(ecotiter::config::PIN_DS18B20);

    while (true)
    {
        esp_task_wdt_reset();
        {
            ecotiter::diag::FfiGuard guard(40);
            auto tempOpt = ecotiter::infrastructure::drivers::readSensor(bus);
            if (tempOpt.has_value())
            {
                int32_t cx100 = static_cast<int32_t>(tempOpt.value() * 100.0f);
                ecotiter::domain::gTempCX100.store(cx100, std::memory_order_release);
            }
            else
            {
                ecotiter::domain::gTempCX100.store(-99999, std::memory_order_release);
                static int failCount = 0;
                if (failCount++ % 60 == 0)
                    ESP_LOGW(TAG, "Temperature read failed, sentinel stored");
            }

            // Also sample ADC every iteration (1000ms)
            auto* adc = ecotiter::infrastructure::drivers::gAdcDriver;
            if (adc)
            {
                int16_t mv = adc->calibratedMv();
                if (mv < 0)
                    mv = 0;
                ecotiter::domain::gLastMv.store(static_cast<uint16_t>(mv),
                                                std::memory_order_release);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

} // anonymous namespace

extern "C" void tempTaskEntry(void* pvParameters)
{
    (void)pvParameters;
    run_temp_loop();
}
