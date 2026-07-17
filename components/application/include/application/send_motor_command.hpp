#pragma once

#include "infrastructure/motor_task.hpp"

namespace ecotiter::application {

inline bool sendMotorCommand(const domain::MotorCommand& cmd) {
    if (infrastructure::gMotorCmdQueue == nullptr) return false;
    return xQueueSend(infrastructure::gMotorCmdQueue, &cmd, 0) == pdTRUE;
}

} // namespace ecotiter::application
