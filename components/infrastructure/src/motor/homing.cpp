#include "internal.hpp"

#include <cstdint>
#include <cstdio>
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "infrastructure/config.hpp"
#include "infrastructure/drivers/stepper.hpp"
#include "infrastructure/drivers/limitswitch.hpp"
#include "domain/types.hpp"
#include "domain/errors.hpp"
#include "diag/ffi_guard.hpp"
#include "diag/rtc_watchdog.hpp"
#include "diag/state_tracer.hpp"

static constexpr auto TAG = "motor_task";

namespace ecotiter::infrastructure::motor {

using drivers::StepperMotor;
using drivers::LimitSwitch;
using domain::BuretteState;

static constexpr uint32_t HOME_INTERVAL_US = 1'000'000 / 1500;
static constexpr uint32_t HOMING_TIMEOUT_MS = 60000;

void run_homing(StepperMotor& stepper, LimitSwitch& fullSwitch) { // NOLINT(readability-function-cognitive-complexity) // reason: homing sequence with timeout + limit switch polling
    puts("DBG: run_homing START"); fflush(stdout);

    diag::FfiGuard guard(config::FFI_HOMING);

    ESP_LOGI("motor_task", "Starting homing sequence");

    auto oldState = domain::gBuretteState.load(std::memory_order_acquire);
    domain::gBuretteState.store(BuretteState::Homing, std::memory_order_release);
    diag::StateTracer::logBuretteTransition(
        domain::buretteStateStr(oldState), "homing");

    gpio_set_level(config::PIN_DIR, 1);

    fullSwitch.clear();
    domain::gStopFull.store(false, std::memory_order_release);

    static uint32_t intervals[config::RMT_CHUNK_SYMBOLS];
    for (size_t i = 0; i < config::RMT_CHUNK_SYMBOLS; ++i) {
        intervals[i] = HOME_INTERVAL_US;
    }

    uint32_t totalSent = 0;
    bool homed = false;
    auto startTime = xTaskGetTickCount();
    bool timedOut = false;

    while (!homed && !timedOut) {
        if (diag::gRtcWdt) {
            diag::gRtcWdt->feed();
        } else {
            puts("DBG: gRtcWdt IS NULL!"); fflush(stdout);
        }
        esp_task_wdt_reset();
        assert_rmt_preconditions();

        if (domain::gStopFull.load(std::memory_order_acquire)) {
            homed = true;
            break;
        }

        if ((xTaskGetTickCount() - startTime) >= pdMS_TO_TICKS(HOMING_TIMEOUT_MS)) {
            timedOut = true;
            break;
        }

        size_t chunk = config::RMT_CHUNK_SYMBOLS;

        auto result = stepper.moveStepsIntervals(
            {intervals, chunk},
            &domain::gStopFull);

        if (!result) {
            if (result.error() == domain::StepperError::LimitSwitchTriggered) {
                homed = true;
            } else {
                ESP_LOGE("motor_task", "Homing RMT error: %d",
                         static_cast<int>(result.error()));
                domain::gBuretteState.store(BuretteState::Error,
                                            std::memory_order_release);
                diag::StateTracer::logBuretteTransition(
                    "homing", "error");
                return;
            }
        }

        totalSent += static_cast<uint32_t>(chunk);

        if (domain::gStopFull.load(std::memory_order_acquire)) {
            homed = true;
        }

        if ((xTaskGetTickCount() - startTime) >= pdMS_TO_TICKS(HOMING_TIMEOUT_MS)) {
            timedOut = true;
        }
    }

    std::ignore = stepper.emergencyStop();
    std::ignore = stepper.enable();

    if (homed) {
        stepper.setCurrentPosition(domain::Steps{0});
        domain::gStopFull.store(false, std::memory_order_release);
        ESP_LOGI("motor_task", "Homing complete at step %lu (FULL limit)",
                 static_cast<unsigned long>(totalSent));
    } else if (timedOut) {
        ESP_LOGW("motor_task", "Homing timed out after %lu ms (%lu steps)",
                 static_cast<unsigned long>(HOMING_TIMEOUT_MS),
                 static_cast<unsigned long>(totalSent));
    }

    domain::gBuretteState.store(BuretteState::Idle, std::memory_order_release);
    diag::StateTracer::logBuretteTransition(
        "homing", "idle");
}

} // namespace ecotiter::infrastructure::motor
