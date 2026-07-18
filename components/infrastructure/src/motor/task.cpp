#include "internal.hpp"

#include <cstdint>
#include <cstdio>
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "diag/stack_monitor.hpp"
#include "diag/state_tracer.hpp"
#include "domain/calibration.hpp"
#include "domain/types.hpp"
#include "infrastructure/config.hpp"
#include "infrastructure/drivers/limitswitch.hpp"
#include "infrastructure/drivers/stepper.hpp"
#include "infrastructure/drivers/tmc_uart.hpp"

static constexpr auto TAG = "motor_task";

namespace ecotiter::infrastructure
{

QueueHandle_t gMotorCmdQueue = nullptr;
QueueHandle_t gSmResultQueue = nullptr;
drivers::TmcUart gTmcUart;

} // namespace ecotiter::infrastructure

namespace ecotiter::infrastructure::motor
{

using domain::BuretteState;
using domain::Direction;
using drivers::LimitSwitch;
using drivers::StepperMotor;
static constexpr uint32_t HOME_SPEED_HZ = 1500;
static constexpr uint32_t HOME_INTERVAL_US = 1'000'000 / HOME_SPEED_HZ;
void assert_rmt_preconditions() {}

extern "C" void motorTaskEntry(void* pvParameters)
{ // NOLINT(readability-function-cognitive-complexity) // reason: motor command dispatch, 11 command
  // types
    (void)pvParameters;

    puts("DBG: motorEntry START");
    fflush(stdout);

    diag::StackMonitor::instance().registerThread("motor", domain::MOTOR_THREAD_STACK);

    {
        auto largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "motor task DRAM before queue: largest_free=%lu", (unsigned long)largest);
        if (largest < config::MOTOR_MIN_DRAM)
        {
            ESP_LOGW(TAG, "Low DRAM for motor queue! largest_free=%lu", (unsigned long)largest);
        }
    }

    gMotorCmdQueue = xQueueCreate(config::MOTOR_CMD_QUEUE_LEN, sizeof(MotorCommand));
    if (gMotorCmdQueue == nullptr)
    {
        ESP_LOGE(TAG, "Failed to create motor command queue");
        vTaskDelete(nullptr);
        return;
    }
    puts("DBG: motor queue created");
    fflush(stdout);
    gSmResultQueue = xQueueCreate(1, sizeof(SmResult));
    esp_task_wdt_add(NULL);

    puts("DBG: before StepperMotor ctor (gpio 21,13)");
    fflush(stdout);
    StepperMotor stepper(config::PIN_STEP, config::PIN_EN);
    puts("DBG: after StepperMotor ctor");
    fflush(stdout);

    puts("DBG: before LimitSwitch emptySwitch ctor (gpio 15)");
    fflush(stdout);
    LimitSwitch emptySwitch(config::PIN_LIMIT_EMPTY, domain::gStopEmpty, false);
    puts("DBG: after LimitSwitch emptySwitch ctor");
    fflush(stdout);

    puts("DBG: before LimitSwitch fullSwitch ctor (gpio 7)");
    fflush(stdout);
    LimitSwitch fullSwitch(config::PIN_LIMIT_FULL, domain::gStopFull, false);
    puts("DBG: after LimitSwitch fullSwitch ctor");
    fflush(stdout);

    // Initialize TMC2209 UART
    puts("DBG: before tmc uart init");
    fflush(stdout);
    bool tmcOk =
        gTmcUart.init(config::PIN_TMC_UART_TX, config::PIN_TMC_UART_RX, config::TMC_UART_BAUD);
    if (tmcOk && gTmcUart.testConnection())
    {
        ESP_LOGI(TAG, "TMC2209 UART connected — configuring registers");

        std::ignore = gTmcUart.writeRegister(drivers::TMC_REG_GCONF, 0x00000041);
        std::ignore = gTmcUart.writeRegister(drivers::TMC_REG_CHOPCONF, 0x00002104);
        std::ignore = gTmcUart.writeRegister(drivers::TMC_REG_COOLCONF, 0x00002205);
        std::ignore = gTmcUart.writeRegister(drivers::TMC_REG_TCOOLTHRS, 0x000FFFFF);
        std::ignore = gTmcUart.writeRegister(drivers::TMC_REG_PWMCONF, 0x00010014);

        uint8_t sg = domain::gStallGuardThreshold.load(std::memory_order_acquire);
        std::ignore = gTmcUart.writeRegister(drivers::TMC_REG_SGTHRS, sg);
    }
    else
    {
        ESP_LOGW(TAG, "TMC2209 UART not connected, running without register config");
    }

    puts("DBG: before run_homing");
    fflush(stdout);
    run_homing(stepper, fullSwitch);
    domain::gHomingDone.store(true, std::memory_order_release);

