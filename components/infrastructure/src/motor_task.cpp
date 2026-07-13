#include "infrastructure/motor_task.hpp"

#include <cstdint>
#include <cstdio>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "infrastructure/config.hpp"
#include "infrastructure/drivers/stepper.hpp"
#include "infrastructure/drivers/tmc_uart.hpp"
#include "infrastructure/drivers/limitswitch.hpp"
#include "domain/types.hpp"
#include "domain/errors.hpp"
#include "domain/calibration.hpp"
#include "domain/rinse_sm.hpp"
#include "domain/cal_dose_sm.hpp"
#include "domain/cal_speed_sm.hpp"
#include "diag/black_box.hpp"
#include "diag/ffi_guard.hpp"
#include "diag/rtc_watchdog.hpp"
#include "diag/stack_monitor.hpp"
#include "diag/state_tracer.hpp"

static constexpr auto TAG = "motor_task";

namespace ecotiter::infrastructure {

QueueHandle_t gMotorCmdQueue = nullptr;
SmResult gSmResult{SmResult::Type::None, 0, 0.0f, {}, 0};
drivers::TmcUart gTmcUart;

namespace {

using drivers::StepperMotor;
using drivers::LimitSwitch;
using domain::Direction;
using domain::BuretteState;
using domain::ValvePosition;

static constexpr size_t INTERVAL_CHUNK = 128;
static constexpr uint32_t HOME_SPEED_HZ = 1500;
static constexpr uint32_t HOME_INTERVAL_US = 1'000'000 / HOME_SPEED_HZ;
static constexpr uint32_t HOMING_TIMEOUT_MS = 120000;

static domain::sm::RinseSm s_rinseSm;
static domain::sm::CalDoseSm s_calDoseSm;
static domain::sm::CalSpeedSingleSm s_calSpeedSm;
static domain::sm::CalSpeedSeqSm s_calSpeedSeqSm;

void assert_rmt_preconditions() {
}

const char* buretteStateName(BuretteState s) {
    switch (s) {
        case BuretteState::Idle:      return "idle";
        case BuretteState::Homing:    return "homing";
        case BuretteState::Filling:   return "filling";
        case BuretteState::Emptying:  return "emptying";
        case BuretteState::Dosing:    return "dosing";
        case BuretteState::Rinsing:   return "rinsing";
        case BuretteState::Stopping:  return "stopping";
        case BuretteState::Error:     return "error";
    }
    return "unknown";
}

void set_valve(ValvePosition pos) {
    gpio_set_level(config::PIN_VALVE, (pos == ValvePosition::Output) ? 1 : 0);
    domain::gValvePosition.store(pos, std::memory_order_release);
}

uint32_t ml_min_to_hz(float speedMlMin) {
    float speedCoeff = domain::CalibrationData::kDefaultSpeedCoeff;
    uint16_t minFreq = domain::CalibrationData::kDefaultMinFreqHz;
    uint16_t maxFreq = domain::CalibrationData::kDefaultMaxFreqHz;
    auto hz = domain::speedMlMinToHz(domain::MlMin{speedMlMin},
        {domain::CalibrationData::kDefaultStepsPerMl, 50.0f,
         speedCoeff, minFreq, maxFreq});
    return hz.value;
}

void move_to_endstop(StepperMotor& stepper, Direction dir,
                     uint32_t speedHz, std::atomic<bool>& stopFlag) {
    gpio_set_level(config::PIN_DIR, (dir == Direction::LiqIn) ? 1 : 0);
    vTaskDelay(pdMS_TO_TICKS(1));

    uint32_t intervalUs = 1'000'000 / speedHz;
    uint32_t intervals[INTERVAL_CHUNK];
    for (auto& i : intervals) i = intervalUs;

    stopFlag.store(false, std::memory_order_release);

    while (true) {
        assert_rmt_preconditions();

        if (stopFlag.load(std::memory_order_acquire)) break;

        auto result = stepper.moveStepsIntervals(
            {intervals, INTERVAL_CHUNK}, &stopFlag);
        if (!result) {
            if (result.error() == domain::StepperError::LimitSwitchTriggered) break;
            ESP_LOGE(TAG, "move_to_endstop error: %d",
                     static_cast<int>(result.error()));
            break;
        }
    }
    std::ignore = stepper.emergencyStop();
    std::ignore = stepper.enable();
}

void move_fill(StepperMotor& stepper, uint32_t speedHz) {
    ESP_LOGI(TAG, "fill: valve=INPUT, dir=LIQ_IN");
    set_valve(ValvePosition::Input);
    domain::gBuretteState.store(BuretteState::Filling, std::memory_order_release);
    move_to_endstop(stepper, Direction::LiqIn, speedHz, domain::gStopFull);
}

void move_empty(StepperMotor& stepper, uint32_t speedHz) {
    ESP_LOGI(TAG, "empty: valve=OUTPUT, dir=LIQ_OUT");
    set_valve(ValvePosition::Output);
    domain::gBuretteState.store(BuretteState::Emptying, std::memory_order_release);
    move_to_endstop(stepper, Direction::LiqOut, speedHz, domain::gStopEmpty);
}

void store_result(SmResult::Type type, int32_t stepsTaken = 0,
                  float measuredSpeed = 0.0f,
                  const float* results = nullptr, int resultCount = 0) {
    gSmResult.type = type;
    gSmResult.stepsTaken = stepsTaken;
    gSmResult.measuredSpeedMlMin = measuredSpeed;
    gSmResult.resultCount = resultCount;
    for (int i = 0; i < resultCount && i < 3; i++) {
        gSmResult.results[i] = results[i];
    }
    domain::gBuretteState.store(BuretteState::Idle, std::memory_order_release);
    domain::gHasPendingResult.store(true, std::memory_order_release);
}

void run_rinse_sm(StepperMotor& stepper,
                  float speedMlMin, uint8_t cycles,
                  float currentVolumeMl, float nominalVolumeMl) {
    s_rinseSm.start(cycles, currentVolumeMl, nominalVolumeMl);

    uint32_t speedHz = ml_min_to_hz(speedMlMin);

    if (s_rinseSm.phase == domain::sm::RinseSm::Phase::PreFill) {
        ESP_LOGI(TAG, "rinse: initial fill");
        move_fill(stepper, speedHz);
    }

    while (!s_rinseSm.isComplete()) {
        if (diag::gRtcWdt) diag::gRtcWdt->feed();
        esp_task_wdt_reset();
        auto action = s_rinseSm.onMotorComplete(
            domain::gVolumeMl.load(std::memory_order_acquire),
            nominalVolumeMl);

        switch (action) {
            case domain::sm::RinseAction::FillToLimit:
                diag::StateTracer::logBuretteTransition(
                    buretteStateName(domain::gBuretteState.load(std::memory_order_acquire)),
                    "filling");
                ESP_LOGI(TAG, "rinse: fill (cycle %u/%u)",
                         s_rinseSm.currentCycle, s_rinseSm.totalCycles);
                move_fill(stepper, speedHz);
                break;
            case domain::sm::RinseAction::EmptyToLimit:
                diag::StateTracer::logBuretteTransition(
                    buretteStateName(domain::gBuretteState.load(std::memory_order_acquire)),
                    "emptying");
                ESP_LOGI(TAG, "rinse: empty (cycle %u/%u)",
                         s_rinseSm.currentCycle, s_rinseSm.totalCycles);
                move_empty(stepper, speedHz);
                break;
            case domain::sm::RinseAction::Complete:
                diag::StateTracer::logBuretteTransition(
                    buretteStateName(domain::gBuretteState.load(std::memory_order_acquire)),
                    "idle");
                ESP_LOGI(TAG, "rinse: complete");
                store_result(SmResult::Type::RinseComplete);
                return;
            case domain::sm::RinseAction::Error:
                diag::StateTracer::logBuretteTransition(
                    buretteStateName(domain::gBuretteState.load(std::memory_order_acquire)),
                    "error");
                diag::BlackBox::instance().record({
                    .timestampUs = static_cast<uint64_t>(xTaskGetTickCount() * portTICK_PERIOD_MS * 1000),
                    .type = diag::BlackBox::EventType::Error,
                    .threadId = 1,
                    .payloadId = 0x10,
                    .payloadValue = 0
                });
                ESP_LOGE(TAG, "rinse: SM error");
                store_result(SmResult::Type::Error);
                return;
        }
    }
}

void run_cal_dose_sm(StepperMotor& stepper,
                     float speedMlMin,
                     float currentVolumeMl, float nominalVolumeMl) {
    s_calDoseSm.start();
    uint32_t speedHz = ml_min_to_hz(speedMlMin);

    auto action = s_calDoseSm.onStart(currentVolumeMl, nominalVolumeMl);

    if (action == domain::sm::CalDoseAction::FillToLimit) {
        diag::StateTracer::logBuretteTransition(
            buretteStateName(domain::gBuretteState.load(std::memory_order_acquire)),
            "filling");
        ESP_LOGI(TAG, "cal_dose: fill");
        move_fill(stepper, speedHz);
        action = s_calDoseSm.onFillComplete(
            stepper.position().value);
    }

    if (action == domain::sm::CalDoseAction::EmptyToLimit) {
        diag::StateTracer::logBuretteTransition(
            buretteStateName(domain::gBuretteState.load(std::memory_order_acquire)),
            "emptying");
        ESP_LOGI(TAG, "cal_dose: empty");
        move_empty(stepper, speedHz);
        action = s_calDoseSm.onEmptyComplete(
            stepper.position().value);
    }

    if (action == domain::sm::CalDoseAction::Complete) {
        diag::StateTracer::logBuretteTransition(
            buretteStateName(domain::gBuretteState.load(std::memory_order_acquire)),
            "idle");
        ESP_LOGI(TAG, "cal_dose: complete, steps=%ld",
                 static_cast<long>(s_calDoseSm.stepsTaken));
        store_result(SmResult::Type::CalDoseComplete,
                     s_calDoseSm.stepsTaken);
    } else {
        diag::StateTracer::logBuretteTransition(
            buretteStateName(domain::gBuretteState.load(std::memory_order_acquire)),
            "error");
        store_result(SmResult::Type::Error);
    }
}

void run_cal_speed_sm(StepperMotor& stepper,
                      float speedMlMin, uint16_t testFreqHz,
                      float currentVolumeMl, float nominalVolumeMl) {
    s_calSpeedSm.start();
    uint32_t speedHz = ml_min_to_hz(speedMlMin);

    auto action = s_calSpeedSm.onStart(currentVolumeMl, nominalVolumeMl);

    if (action == domain::sm::CalSpeedAction::FillToLimit) {
        diag::StateTracer::logBuretteTransition(
            buretteStateName(domain::gBuretteState.load(std::memory_order_acquire)),
            "filling");
        ESP_LOGI(TAG, "cal_speed: fill");
        move_fill(stepper, speedHz);
        auto now = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
        action = s_calSpeedSm.onFillComplete(now);
    }

    if (action == domain::sm::CalSpeedAction::EmptyToLimit) {
        diag::StateTracer::logBuretteTransition(
            buretteStateName(domain::gBuretteState.load(std::memory_order_acquire)),
            "emptying");
        uint32_t hz = testFreqHz;
        if (hz < ml_min_to_hz(15.0f)) hz = ml_min_to_hz(15.0f);
        ESP_LOGI(TAG, "cal_speed: empty at %lu Hz",
                 static_cast<unsigned long>(hz));
        move_empty(stepper, hz);
        auto now = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
        action = s_calSpeedSm.onEmptyComplete(now, nominalVolumeMl);
    }

    if (action == domain::sm::CalSpeedAction::Complete) {
        diag::StateTracer::logBuretteTransition(
            buretteStateName(domain::gBuretteState.load(std::memory_order_acquire)),
            "idle");
        ESP_LOGI(TAG, "cal_speed: complete, speed=%.2f ml/min",
                 static_cast<double>(s_calSpeedSm.measuredSpeedMlMin));
        store_result(SmResult::Type::CalSpeedComplete,
                     0, s_calSpeedSm.measuredSpeedMlMin);
    } else {
        diag::StateTracer::logBuretteTransition(
            buretteStateName(domain::gBuretteState.load(std::memory_order_acquire)),
            "error");
        store_result(SmResult::Type::Error);
    }
}

void run_cal_speed_seq_sm(StepperMotor& stepper,
                          const uint16_t freqs[3],
                          float fillSpeedMlMin,
                          float currentVolumeMl, float nominalVolumeMl) {
    s_calSpeedSeqSm.start(freqs);
    uint32_t fillHz = ml_min_to_hz(fillSpeedMlMin);

    if (currentVolumeMl >= nominalVolumeMl - 0.1f) {
        s_calSpeedSeqSm.firstEver = false;
    }

    while (!s_calSpeedSeqSm.isComplete()) {
        if (diag::gRtcWdt) diag::gRtcWdt->feed();
        esp_task_wdt_reset();
        auto now = static_cast<uint32_t>(
            xTaskGetTickCount() * portTICK_PERIOD_MS);
        auto action = s_calSpeedSeqSm.onTick(now);

        switch (action) {
            case domain::sm::CalSpeedAction::FillToLimit:
                diag::StateTracer::logBuretteTransition(
                    buretteStateName(domain::gBuretteState.load(std::memory_order_acquire)),
                    "filling");
                ESP_LOGI(TAG, "cal_speed_seq: fill (point %d/3)",
                         s_calSpeedSeqSm.seqIdx + 1);
                move_fill(stepper, fillHz);
                break;

            case domain::sm::CalSpeedAction::EmptyToLimit: {
                diag::StateTracer::logBuretteTransition(
                    buretteStateName(domain::gBuretteState.load(std::memory_order_acquire)),
                    "emptying");
                int idx = s_calSpeedSeqSm.seqIdx;
                uint16_t f = s_calSpeedSeqSm.freqs[idx];
                ESP_LOGI(TAG, "cal_speed_seq: empty at %u Hz (point %d/3)",
                         static_cast<unsigned>(f), idx + 1);
                move_empty(stepper, f);
                break;
            }

            case domain::sm::CalSpeedAction::SettleValve:
                vTaskDelay(pdMS_TO_TICKS(50));
                break;

            case domain::sm::CalSpeedAction::Complete:
                diag::StateTracer::logBuretteTransition(
                    buretteStateName(domain::gBuretteState.load(std::memory_order_acquire)),
                    "idle");
                ESP_LOGI(TAG, "cal_speed_seq: complete");
                store_result(SmResult::Type::CalSpeedSeqComplete,
                             0, 0.0f,
                             s_calSpeedSeqSm.results,
                             s_calSpeedSeqSm.seqIdx);
                return;

            case domain::sm::CalSpeedAction::Error:
                diag::StateTracer::logBuretteTransition(
                    buretteStateName(domain::gBuretteState.load(std::memory_order_acquire)),
                    "error");
                ESP_LOGE(TAG, "cal_speed_seq: SM error");
                store_result(SmResult::Type::Error);
                return;
        }
    }
}

void run_homing(StepperMotor& stepper, LimitSwitch& fullSwitch) {
    puts("DBG: run_homing START"); fflush(stdout);

    diag::FfiGuard guard(30);

    ESP_LOGI(TAG, "Starting homing sequence");

    auto oldState = domain::gBuretteState.load(std::memory_order_acquire);
    domain::gBuretteState.store(BuretteState::Homing, std::memory_order_release);
    diag::StateTracer::logBuretteTransition(
        buretteStateName(oldState), "homing");

    gpio_set_level(config::PIN_DIR, 1);

    fullSwitch.clear();
    domain::gStopFull.store(false, std::memory_order_release);

    uint32_t intervals[INTERVAL_CHUNK];
    for (size_t i = 0; i < INTERVAL_CHUNK; ++i) {
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

        size_t chunk = INTERVAL_CHUNK;

        auto result = stepper.moveStepsIntervals(
            {intervals, chunk},
            &domain::gStopFull);

        if (!result) {
            if (result.error() == domain::StepperError::LimitSwitchTriggered) {
                homed = true;
            } else {
                ESP_LOGE(TAG, "Homing RMT error: %d",
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
        ESP_LOGI(TAG, "Homing complete at step %lu (FULL limit)",
                 static_cast<unsigned long>(totalSent));
    } else if (timedOut) {
        ESP_LOGW(TAG, "Homing timed out after %lu ms (%lu steps)",
                 static_cast<unsigned long>(HOMING_TIMEOUT_MS),
                 static_cast<unsigned long>(totalSent));
    }

    domain::gBuretteState.store(BuretteState::Idle, std::memory_order_release);
    diag::StateTracer::logBuretteTransition(
        "homing", "idle");
}

void execute_move_steps(StepperMotor& stepper, int32_t steps) {
    if (steps <= 0) return;

    auto dir = domain::gDirection.load(std::memory_order_acquire);
    gpio_set_level(config::PIN_DIR, (dir == Direction::LiqIn) ? 1 : 0);
    vTaskDelay(pdMS_TO_TICKS(1));

    auto speed = domain::gSpeed.load(std::memory_order_acquire);
    if (speed < config::STEP_FREQ_MIN_HZ) speed = config::STEP_FREQ_MIN_HZ;
    if (speed > config::STEP_FREQ_MAX_HZ) speed = config::STEP_FREQ_MAX_HZ;

    uint32_t intervalUs = 1'000'000 / speed;

    uint32_t intervals[INTERVAL_CHUNK];
    for (size_t i = 0; i < INTERVAL_CHUNK; ++i) {
        intervals[i] = intervalUs;
    }

    int32_t remaining = steps;
    while (remaining > 0) {
        if (diag::gRtcWdt) diag::gRtcWdt->feed();
        esp_task_wdt_reset();
        assert_rmt_preconditions();

        if (domain::gStopFull.load(std::memory_order_acquire)) {
            ESP_LOGI(TAG, "Move interrupted by stop flag");
            break;
        }

        size_t chunk = static_cast<size_t>(remaining);
        if (chunk > INTERVAL_CHUNK) chunk = INTERVAL_CHUNK;

        auto result = stepper.moveStepsIntervals(
            {intervals, chunk},
            &domain::gStopFull);

        if (!result) {
            if (result.error() == domain::StepperError::LimitSwitchTriggered) {
                ESP_LOGI(TAG, "Limit switch triggered during move");
                domain::gBuretteState.store(BuretteState::Error,
                                            std::memory_order_release);
                diag::StateTracer::logBuretteTransition(
                    buretteStateName(
                        domain::gBuretteState.load(std::memory_order_acquire)),
                    "error");
            } else {
                ESP_LOGE(TAG, "Move RMT error: %d",
                         static_cast<int>(result.error()));
            }
            break;
        }

        remaining -= static_cast<int32_t>(chunk);
    }
}

} // anonymous namespace

extern "C" void motorTaskEntry(void* pvParameters) {
    (void)pvParameters;

    puts("DBG: motorEntry START"); fflush(stdout);

    diag::StackMonitor::instance().registerThread("motor", domain::MOTOR_THREAD_STACK);

    {
        auto largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "motor task DRAM before queue: largest_free=%lu",
                 (unsigned long)largest);
        if (largest < 8192) {
            ESP_LOGW(TAG, "Low DRAM for motor queue! largest_free=%lu",
                     (unsigned long)largest);
        }
    }

    gMotorCmdQueue = xQueueCreate(4, sizeof(MotorCommand));
    if (gMotorCmdQueue == nullptr) {
        ESP_LOGE(TAG, "Failed to create motor command queue");
        vTaskDelete(nullptr);
        return;
    }
    puts("DBG: motor queue created"); fflush(stdout);
    esp_task_wdt_add(NULL);

    puts("DBG: before StepperMotor ctor (gpio 21,5,27)"); fflush(stdout);
    StepperMotor stepper(config::PIN_STEP, config::PIN_DIR, config::PIN_EN);
    puts("DBG: after StepperMotor ctor"); fflush(stdout);

    puts("DBG: before LimitSwitch emptySwitch ctor (gpio 15)"); fflush(stdout);
    LimitSwitch emptySwitch(config::PIN_LIMIT_EMPTY,
                            domain::gStopEmpty, false);
    puts("DBG: after LimitSwitch emptySwitch ctor"); fflush(stdout);

    puts("DBG: before LimitSwitch fullSwitch ctor (gpio 7)"); fflush(stdout);
    LimitSwitch fullSwitch(config::PIN_LIMIT_FULL,
                           domain::gStopFull, false);
    puts("DBG: after LimitSwitch fullSwitch ctor"); fflush(stdout);

    // Initialize TMC2209 UART
    puts("DBG: before tmc uart init"); fflush(stdout);
    bool tmcOk = gTmcUart.init(config::PIN_TMC_UART_TX, config::PIN_TMC_UART_RX,
                               config::TMC_UART_BAUD);
    if (tmcOk && gTmcUart.testConnection()) {
        ESP_LOGI(TAG, "TMC2209 UART connected — configuring registers");

        // GCONF: pdn_disable=1, I_scale_analog=1, en_spreadCycle=0
        std::ignore = gTmcUart.writeRegister(drivers::TMC_REG_GCONF, 0x00000041);

        // CHOPCONF: toff=4, tbl=1, mres=4 (16 µsteps)
        std::ignore = gTmcUart.writeRegister(drivers::TMC_REG_CHOPCONF, 0x00002104);

        // COOLCONF: semin=5, semax=2, sedn=1
        std::ignore = gTmcUart.writeRegister(drivers::TMC_REG_COOLCONF, 0x00002205);

        // TCOOLTHRS: upper CoolStep threshold (20-bit)
        std::ignore = gTmcUart.writeRegister(drivers::TMC_REG_TCOOLTHRS, 0x000FFFFF);

        // PWMCONF: pwm_autoscale=1, pwm_autograd=1, pwm_freq=1
        std::ignore = gTmcUart.writeRegister(drivers::TMC_REG_PWMCONF, 0x00010014);

        // StallGuard threshold (runtime-configurable via NVS/WebUI)
        uint8_t sg = domain::gStallGuardThreshold.load(std::memory_order_acquire);
        std::ignore = gTmcUart.writeRegister(drivers::TMC_REG_SGTHRS, sg);
    } else {
        ESP_LOGW(TAG, "TMC2209 UART not connected, running without register config");
    }

    puts("DBG: before run_homing"); fflush(stdout);
    run_homing(stepper, fullSwitch);
    domain::gHomingDone.store(true, std::memory_order_release);

    gSmResult = {SmResult::Type::None, 0, 0.0f, {}, 0};

    MotorCommand cmd;
    while (true) {
        esp_task_wdt_reset();
        if (xQueueReceive(gMotorCmdQueue, &cmd, pdMS_TO_TICKS(100))) {
            switch (cmd.type) {

            case MotorCommandType::MoveSteps: {
                int32_t stepsBefore = stepper.position().value;
                execute_move_steps(stepper, cmd.steps);
                int32_t stepsTaken = static_cast<int32_t>(
                    stepper.position().value) - stepsBefore;
                if (stepsTaken < 0) stepsTaken = -stepsTaken;
                gSmResult.type = SmResult::Type::None; // reuse None as generic "done"
                gSmResult.stepsTaken = stepsTaken;
                domain::gHasPendingResult.store(true, std::memory_order_release);
                break;
            }

            case MotorCommandType::Stop: {
                ESP_LOGI(TAG, "Stop requested");
                domain::gBuretteState.store(BuretteState::Stopping,
                                            std::memory_order_release);
                domain::gStopFull.store(true, std::memory_order_release);
                vTaskDelay(pdMS_TO_TICKS(50));
                domain::gStopFull.store(false, std::memory_order_release);
                domain::gBuretteState.store(BuretteState::Idle,
                                            std::memory_order_release);
                break;
            }

            case MotorCommandType::EmergencyStop: {
                ESP_LOGW(TAG, "EMERGENCY STOP");
                domain::gStopFull.store(true, std::memory_order_release);
                domain::gStopEmpty.store(true, std::memory_order_release);
                domain::gBuretteState.store(BuretteState::Error,
                                            std::memory_order_release);
                diag::StateTracer::logBuretteTransition(
                    buretteStateName(
                        domain::gBuretteState.load(std::memory_order_acquire)),
                    "error");
                break;
            }

            case MotorCommandType::Home:
                run_homing(stepper, emptySwitch);
                break;

            case MotorCommandType::SetDirection: {
                auto dir = cmd.direction;
                domain::gDirection.store(dir, std::memory_order_release);
                gpio_set_level(config::PIN_DIR,
                                (dir == Direction::LiqIn) ? 1 : 0);
                break;
            }

            case MotorCommandType::SetSpeed:
                if (cmd.speedHz >= config::STEP_FREQ_MIN_HZ &&
                    cmd.speedHz <= config::STEP_FREQ_MAX_HZ) {
                    domain::gSpeed.store(cmd.speedHz, std::memory_order_release);
                }
                break;

            case MotorCommandType::SetAccel:
                domain::gAccel.store(cmd.accelHzPerS, std::memory_order_release);
                break;

            case MotorCommandType::StartRinse: {
                auto p = cmd.startRinse;
                ESP_LOGI(TAG, "StartRinse: %u cycles", p.cycles);
                domain::gBuretteState.store(BuretteState::Rinsing,
                                            std::memory_order_release);
                auto cal = domain::CalibrationData{};
                float nominalVol = cal.nominalVolumeMl;
                float curVol = domain::gVolumeMl.load(std::memory_order_acquire);
                run_rinse_sm(stepper, p.speedMlMin, p.cycles, curVol, nominalVol);
                break;
            }

            case MotorCommandType::StartCalDose: {
                auto p = cmd.startCalDose;
                ESP_LOGI(TAG, "StartCalDose");
                domain::gBuretteState.store(BuretteState::Dosing,
                                            std::memory_order_release);
                auto cal = domain::CalibrationData{};
                float nominalVol = cal.nominalVolumeMl;
                float curVol = domain::gVolumeMl.load(std::memory_order_acquire);
                run_cal_dose_sm(stepper, p.speedMlMin, curVol, nominalVol);
                break;
            }

            case MotorCommandType::StartCalSpeed: {
                auto p = cmd.startCalSpeed;
                ESP_LOGI(TAG, "StartCalSpeed: freq=%u Hz",
                         static_cast<unsigned>(p.testFreqHz));
                domain::gBuretteState.store(BuretteState::Dosing,
                                            std::memory_order_release);
                auto cal = domain::CalibrationData{};
                float nominalVol = cal.nominalVolumeMl;
                float curVol = domain::gVolumeMl.load(std::memory_order_acquire);
                run_cal_speed_sm(stepper, p.speedMlMin, p.testFreqHz,
                                 curVol, nominalVol);
                break;
            }

            case MotorCommandType::StartCalSpeedSeq: {
                auto p = cmd.startCalSpeedSeq;
                ESP_LOGI(TAG, "StartCalSpeedSeq");
                domain::gBuretteState.store(BuretteState::Dosing,
                                            std::memory_order_release);
                auto cal = domain::CalibrationData{};
                float nominalVol = cal.nominalVolumeMl;
                float curVol = domain::gVolumeMl.load(std::memory_order_acquire);
                run_cal_speed_seq_sm(stepper, p.freqs, p.fillSpeedMlMin,
                                     curVol, nominalVol);
                break;
            }

            }
        }
    }
}

} // namespace ecotiter::infrastructure
