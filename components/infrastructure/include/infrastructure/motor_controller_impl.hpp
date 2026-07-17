#pragma once

#include <cstdint>
#include <optional>

#include "application/motor_controller.hpp"
#include "domain/sm_result.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace ecotiter::infrastructure
{

/// Concrete implementation of IMotorController that wraps the motor task's
/// global FreeRTOS queues (gMotorCmdQueue, gSmResultQueue).
///
/// Thread-safe: all methods delegate to FreeRTOS queue API.
class MotorControllerImpl : public application::IMotorController
{
public:
    /// Constructs the controller using the extern globals
    /// (gMotorCmdQueue, gSmResultQueue). The globals start as nullptr
    /// and become valid once the motor task initialises its queues.
    MotorControllerImpl() = default;

    /// Constructs the controller with explicit queue handles.
    MotorControllerImpl(QueueHandle_t cmdQueue, QueueHandle_t resultQueue);

    bool sendCommand(const char* cmdJson, size_t len) override;
    bool readTmcRegister(uint8_t reg, uint32_t& value) override;
    std::optional<domain::SmResult> peekResult() override;
    std::optional<domain::SmResult> waitResult(uint32_t timeoutMs) override;

private:
    QueueHandle_t cmdQueue_{nullptr};
    QueueHandle_t resultQueue_{nullptr};

    // When true, use the explicit handles; otherwise use extern globals.
    bool useExplicitHandles_{false};

    QueueHandle_t getCmdQueue() const noexcept;
    QueueHandle_t getResultQueue() const noexcept;

    // Minimal JSON helpers (no nlohmann dependency in infrastructure layer)
    static const char* findJsonString(const char* json, size_t len, const char* key,
                                      size_t* valueLen);
    static bool findJsonInt(const char* json, size_t len, const char* key, int32_t& value);
    static bool findJsonFloat(const char* json, size_t len, const char* key, float& value);
    static bool findJsonBool(const char* json, size_t len, const char* key, bool& value);
};

} // namespace ecotiter::infrastructure
