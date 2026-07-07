#pragma once

#include <expected>
#include <optional>

#include "application/command.hpp"
#include "domain/errors.hpp"
#include "domain/types.hpp"

namespace ecotiter::application::handlers::burette_ops {

[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleFill();
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleEmpty();
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleDoseVolume(
    std::optional<domain::Ml> volume);
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleRinse();
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleStop();
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleEmergencyStop();
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleGetStatus(
    domain::BuretteState state, int32_t tempCX100,
    domain::ValvePosition valvePos, float mv,
    domain::Direction dir, uint32_t speed,
    uint32_t accel, float volumeMl);
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleMoveSteps(
    std::optional<domain::Steps> steps);
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleSetDirection(
    std::optional<domain::Direction> dir);
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleSetSpeed(
    std::optional<uint32_t> speedHz);
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleSetAccel(
    std::optional<uint32_t> accelSteps);
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleSetVolume(
    std::optional<domain::Ml> volume);
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleConfigMove(
    std::optional<uint32_t> speed, std::optional<uint32_t> accel);
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleConfigHome(
    std::optional<uint32_t> speed);
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleConfigSensor(
    std::optional<uint32_t> value);

} // namespace ecotiter::application::handlers::burette_ops
