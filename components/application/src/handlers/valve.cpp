#include "application/handlers/valve.hpp"

#include <cstdio>
#include <cstring>

#include "application/command.hpp"
#include "domain/memory.hpp"
#include "infrastructure/motor_task.hpp"

namespace ecotiter::application::handlers::valve {

std::expected<CommandResponse, domain::AppError> handleSetPosition(
    std::optional<domain::ValvePosition> pos) {
  if (!pos) {
    return makeErrorResponse("invalid_params");
  }
  infrastructure::MotorCommand cmd{};
  cmd.type = infrastructure::MotorCommandType::SetValve;
  cmd.valvePos = *pos;
  if (infrastructure::gMotorCmdQueue == nullptr ||
      xQueueSend(infrastructure::gMotorCmdQueue, &cmd, 0) != pdTRUE) {
    return makeErrorResponse("start_failed");
  }
  const char* posStr = (*pos == domain::ValvePosition::Input) ? "input" : "output";
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  rsp.bodySize = static_cast<size_t>(
      std::snprintf(rsp.body.data(), rsp.body.size(),
                    R"({"status":"ok","data":{"position":"%s"}})", posStr));
  return rsp;
}

std::expected<CommandResponse, domain::AppError> handleGetState(
    domain::ValvePosition currentPos) {
  const char* posStr = (currentPos == domain::ValvePosition::Input) ? "input" : "output";
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  rsp.bodySize = static_cast<size_t>(
      std::snprintf(rsp.body.data(), rsp.body.size(),
                    R"({"status":"ok","data":{"position":"%s"}})", posStr));
  return rsp;
}

} // namespace ecotiter::application::handlers::valve
