#pragma once

#include <expected>

#include "application/command.hpp"
#include "application/motor_controller.hpp"
#include "domain/errors.hpp"

namespace ecotiter::application
{

// Central dispatch: routes a parsed Command to the correct handler module.
// Returns a CommandResponse that the caller sends back over the transport.
[[nodiscard]] std::expected<CommandResponse, domain::AppError> dispatch(const Command& cmd);

// Set callback for ADC sample reads (platform-specific — called from main.cpp)
void setAdcSampleReadCb(uint16_t (*cb)());

// Set the motor controller instance used by the dispatch and handlers.
// Must be called once during boot, after the motor task queues are created.
void setMotorController(IMotorController* controller) noexcept;

// Get the currently registered motor controller (may be nullptr if not set).
IMotorController* getMotorController() noexcept;

} // namespace ecotiter::application
