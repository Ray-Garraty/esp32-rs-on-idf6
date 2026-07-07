#include <cstdio>
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "diag/black_box.hpp"
#include "diag/stack_monitor.hpp"

static constexpr auto TAG = "main";

extern "C" void app_main(void) {
    nvs_flash_init();
    ecotiter::diag::BlackBox::instance().init();
    ecotiter::diag::StackMonitor::instance().registerMainTask();


    TickType_t last_wake = xTaskGetTickCount();
    constexpr TickType_t PACING_TICK = pdMS_TO_TICKS(10);

    while (true) {
        vTaskDelayUntil(&last_wake, PACING_TICK);
    }
}