    MotorCommand cmd;
    while (true)
    {
        esp_task_wdt_reset();
        if (xQueueReceive(gMotorCmdQueue, &cmd, pdMS_TO_TICKS(config::MOTOR_POLL_MS)))
        {
            switch (cmd.type)
            {

            case MotorCommandType::MoveSteps: {
                int32_t stepsBefore = stepper.position().value;
                execute_move_steps(stepper, cmd.steps);
                int32_t stepsTaken = static_cast<int32_t>(stepper.position().value) - stepsBefore;
                if (stepsTaken < 0)
                    stepsTaken = -stepsTaken;
                store_result(SmResult::Type::None, stepsTaken);
                break;
            }

            case MotorCommandType::Stop: {
                ESP_LOGI(TAG, "Stop requested");
                domain::gBuretteState.store(BuretteState::Stopping, std::memory_order_release);
                domain::gStopFull.store(true, std::memory_order_release);
                vTaskDelay(pdMS_TO_TICKS(config::STOP_SETTLE_MS));
                domain::gStopFull.store(false, std::memory_order_release);
                domain::gBuretteState.store(BuretteState::Idle, std::memory_order_release);
                break;
            }

            case MotorCommandType::EmergencyStop: {
                ESP_LOGW(TAG, "EMERGENCY STOP");
                domain::gStopFull.store(true, std::memory_order_release);
                domain::gStopEmpty.store(true, std::memory_order_release);
                domain::gBuretteState.store(BuretteState::Error, std::memory_order_release);
                diag::StateTracer::logBuretteTransition(
                    domain::buretteStateStr(domain::gBuretteState.load(std::memory_order_acquire)),
                    "error");
                break;
            }

            case MotorCommandType::Home:
                run_homing(stepper, emptySwitch);
                break;

            case MotorCommandType::SetDirection: {
                auto dir = cmd.direction;
                domain::gDirection.store(dir, std::memory_order_release);
                gpio_set_level(config::PIN_DIR, (dir == Direction::LiqIn) ? 1 : 0);
                break;
            }

            case MotorCommandType::SetSpeed:
                if (cmd.speedHz >= config::STEP_FREQ_MIN_HZ &&
                    cmd.speedHz <= config::STEP_FREQ_MAX_HZ)
                {
                    domain::gSpeed.store(cmd.speedHz, std::memory_order_release);
                }
                break;

            case MotorCommandType::SetAccel:
                domain::gAccel.store(cmd.accelHzPerS, std::memory_order_release);
                break;

            case MotorCommandType::SetStallThreshold:
                domain::gStallGuardThreshold.store(cmd.stallThreshold, std::memory_order_release);
                std::ignore = gTmcUart.writeRegister(drivers::TMC_REG_SGTHRS, cmd.stallThreshold);
                ESP_LOGI(TAG, "StallGuard threshold set to %u",
                         static_cast<unsigned>(cmd.stallThreshold));
                break;

            case MotorCommandType::StartRinse: {
                auto p = cmd.startRinse;
                ESP_LOGI(TAG, "StartRinse: %u cycles", p.cycles);
                domain::gBuretteState.store(BuretteState::Rinsing, std::memory_order_release);
                auto cal = domain::CalibrationData{};
                float nominalVol = cal.nominalVolumeMl;
                float curVol = domain::gVolumeMl.load(std::memory_order_acquire);
                run_rinse_sm(stepper, p.speedMlMin, p.cycles, curVol, nominalVol);
                break;
            }

            case MotorCommandType::StartCalDose: {
                auto p = cmd.startCalDose;
                ESP_LOGI(TAG, "StartCalDose");
                domain::gBuretteState.store(BuretteState::Dosing, std::memory_order_release);
                auto cal = domain::CalibrationData{};
                float nominalVol = cal.nominalVolumeMl;
                float curVol = domain::gVolumeMl.load(std::memory_order_acquire);
                run_cal_dose_sm(stepper, p.speedMlMin, curVol, nominalVol);
                break;
            }

            case MotorCommandType::StartCalSpeed: {
                auto p = cmd.startCalSpeed;
                ESP_LOGI(TAG, "StartCalSpeed: freq=%u Hz", static_cast<unsigned>(p.testFreqHz));
                domain::gBuretteState.store(BuretteState::Dosing, std::memory_order_release);
                auto cal = domain::CalibrationData{};
                float nominalVol = cal.nominalVolumeMl;
                float curVol = domain::gVolumeMl.load(std::memory_order_acquire);
                run_cal_speed_sm(stepper, p.speedMlMin, p.testFreqHz, curVol, nominalVol);
                break;
            }

            case MotorCommandType::StartCalSpeedSeq: {
                auto p = cmd.startCalSpeedSeq;
                ESP_LOGI(TAG, "StartCalSpeedSeq");
                domain::gBuretteState.store(BuretteState::Dosing, std::memory_order_release);
                auto cal = domain::CalibrationData{};
                float nominalVol = cal.nominalVolumeMl;
                float curVol = domain::gVolumeMl.load(std::memory_order_acquire);
                run_cal_speed_seq_sm(stepper, p.freqs, p.fillSpeedMlMin, curVol, nominalVol);
                break;
            }
            }
        }
    }
}

} // namespace ecotiter::infrastructure::motor
