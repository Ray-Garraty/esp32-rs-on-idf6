#pragma once

#include "domain/errors.hpp"
#include "domain/types.hpp"

namespace ecotiter::domain
{

struct BuretteController
{
    BuretteState state = BuretteState::Idle;

    [[nodiscard]] Result<void, StateError> validateTransition(BuretteCommand cmd) const noexcept;
    [[nodiscard]] Result<void, StateError> transition(BuretteCommand cmd) noexcept;
};

} // namespace ecotiter::domain
