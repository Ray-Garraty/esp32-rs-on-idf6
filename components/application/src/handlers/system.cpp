#include "application/handlers/system.hpp"

#include <cstdio>
#include <cstring>

#include "application/command.hpp"
#include "domain/memory.hpp"

namespace ecotiter::application::handlers::system {

std::expected<CommandResponse, domain::AppError> handleGetStatus(
    domain::BuretteState state, int32_t tempCX100,
    domain::ValvePosition valvePos, float mv,
    domain::Direction dir, uint32_t speed,
    uint32_t accel, float volumeMl) {
  bool volumeIsNull = (state == domain::BuretteState::Homing);
  return makeStatusResponse(0, state, tempCX100, valvePos, mv,
                            dir, speed, accel, volumeMl, volumeIsNull);
}

std::expected<CommandResponse, domain::AppError> handleGetFormattedLogs() {
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  rsp.bodySize = static_cast<size_t>(
      std::snprintf(rsp.body.data(), rsp.body.size(),
                    R"({"cmd":"system.getFormattedLogs","logs":[]})"));
  return rsp;
}

std::expected<CommandResponse, domain::AppError> handleReadLog() {
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  rsp.bodySize = static_cast<size_t>(
      std::snprintf(rsp.body.data(), rsp.body.size(),
                    R"({"cmd":"system.readLog","entries":[]})"));
  return rsp;
}

std::expected<CommandResponse, domain::AppError> handleFirmwareVersion(
    std::optional<std::string_view> version) {
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  if (version) {
    rsp.bodySize = static_cast<size_t>(
        std::snprintf(rsp.body.data(), rsp.body.size(),
                      R"({"cmd":"system.firmwareVersion","version":"%.*s"})",
                      static_cast<int>(version->size()), version->data()));
  } else {
    rsp.bodySize = static_cast<size_t>(
        std::snprintf(rsp.body.data(), rsp.body.size(),
                      R"({"cmd":"system.firmwareVersion","version":"0.1.0"})"));
  }
  return rsp;
}

} // namespace ecotiter::application::handlers::system
