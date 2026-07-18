#include "domain/burette.hpp"

namespace ecotiter::domain
{

Result<void, StateError> BuretteController::validateTransition(BuretteCommand cmd) const noexcept
{

    // Stop and EmergencyStop are always allowed
    if (cmd == BuretteCommand::Stop || cmd == BuretteCommand::EmergencyStop)
    {
        if (state == BuretteState::Idle)
        {
            return std::unexpected(StateError::InvalidTransition);
        }
        return {};
    }

    // Reset only from Error
    if (cmd == BuretteCommand::Reset)
    {
        if (state != BuretteState::Error)
        {
            return std::unexpected(StateError::InvalidTransition);
        }
        return {};
    }

    // All other commands require Idle
    if (state != BuretteState::Idle)
    {
        return std::unexpected(StateError::Busy);
    }

    return {};
}

Result<void, StateError> BuretteController::transition(BuretteCommand cmd) noexcept
{

    auto validation = validateTransition(cmd);
    if (!validation)
    {
        return std::unexpected(validation.error());
    }

    switch (cmd)
    {
    case BuretteCommand::Fill:
        state = BuretteState::Filling;
        break;
    case BuretteCommand::Dose:
        state = BuretteState::Dosing;
        break;
    case BuretteCommand::Empty:
        state = BuretteState::Emptying;
        break;
    case BuretteCommand::Rinse:
        state = BuretteState::Rinsing;
        break;
    case BuretteCommand::Stop:
    case BuretteCommand::EmergencyStop:
        state = BuretteState::Stopping;
        break;
    case BuretteCommand::Reset:
        state = BuretteState::Idle;
        break;
    }

    return {};
}

} // namespace ecotiter::domain
