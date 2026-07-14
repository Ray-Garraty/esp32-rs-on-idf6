#pragma once

#include <expected>
#include <optional>

#include "application/command.hpp"
#include "domain/errors.hpp"
#include "domain/types.hpp"

namespace ecotiter::application::handlers::system {

[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleGetStatus(
    domain::BuretteState state, int32_t tempCX100,
    domain::ValvePosition valvePos, float mv,
    domain::Direction dir, uint32_t speed,
    uint32_t accel, float volumeMl);
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleGetFormattedLogs();
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleReadLog();
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleFirmwareVersion(
    std::optional<std::string_view> version);

} // namespace ecotiter::application::handlers::system
