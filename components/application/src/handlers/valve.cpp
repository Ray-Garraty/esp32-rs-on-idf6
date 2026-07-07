#include "application/handlers/valve.hpp"

#include <cstdio>
#include <cstring>

#include "application/command.hpp"
#include "domain/memory.hpp"

namespace ecotiter::application::handlers::valve {

std::expected<CommandResponse, domain::AppError> handleSetPosition(
    std::optional<domain::ValvePosition> pos) {
  if (!pos) {
    return makeErrorResponse("valve.setPosition requires 'position' param");
  }
  const char* posStr = (*pos == domain::ValvePosition::Input) ? "input" : "output";
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  rsp.bodySize = static_cast<size_t>(
      std::snprintf(rsp.body.data(), rsp.body.size(),
                    R"({"cmd":"valve.setPosition","position":"%s"})", posStr));
  return rsp;
}

std::expected<CommandResponse, domain::AppError> handleGetState(
    domain::ValvePosition currentPos) {
  const char* posStr = (currentPos == domain::ValvePosition::Input) ? "input" : "output";
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  rsp.bodySize = static_cast<size_t>(
      std::snprintf(rsp.body.data(), rsp.body.size(),
                    R"({"cmd":"valve.getState","position":"%s"})", posStr));
  return rsp;
}

} // namespace ecotiter::application::handlers::valve
