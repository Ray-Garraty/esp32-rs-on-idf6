#include "application/state_machine.hpp"

namespace ecotiter::application {

domain::Result<void, domain::StateError> ApplicationStateMachine::apply(
    domain::BuretteCommand cmd) noexcept {
  return controller_.transition(cmd);
}

void ApplicationStateMachine::startOperation(
    uint32_t currentTick, uint32_t durationTicks) noexcept {
  pending_.startTick = currentTick;
  pending_.expectedDurationTicks = durationTicks;
  pending_.active = true;
}

bool ApplicationStateMachine::tick(uint32_t currentTick) noexcept {
  if (!pending_.active) return false;

  uint32_t elapsed = currentTick - pending_.startTick;
  if (elapsed >= pending_.expectedDurationTicks) {
    // Operation completed or timed out — reset to Idle
    pending_.active = false;
    controller_.state = domain::BuretteState::Idle;
    return true;
  }
  return false;
}

void ApplicationStateMachine::reset() noexcept {
  pending_.active = false;
  controller_.state = domain::BuretteState::Idle;
}

} // namespace ecotiter::application
