#pragma once

#include <expected>
#include <optional>

#include "application/command.hpp"
#include "domain/errors.hpp"
#include "domain/types.hpp"

namespace ecotiter::application::handlers::burette_ops
{

[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleFill();
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleEmpty();
[[nodiscard]] std::expected<CommandResponse, domain::AppError>
handleDoseVolume(std::optional<domain::Ml> volume, std::optional<float> speedMlMin = std::nullopt);
[[nodiscard]] std::expected<CommandResponse, domain::AppError>
handleRinse(std::optional<uint32_t> cycles = std::nullopt);
[[nodiscard]] std::expected<CommandResponse, domain::AppError>
handleCalRun(std::optional<std::string_view> mode, std::optional<float> freqHz,
             std::optional<float> speedMlMin);
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleStop();
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleEmergencyStop();
[[nodiscard]] std::expected<CommandResponse, domain::AppError>
handleGetStatus(domain::BuretteState state, int32_t tempCX100, domain::ValvePosition valvePos,
                float mv, domain::Direction dir, uint32_t speed, uint32_t accel, float volumeMl);
[[nodiscard]] std::expected<CommandResponse, domain::AppError>
handleMoveSteps(std::optional<domain::Steps> steps);
[[nodiscard]] std::expected<CommandResponse, domain::AppError>
handleSetDirection(std::optional<domain::Direction> dir);
[[nodiscard]] std::expected<CommandResponse, domain::AppError>
handleSetSpeed(std::optional<uint32_t> speedHz);
[[nodiscard]] std::expected<CommandResponse, domain::AppError>
handleSetAccel(std::optional<uint32_t> accelSteps);

} // namespace ecotiter::application::handlers::burette_ops
