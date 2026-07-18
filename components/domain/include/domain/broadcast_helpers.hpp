#pragma once

#include "domain/types.hpp"

namespace ecotiter::domain
{

inline constexpr float DEFAULT_BROADCAST_SPEED_ML_MIN = 10.0f;

inline void updateBroadcastState()
{
    auto brtState = gBuretteState.load(std::memory_order_acquire);
    bool motorMoving = brtState != BuretteState::Idle;
    gMotorIsMoving.store(motorMoving, std::memory_order_release);
    gSpeedMlMin.store(gSpeed.load(std::memory_order_acquire) > 0 ? DEFAULT_BROADCAST_SPEED_ML_MIN
                                                                 : 0.0f,
                      std::memory_order_release);
}

} // namespace ecotiter::domain
