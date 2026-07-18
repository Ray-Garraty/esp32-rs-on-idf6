#pragma once

#include <cstdint>

#include "domain/types.hpp"

namespace ecotiter::domain
{

/// Motor command type — moved from infrastructure to domain for SRP layering.
/// These are pure data types with no hardware dependency.
enum class MotorCommandType : uint8_t
{
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
    StartCalSpeedSeq,
    SetValvePosition,
    ReadTmcRegister
};

struct StartRinseParams
{
    uint8_t cycles;
    float speedMlMin;
};

struct StartCalDoseParams
{
    float speedMlMin;
};

struct StartCalSpeedParams
{
    float speedMlMin;
    uint16_t testFreqHz;
};

struct StartCalSpeedSeqParams
{
    uint16_t freqs[3];
    float fillSpeedMlMin;
};

struct ReadTmcRegisterParams
{
    uint8_t reg; // TMC register address (e.g. TMC_REG_SG_RESULT)
};

/// Command struct sent via FreeRTOS queue to the motor task.
///
/// ABI-critical: layout must remain stable because FreeRTOS queues use
/// sizeof(MotorCommand) for message sizing.
struct MotorCommand
{
    MotorCommandType type;
    int32_t steps;
    Direction direction;
    uint32_t speedHz;
    uint32_t accelHzPerS;
    uint8_t stallThreshold;
    ValvePosition valvePosition;
    StartRinseParams startRinse;
    StartCalDoseParams startCalDose;
    StartCalSpeedParams startCalSpeed;
    StartCalSpeedSeqParams startCalSpeedSeq;
    ReadTmcRegisterParams readTmcReg;
};

} // namespace ecotiter::domain
