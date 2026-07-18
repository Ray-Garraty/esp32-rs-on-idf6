#pragma once

#include <cstdint>
#include <optional>

#include "domain/sm_result.hpp"

namespace ecotiter::application
{

/// Abstract interface for motor control operations.
///
/// Decouples the application layer from infrastructure motor task globals.
/// Implementations wrap FreeRTOS queues and provide a clean API for
/// sending commands and receiving results.
class IMotorController
{
public:
    virtual ~IMotorController() = default;

    /// Send a motor command JSON string (e.g. {"cmd":"stop"}).
    /// Parses the JSON, constructs the appropriate MotorCommand, and
    /// queues it for the motor task.
    /// @return true if queued successfully, false if queue full.
    virtual bool sendCommand(const char* cmdJson, size_t len) = 0;

    /// Read a TMC2209 register via UART.
    /// @param reg   Register address (e.g. TMC_REG_SG_RESULT).
    /// @param value Output parameter for the register value.
    /// @return true if read succeeded, false on error.
    virtual bool readTmcRegister(uint8_t reg, uint32_t& value) = 0;

    /// Non-blocking peek for a state-machine result.
    /// @return SmResult if one is available, std::nullopt otherwise.
    virtual std::optional<domain::SmResult> peekResult() = 0;

    /// Blocking wait for a state-machine result with timeout.
    /// @param timeoutMs  maximum time to wait in milliseconds.
    /// @return SmResult if received within timeout, std::nullopt on timeout.
    // NOTE: Deprecated for HTTP handlers — use WS broadcast for async completion.
    // Still used internally by serial/console path with timeout=0 (non-blocking peek).
    virtual std::optional<domain::SmResult> waitResult(uint32_t timeoutMs) = 0;
};

} // namespace ecotiter::application
