#pragma once

#include <expected>

#include "application/command.hpp"
#include "domain/errors.hpp"

namespace ecotiter::application {

// Central dispatch: routes a parsed Command to the correct handler module.
// Returns a CommandResponse that the caller sends back over the transport.
[[nodiscard]] std::expected<CommandResponse, domain::AppError> dispatch(
    const Command& cmd);

} // namespace ecotiter::application
