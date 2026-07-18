#include "internal.hpp"

#include <cstdint>
#include <cstdio>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "diag/rtc_watchdog.hpp"
#include "diag/state_tracer.hpp"
#include "domain/calibration.hpp"
#include "domain/errors.hpp"
#include "domain/memory.hpp"
#include "domain/types.hpp"
#include "infrastructure/config.hpp"
#include "infrastructure/drivers/stepper.hpp"
#include "infrastructure/drivers/valve.hpp"

// Declared in main/net_owner.hpp — motor task pushes WS broadcast without
// calling broadcastWsEvent() directly (net_owner drain loop owns the HTTP server)
extern QueueHandle_t gWsBroadcastQueue;

static constexpr auto TAG = "motor_task";

namespace ecotiter::infrastructure::motor
{

using domain::BuretteState;
using domain::Direction;
using domain::ValvePosition;
using drivers::StepperMotor;

void set_valve(ValvePosition pos)
{
    drivers::gValve.setPosition(pos);
    domain::gValvePosition.store(pos, std::memory_order_release);
}

uint32_t ml_min_to_hz(float speedMlMin)
{
    float speedCoeff = domain::CalibrationData::kDefaultSpeedCoeff;
    uint16_t minFreq = domain::CalibrationData::kDefaultMinFreqHz;
    uint16_t maxFreq = domain::CalibrationData::kDefaultMaxFreqHz;
    auto hz = domain::speedMlMinToHz(
        domain::MlMin{speedMlMin},
        {domain::CalibrationData::kDefaultStepsPerMl, 50.0f, speedCoeff, minFreq, maxFreq});
    return hz.value;
}

void move_to_endstop(StepperMotor& stepper, Direction dir, uint32_t speedHz,
                     std::atomic<bool>& stopFlag)
{
    gpio_set_level(config::PIN_DIR, (dir == Direction::LiqIn) ? 1 : 0);
    vTaskDelay(pdMS_TO_TICKS(config::DIR_SETUP_MS));

    uint32_t intervalUs = 1'000'000 / speedHz;
    static uint32_t intervals[config::RMT_CHUNK_SYMBOLS];
    for (auto& i : intervals)
        i = intervalUs;

    stopFlag.store(false, std::memory_order_release);

    while (true)
    {
        assert_rmt_preconditions();

        if (stopFlag.load(std::memory_order_acquire))
            break;

        auto result = stepper.moveStepsIntervals({intervals, config::RMT_CHUNK_SYMBOLS}, &stopFlag);
        if (!result)
        {
            if (result.error() == domain::StepperError::LimitSwitchTriggered)
                break;
            ESP_LOGE("motor_task", "move_to_endstop error: %d", static_cast<int>(result.error()));
            break;
        }
    }
    std::ignore = stepper.emergencyStop();
    std::ignore = stepper.enable();
}

void move_fill(StepperMotor& stepper, uint32_t speedHz)
{
    ESP_LOGI("motor_task", "fill: valve=INPUT, dir=LIQ_IN");
    set_valve(ValvePosition::Input);
    vTaskDelay(pdMS_TO_TICKS(config::VALVE_SETTLE_MS));
    domain::gBuretteState.store(BuretteState::Filling, std::memory_order_release);
    move_to_endstop(stepper, Direction::LiqIn, speedHz, domain::gStopFull);
}

void move_empty(StepperMotor& stepper, uint32_t speedHz)
{
    ESP_LOGI("motor_task", "empty: valve=OUTPUT, dir=LIQ_OUT");
    set_valve(ValvePosition::Output);
    vTaskDelay(pdMS_TO_TICKS(config::VALVE_SETTLE_MS));
    domain::gBuretteState.store(BuretteState::Emptying, std::memory_order_release);
    move_to_endstop(stepper, Direction::LiqOut, speedHz, domain::gStopEmpty);
}

void store_result(SmResult::Type type, int32_t stepsTaken, float measuredSpeed,
                  const float* results, int resultCount)
{
    SmResult sr{type, stepsTaken, measuredSpeed, {}, resultCount};
    for (int i = 0; i < resultCount && i < config::MAX_STORED_RESULTS; ++i)
    {
        sr.results[i] = results[i];
    }
    if (gSmResultQueue)
    {
        xQueueOverwrite(gSmResultQueue, &sr);
    }

    // Push WS broadcast for motor completion (non-blocking, best-effort)
    if (gWsBroadcastQueue)
    {
        struct WsBcEntry
        {
            char data[domain::memory::MAX_RSP_SIZE];
            size_t len;
        };
        WsBcEntry entry;
        int n = std::snprintf(entry.data, sizeof(entry.data),
                              R"({"event":"motor_complete","type":%d,"steps":%ld})",
                              static_cast<int>(type), static_cast<long>(stepsTaken));
        if (n > 0 && static_cast<size_t>(n) < sizeof(entry.data))
        {
            entry.len = static_cast<size_t>(n);
            xQueueSend(gWsBroadcastQueue, &entry, 0);
        }
    }

    domain::gBuretteState.store(BuretteState::Idle, std::memory_order_release);
}

void execute_move_steps(StepperMotor& stepper, int32_t steps)
{ // NOLINT(readability-function-cognitive-complexity) // reason: stepped move with stop flag + RMT
  // error handling
    if (steps <= 0)
        return;

    auto dir = domain::gDirection.load(std::memory_order_acquire);
    gpio_set_level(config::PIN_DIR, (dir == Direction::LiqIn) ? 1 : 0);
    vTaskDelay(pdMS_TO_TICKS(config::DIR_SETUP_MS));

    auto speed = domain::gSpeed.load(std::memory_order_acquire);
    if (speed < config::STEP_FREQ_MIN_HZ)
        speed = config::STEP_FREQ_MIN_HZ;
    if (speed > config::STEP_FREQ_MAX_HZ)
        speed = config::STEP_FREQ_MAX_HZ;

    uint32_t intervalUs = 1'000'000 / speed;

    static uint32_t intervals[config::RMT_CHUNK_SYMBOLS];
    for (size_t i = 0; i < config::RMT_CHUNK_SYMBOLS; ++i)
    {
        intervals[i] = intervalUs;
    }

    int32_t remaining = steps;
    while (remaining > 0)
    {
        if (diag::gRtcWdt)
            diag::gRtcWdt->feed();
        esp_task_wdt_reset();
        assert_rmt_preconditions();

        if (domain::gStopFull.load(std::memory_order_acquire))
        {
            ESP_LOGI("motor_task", "Move interrupted by stop flag");
            break;
        }

        size_t chunk = static_cast<size_t>(remaining);
        if (chunk > config::RMT_CHUNK_SYMBOLS)
            chunk = config::RMT_CHUNK_SYMBOLS;

        auto result = stepper.moveStepsIntervals({intervals, chunk}, &domain::gStopFull);

        if (!result)
        {
            if (result.error() == domain::StepperError::LimitSwitchTriggered)
            {
                ESP_LOGI("motor_task", "Limit switch triggered during move");
                domain::gBuretteState.store(BuretteState::Error, std::memory_order_release);
                diag::StateTracer::logBuretteTransition(
                    domain::buretteStateStr(domain::gBuretteState.load(std::memory_order_acquire)),
                    "error");
            }
            else
            {
                ESP_LOGE("motor_task", "Move RMT error: %d", static_cast<int>(result.error()));
            }
            break;
        }

        remaining -= static_cast<int32_t>(chunk);
    }
}

} // namespace ecotiter::infrastructure::motor
