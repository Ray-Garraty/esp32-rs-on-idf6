#pragma once

#include <expected>

#include "application/command.hpp"
#include "domain/errors.hpp"

namespace ecotiter::application::handlers::serial {

[[nodiscard]] std::expected<CommandResponse, domain::AppError> handlePing();

} // namespace ecotiter::application::handlers::serial
