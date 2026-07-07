#pragma once

#include <expected>
#include <optional>

#include "application/command.hpp"
#include "domain/errors.hpp"
#include "domain/types.hpp"

namespace ecotiter::application::handlers::valve {

[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleSetPosition(
    std::optional<domain::ValvePosition> pos);
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleGetState(
    domain::ValvePosition currentPos);

} // namespace ecotiter::application::handlers::valve
