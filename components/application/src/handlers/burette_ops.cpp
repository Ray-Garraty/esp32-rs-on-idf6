#include "application/handlers/burette_ops.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "application/command.hpp"
#include "domain/memory.hpp"

namespace ecotiter::application::handlers::burette_ops {
namespace {

using Buf = domain::memory::ResponseBuffer;

CommandResponse makeCmdResponse(const char* cmdName,
                                const char* fmt = nullptr, ...) {
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  size_t off = 0;
  auto& buf = rsp.body;
  off = static_cast<size_t>(
      std::snprintf(buf.data(), buf.size(), R"({"cmd":"%s")", cmdName));
  if (fmt) {
    va_list args;
    va_start(args, fmt);
    int n = std::vsnprintf(buf.data() + off, buf.size() - off, fmt, args);
    va_end(args);
    if (n > 0) off += static_cast<size_t>(n);
  }
  if (off < buf.size() - 1) {
    buf[off++] = '}';
  }
  if (off < buf.size()) {
    buf[off] = '\0';
  }
  rsp.bodySize = off;
  return rsp;
}

} // anonymous namespace

std::expected<CommandResponse, domain::AppError> handleFill() {
  return makeAckThenResponse();
}

std::expected<CommandResponse, domain::AppError> handleEmpty() {
  return makeAckThenResponse();
}

std::expected<CommandResponse, domain::AppError> handleDoseVolume(
    std::optional<domain::Ml> volume) {
  if (!volume) {
    return makeErrorResponse("doseVolume requires 'volume' param");
  }
  return makeAckThenResponse();
}

std::expected<CommandResponse, domain::AppError> handleRinse() {
  return makeAckThenResponse();
}

std::expected<CommandResponse, domain::AppError> handleStop() {
  return makeCmdResponse("stop", R"(,"result":"stopping")");
}

std::expected<CommandResponse, domain::AppError> handleEmergencyStop() {
  return makeCmdResponse("emergencyStop", R"(,"result":"emergency_stop")");
}

std::expected<CommandResponse, domain::AppError> handleGetStatus(
    domain::BuretteState state, int32_t tempCX100,
    domain::ValvePosition valvePos, float mv,
    domain::Direction dir, uint32_t speed,
    uint32_t accel, float volumeMl) {
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  size_t off = 0;
  serializeStatusJson(rsp.body, off, state, tempCX100,
                      valvePos, mv, dir, speed, accel, volumeMl);
  rsp.bodySize = off;
  return rsp;
}

std::expected<CommandResponse, domain::AppError> handleMoveSteps(
    std::optional<domain::Steps> steps) {
  if (!steps) {
    return makeErrorResponse("moveSteps requires 'steps' param");
  }
  return makeCmdResponse("moveSteps",
                         R"(,"steps":%ld)",
                         static_cast<long>(steps->value));
}

std::expected<CommandResponse, domain::AppError> handleSetDirection(
    std::optional<domain::Direction> dir) {
  if (!dir) {
    return makeErrorResponse("setDirection requires 'direction' param");
  }
  const char* dirStr = (*dir == domain::Direction::Cw) ? "cw" : "ccw";
  return makeCmdResponse("setDirection",
                         R"(,"direction":"%s")", dirStr);
}

std::expected<CommandResponse, domain::AppError> handleSetSpeed(
    std::optional<uint32_t> speedHz) {
  if (!speedHz) {
    return makeErrorResponse("setSpeed requires 'speed' param");
  }
  return makeCmdResponse("setSpeed",
                         R"(,"speed":%lu)",
                         static_cast<unsigned long>(*speedHz));
}

std::expected<CommandResponse, domain::AppError> handleSetAccel(
    std::optional<uint32_t> accelSteps) {
  if (!accelSteps) {
    return makeErrorResponse("setAccel requires 'accel' param");
  }
  return makeCmdResponse("setAccel",
                         R"(,"accel":%lu)",
                         static_cast<unsigned long>(*accelSteps));
}

std::expected<CommandResponse, domain::AppError> handleSetVolume(
    std::optional<domain::Ml> volume) {
  if (!volume) {
    return makeErrorResponse("setVolume requires 'targetVolume' param");
  }
  return makeCmdResponse("setVolume",
                         R"(,"volume":%.1f)",
                         static_cast<double>(volume->value));
}

std::expected<CommandResponse, domain::AppError> handleConfigMove(
    std::optional<uint32_t> speed, std::optional<uint32_t> accel) {
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  size_t off = static_cast<size_t>(
      std::snprintf(rsp.body.data(), rsp.body.size(),
                    R"({"cmd":"configMove")"));
  if (speed) {
    off += static_cast<size_t>(
        std::snprintf(rsp.body.data() + off, rsp.body.size() - off,
                      R"(,"speed":%lu)", static_cast<unsigned long>(*speed)));
  }
  if (accel) {
    off += static_cast<size_t>(
        std::snprintf(rsp.body.data() + off, rsp.body.size() - off,
                      R"(,"accel":%lu)", static_cast<unsigned long>(*accel)));
  }
  if (off < rsp.body.size() - 1) {
    rsp.body[off++] = '}';
  }
  rsp.bodySize = off;
  return rsp;
}

std::expected<CommandResponse, domain::AppError> handleConfigHome(
    std::optional<uint32_t> speed) {
  if (!speed) {
    return makeErrorResponse("configHome requires 'homeSpeed' param");
  }
  return makeCmdResponse("configHome",
                         R"(,"homeSpeed":%lu)",
                         static_cast<unsigned long>(*speed));
}

std::expected<CommandResponse, domain::AppError> handleConfigSensor(
    std::optional<uint32_t> value) {
  if (!value) {
    return makeErrorResponse("configSensor requires 'sensorValue' param");
  }
  return makeCmdResponse("configSensor",
                         R"(,"sensorValue":%lu)",
                         static_cast<unsigned long>(*value));
}

} // namespace ecotiter::application::handlers::burette_ops
