#pragma once

#include <atomic>
#include <cstdint>

#include "domain/types.hpp"
#include "infrastructure/drivers/limitswitch.hpp"
#include "infrastructure/drivers/stepper.hpp"
#include "infrastructure/motor_task.hpp"

namespace ecotiter::infrastructure::motor
{

// ── task.cpp (shared) ──────────────────────────────────────────
void assert_rmt_preconditions();

// ── motion.cpp ──────────────────────────────────────────────────
void set_valve(domain::ValvePosition pos);
uint32_t ml_min_to_hz(float speedMlMin);
void move_to_endstop(drivers::StepperMotor& stepper, domain::Direction dir, uint32_t speedHz,
                     std::atomic<bool>& stopFlag);
void move_fill(drivers::StepperMotor& stepper, uint32_t speedHz);
void move_empty(drivers::StepperMotor& stepper, uint32_t speedHz);
void store_result(SmResult::Type type, int32_t stepsTaken = 0, float measuredSpeed = 0.0f,
                  const float* results = nullptr, int resultCount = 0);
void execute_move_steps(drivers::StepperMotor& stepper, int32_t steps);

// ── sm_runners.cpp ────────────────────────────────────────────
void run_rinse_sm(drivers::StepperMotor& stepper, float speedMlMin, uint8_t cycles,
                  float currentVolumeMl, float nominalVolumeMl);
void run_cal_dose_sm(drivers::StepperMotor& stepper, float speedMlMin, float currentVolumeMl,
                     float nominalVolumeMl);
void run_cal_speed_sm(drivers::StepperMotor& stepper, float speedMlMin, uint16_t testFreqHz,
                      float currentVolumeMl, float nominalVolumeMl);
void run_cal_speed_seq_sm(drivers::StepperMotor& stepper, const uint16_t freqs[3],
                          float fillSpeedMlMin, float currentVolumeMl, float nominalVolumeMl);

// ── homing.cpp ────────────────────────────────────────────────
void run_homing(drivers::StepperMotor& stepper, drivers::LimitSwitch& fullSwitch);

} // namespace ecotiter::infrastructure::motor
