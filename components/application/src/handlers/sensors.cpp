#include "application/handlers/sensors.hpp"

#include <cstdio>
#include <cstring>

#include "application/command.hpp"
#include "domain/memory.hpp"

namespace ecotiter::application::handlers::sensors {

std::expected<CommandResponse, domain::AppError> handleReadTemperature(
    int32_t tempCX100) {
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  float tempC = (tempCX100 > -99999)
      ? static_cast<float>(tempCX100) / 100.0f
      : 0.0f;
  rsp.bodySize = static_cast<size_t>(
      std::snprintf(rsp.body.data(), rsp.body.size(),
                    R"({"cmd":"temperature.read","temperature":%.1f})",
                    static_cast<double>(tempC)));
  return rsp;
}

std::expected<CommandResponse, domain::AppError> handleAdcCalGet(
    AdcCalReadCb read) {
  uint16_t a = 0;
  int16_t b = 0;
  read(a, b);
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  rsp.bodySize = static_cast<size_t>(
      std::snprintf(rsp.body.data(), rsp.body.size(),
                    R"({"cmd":"adc.cal.get","aX1000":%u,"b":%d})",
                    static_cast<unsigned>(a), static_cast<int>(b)));
  return rsp;
}

std::expected<CommandResponse, domain::AppError> handleAdcCalSave(
    std::optional<uint16_t> aX1000, std::optional<int16_t> b,
    AdcCalWriteCb write) {
  uint16_t a = aX1000.value_or(1000);
  int16_t bVal = b.value_or(0);
  auto result = write(a, bVal);
  if (!result) {
    return makeErrorResponse("failed to save ADC calibration");
  }
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  rsp.bodySize = static_cast<size_t>(
      std::snprintf(rsp.body.data(), rsp.body.size(),
                    R"({"cmd":"adc.cal.save","aX1000":%u,"b":%d})",
                    static_cast<unsigned>(a), static_cast<int>(bVal)));
  return rsp;
}

std::expected<CommandResponse, domain::AppError> handleStallGuardGet(
    uint8_t threshold) {
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  rsp.bodySize = static_cast<size_t>(
      std::snprintf(rsp.body.data(), rsp.body.size(),
                    R"({"cmd":"stallGuard.get","threshold":%u})",
                    static_cast<unsigned>(threshold)));
  return rsp;
}

std::expected<CommandResponse, domain::AppError> handleStallGuardSetThreshold(
    std::optional<uint8_t> threshold) {
  if (!threshold) {
    return makeErrorResponse("stallGuard.setThreshold requires 'threshold' param");
  }
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  rsp.bodySize = static_cast<size_t>(
      std::snprintf(rsp.body.data(), rsp.body.size(),
                    R"({"cmd":"stallGuard.setThreshold","threshold":%u})",
                    static_cast<unsigned>(*threshold)));
  return rsp;
}

} // namespace ecotiter::application::handlers::sensors
