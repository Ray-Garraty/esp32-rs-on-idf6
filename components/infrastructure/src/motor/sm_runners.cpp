#include "internal.hpp"

#include <cstdint>
#include <cstdio>
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "diag/black_box.hpp"
#include "diag/rtc_watchdog.hpp"
#include "diag/state_tracer.hpp"
#include "domain/cal_dose_sm.hpp"
#include "domain/cal_speed_sm.hpp"
#include "domain/calibration.hpp"
#include "domain/rinse_sm.hpp"
#include "domain/types.hpp"
#include "infrastructure/config.hpp"
#include "infrastructure/drivers/stepper.hpp"
#include "infrastructure/drivers/tmc_uart.hpp"

static constexpr auto TAG = "motor_task";

// BlackBox event payloadId for rinse SM errors
static constexpr uint16_t FFI_RINSE_ERROR_ID = 0x10;

namespace ecotiter::infrastructure::motor
{

using domain::BuretteState;
using domain::ValvePosition;
using drivers::StepperMotor;

static domain::sm::RinseSm s_rinseSm;
static domain::sm::CalDoseSm s_calDoseSm;
static domain::sm::CalSpeedSingleSm s_calSpeedSm;
static domain::sm::CalSpeedSeqSm s_calSpeedSeqSm;

namespace
{

void doRinseFill(StepperMotor& stepper, uint32_t speedHz, const domain::sm::RinseSm& sm)
{
    diag::StateTracer::logBuretteTransition(
        domain::buretteStateStr(domain::gBuretteState.load(std::memory_order_acquire)), "filling");
    ESP_LOGI("motor_task", "rinse: fill (cycle %u/%u)", sm.currentCycle, sm.totalCycles);
    move_fill(stepper, speedHz);
}

void doRinseEmpty(StepperMotor& stepper, uint32_t speedHz, const domain::sm::RinseSm& sm)
{
    diag::StateTracer::logBuretteTransition(
        domain::buretteStateStr(domain::gBuretteState.load(std::memory_order_acquire)), "emptying");
    ESP_LOGI("motor_task", "rinse: empty (cycle %u/%u)", sm.currentCycle, sm.totalCycles);
    move_empty(stepper, speedHz);
}

void doRinseComplete()
{
    diag::StateTracer::logBuretteTransition(
        domain::buretteStateStr(domain::gBuretteState.load(std::memory_order_acquire)), "idle");
    ESP_LOGI("motor_task", "rinse: complete");
    store_result(SmResult::Type::RinseComplete);
}

void doRinseError()
{
    diag::StateTracer::logBuretteTransition(
        domain::buretteStateStr(domain::gBuretteState.load(std::memory_order_acquire)), "error");
    diag::BlackBox::instance().record({.timestampUs = static_cast<uint64_t>(
                                           xTaskGetTickCount() * portTICK_PERIOD_MS * 1000),
                                       .type = diag::BlackBox::EventType::Error,
                                       .threadId = 1,
                                       .payloadId = FFI_RINSE_ERROR_ID,
                                       .payloadValue = 0});
    ESP_LOGE("motor_task", "rinse: SM error");
    store_result(SmResult::Type::Error);
}

} // anonymous namespace

void run_rinse_sm(StepperMotor& stepper,
                  float speedMlMin, uint8_t cycles, float currentVolumeMl, float nominalVolumeMl)
{
    s_rinseSm.start(cycles, currentVolumeMl, nominalVolumeMl);

    uint32_t speedHz = ml_min_to_hz(speedMlMin);

    if (s_rinseSm.phase == domain::sm::RinseSm::Phase::PreFill)
    {
        ESP_LOGI("motor_task", "rinse: initial fill");
        move_fill(stepper, speedHz);
    }

    while (!s_rinseSm.isComplete())
    {
        if (diag::gRtcWdt)
            diag::gRtcWdt->feed();
        esp_task_wdt_reset();
        auto action = s_rinseSm.onMotorComplete(domain::gVolumeMl.load(std::memory_order_acquire),
                                                nominalVolumeMl);

        switch (action)
        {
        case domain::sm::RinseAction::FillToLimit:
            doRinseFill(stepper, speedHz, s_rinseSm);
            break;
        case domain::sm::RinseAction::EmptyToLimit:
            doRinseEmpty(stepper, speedHz, s_rinseSm);
            break;
        case domain::sm::RinseAction::Complete:
            doRinseComplete();
            return;
        case domain::sm::RinseAction::Error:
            doRinseError();
            return;
        }
    }
}

void run_cal_dose_sm(StepperMotor& stepper,
                     float speedMlMin, float currentVolumeMl, float nominalVolumeMl)
{
    s_calDoseSm.start();
    uint32_t speedHz = ml_min_to_hz(speedMlMin);

    auto action = s_calDoseSm.onStart(currentVolumeMl, nominalVolumeMl);

    if (action == domain::sm::CalDoseAction::FillToLimit)
    {
        diag::StateTracer::logBuretteTransition(
            domain::buretteStateStr(domain::gBuretteState.load(std::memory_order_acquire)),
            "filling");
        ESP_LOGI("motor_task", "cal_dose: fill");
        move_fill(stepper, speedHz);
        action = s_calDoseSm.onFillComplete(stepper.position().value);
    }

    if (action == domain::sm::CalDoseAction::EmptyToLimit)
    {
        diag::StateTracer::logBuretteTransition(
            domain::buretteStateStr(domain::gBuretteState.load(std::memory_order_acquire)),
            "emptying");
        ESP_LOGI("motor_task", "cal_dose: empty");
        move_empty(stepper, speedHz);
        action = s_calDoseSm.onEmptyComplete(stepper.position().value);
    }

    if (action == domain::sm::CalDoseAction::Complete)
    {
        diag::StateTracer::logBuretteTransition(
            domain::buretteStateStr(domain::gBuretteState.load(std::memory_order_acquire)), "idle");
        ESP_LOGI("motor_task", "cal_dose: complete, steps=%ld",
                 static_cast<long>(s_calDoseSm.stepsTaken));
        store_result(SmResult::Type::CalDoseComplete, s_calDoseSm.stepsTaken);
    }
    else
    {
        diag::StateTracer::logBuretteTransition(
            domain::buretteStateStr(domain::gBuretteState.load(std::memory_order_acquire)),
            "error");
        store_result(SmResult::Type::Error);
    }
}

void run_cal_speed_sm(StepperMotor& stepper,
                      float speedMlMin, uint16_t testFreqHz, float currentVolumeMl,
                      float nominalVolumeMl)
{
    s_calSpeedSm.start();
    uint32_t speedHz = ml_min_to_hz(speedMlMin);

    auto action = s_calSpeedSm.onStart(currentVolumeMl, nominalVolumeMl);

    if (action == domain::sm::CalSpeedAction::FillToLimit)
    {
        diag::StateTracer::logBuretteTransition(
            domain::buretteStateStr(domain::gBuretteState.load(std::memory_order_acquire)),
            "filling");
        ESP_LOGI("motor_task", "cal_speed: fill");
        move_fill(stepper, speedHz);
        auto now = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
        action = s_calSpeedSm.onFillComplete(now);
    }

    if (action == domain::sm::CalSpeedAction::EmptyToLimit)
    {
        diag::StateTracer::logBuretteTransition(
            domain::buretteStateStr(domain::gBuretteState.load(std::memory_order_acquire)),
            "emptying");
        uint32_t hz = testFreqHz;
        if (hz < ml_min_to_hz(config::MIN_CAL_SPEED_ML_MIN))
            hz = ml_min_to_hz(config::MIN_CAL_SPEED_ML_MIN);
        ESP_LOGI("motor_task", "cal_speed: empty at %lu Hz", static_cast<unsigned long>(hz));
        move_empty(stepper, hz);
        auto now = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
        action = s_calSpeedSm.onEmptyComplete(now, nominalVolumeMl);
    }

    if (action == domain::sm::CalSpeedAction::Complete)
    {
        diag::StateTracer::logBuretteTransition(
            domain::buretteStateStr(domain::gBuretteState.load(std::memory_order_acquire)), "idle");
        ESP_LOGI("motor_task", "cal_speed: complete, speed=%.2f ml/min",
                 static_cast<double>(s_calSpeedSm.measuredSpeedMlMin));
        store_result(SmResult::Type::CalSpeedComplete, 0, s_calSpeedSm.measuredSpeedMlMin);
    }
    else
    {
        diag::StateTracer::logBuretteTransition(
            domain::buretteStateStr(domain::gBuretteState.load(std::memory_order_acquire)),
            "error");
        store_result(SmResult::Type::Error);
    }
}

namespace
{

/// Handle a CalSpeedSeq SM action and return true if the loop should continue.
bool dispatchCalSpeedAction(StepperMotor& stepper, domain::sm::CalSpeedAction action,
                            uint32_t fillHz)
{
    switch (action)
    {
    case domain::sm::CalSpeedAction::FillToLimit:
        diag::StateTracer::logBuretteTransition(
            domain::buretteStateStr(domain::gBuretteState.load(std::memory_order_acquire)),
            "filling");
        ESP_LOGI("motor_task", "cal_speed_seq: fill (point %d/3)", s_calSpeedSeqSm.seqIdx + 1);
        move_fill(stepper, fillHz);
        return true;

    case domain::sm::CalSpeedAction::EmptyToLimit: {
        diag::StateTracer::logBuretteTransition(
            domain::buretteStateStr(domain::gBuretteState.load(std::memory_order_acquire)),
            "emptying");
        int idx = s_calSpeedSeqSm.seqIdx;
        uint16_t f = s_calSpeedSeqSm.freqs[idx];
        ESP_LOGI("motor_task", "cal_speed_seq: empty at %u Hz (point %d/3)",
                 static_cast<unsigned>(f), idx + 1);
        move_empty(stepper, f);
        return true;
    }

    case domain::sm::CalSpeedAction::SettleValve:
        vTaskDelay(pdMS_TO_TICKS(config::VALVE_SETTLE_MS));
        return true;

    case domain::sm::CalSpeedAction::Complete:
        diag::StateTracer::logBuretteTransition(
            domain::buretteStateStr(domain::gBuretteState.load(std::memory_order_acquire)), "idle");
        ESP_LOGI("motor_task", "cal_speed_seq: complete");
        store_result(SmResult::Type::CalSpeedSeqComplete, 0, 0.0f, s_calSpeedSeqSm.results,
                     s_calSpeedSeqSm.seqIdx);
        return false;

    case domain::sm::CalSpeedAction::Error:
        diag::StateTracer::logBuretteTransition(
            domain::buretteStateStr(domain::gBuretteState.load(std::memory_order_acquire)), "error");
        ESP_LOGE("motor_task", "cal_speed_seq: SM error");
        store_result(SmResult::Type::Error);
        return false;
    }
    return false;
}

} // anonymous namespace

void run_cal_speed_seq_sm(
    StepperMotor& stepper, const uint16_t freqs[3], float fillSpeedMlMin, float currentVolumeMl,
    float nominalVolumeMl)
{
    s_calSpeedSeqSm.start(freqs);
    uint32_t fillHz = ml_min_to_hz(fillSpeedMlMin);

    if (currentVolumeMl >= nominalVolumeMl - 0.1f)
    {
        s_calSpeedSeqSm.firstEver = false;
    }

    while (!s_calSpeedSeqSm.isComplete())
    {
        if (diag::gRtcWdt)
            diag::gRtcWdt->feed();
        esp_task_wdt_reset();
        auto now = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
        auto action = s_calSpeedSeqSm.onTick(now);

        if (!dispatchCalSpeedAction(stepper, action, fillHz))
            return;
    }
}

} // namespace ecotiter::infrastructure::motor
