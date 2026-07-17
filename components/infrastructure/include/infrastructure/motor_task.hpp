#pragma once

#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "domain/sm_result.hpp"
#include "domain/types.hpp"
#include "infrastructure/drivers/tmc_uart.hpp"

extern "C" void motorTaskEntry(void* pvParameters);

namespace ecotiter::infrastructure {

// Backward-compatible alias — SmResult moved to domain layer for SRP.
// Remove this alias once all downstream code is migrated to domain::SmResult.
using SmResult = domain::SmResult;

enum class MotorCommandType : uint8_t {
    MoveSteps,
    Stop,
    EmergencyStop,
    Home,
    SetDirection,
    SetSpeed,
    SetAccel,
    SetStallThreshold,
    StartRinse,
    StartCalDose,
    StartCalSpeed,
    StartCalSpeedSeq
};

struct StartRinseParams {
    uint8_t cycles;
    float speedMlMin;
};

struct StartCalDoseParams {
    float speedMlMin;
};

struct StartCalSpeedParams {
    float speedMlMin;
    uint16_t testFreqHz;
};

struct StartCalSpeedSeqParams {
    uint16_t freqs[3];
    float fillSpeedMlMin;
};

struct MotorCommand {
    MotorCommandType type;
    int32_t steps;
    domain::Direction direction;
    uint32_t speedHz;
    uint32_t accelHzPerS;
    uint8_t stallThreshold;
    StartRinseParams startRinse;
    StartCalDoseParams startCalDose;
    StartCalSpeedParams startCalSpeed;
    StartCalSpeedSeqParams startCalSpeedSeq;
};

extern QueueHandle_t gMotorCmdQueue;
extern QueueHandle_t gSmResultQueue;
extern drivers::TmcUart gTmcUart;

} // namespace ecotiter::infrastructure
