#pragma once

#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "domain/motor_command.hpp"
#include "domain/sm_result.hpp"
#include "infrastructure/drivers/tmc_uart.hpp"

extern "C" void motorTaskEntry(void* pvParameters);

namespace ecotiter::infrastructure
{

// Backward-compatible alias — SmResult moved to domain layer for SRP.
// Remove this alias once all downstream code is migrated to domain::SmResult.
using SmResult = domain::SmResult;

// Backward-compatible aliases — MotorCommand types moved to domain layer for SRP.
// Remove these aliases once all downstream code is migrated to domain:: types.
using MotorCommandType = domain::MotorCommandType;
using MotorCommand = domain::MotorCommand;

extern QueueHandle_t gMotorCmdQueue;
extern QueueHandle_t gSmResultQueue;
extern drivers::TmcUart gTmcUart;

} // namespace ecotiter::infrastructure
