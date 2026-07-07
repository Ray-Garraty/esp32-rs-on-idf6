#include "application/handlers/burette_cal.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "application/command.hpp"
#include "domain/calibration.hpp"
#include "domain/memory.hpp"

namespace ecotiter::application::handlers::burette_cal {
namespace {

CommandResponse makeCalResponse(const char* cmdName,
                                const char* fmt = nullptr, ...) {
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  size_t off = static_cast<size_t>(
      std::snprintf(rsp.body.data(), rsp.body.size(),
                    R"({"cmd":"%s")", cmdName));
  if (fmt) {
    va_list args;
    va_start(args, fmt);
    int n = std::vsnprintf(rsp.body.data() + off,
                           rsp.body.size() - off, fmt, args);
    va_end(args);
    if (n > 0) off += static_cast<size_t>(n);
  }
  if (off < rsp.body.size() - 1) {
    rsp.body[off++] = '}';
  }
  if (off < rsp.body.size()) {
    rsp.body[off] = '\0';
  }
  rsp.bodySize = off;
  return rsp;
}

} // anonymous namespace

std::expected<CommandResponse, domain::AppError> handleGetCalibration(
    ReadCalCb readCal) {
  auto cal = readCal();
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  size_t off = 0;
  if (cal) {
    off = static_cast<size_t>(
        std::snprintf(rsp.body.data(), rsp.body.size(),
                      R"({"cmd":"cal.get","stepsPerMl":%.1f,"nominalVolume":%.1f})",
                      static_cast<double>(cal->stepsPerMl),
                      static_cast<double>(cal->nominalVolumeMl)));
  } else {
    off = static_cast<size_t>(
        std::snprintf(rsp.body.data(), rsp.body.size(),
                      R"({"cmd":"cal.get","error":"nvs_unavailable"})"));
  }
  rsp.bodySize = off;
  return rsp;
}

std::expected<CommandResponse, domain::AppError> handleCalcVolume(
    std::optional<domain::Steps> steps, ReadCalCb readCal) {
  if (!steps) {
    return makeErrorResponse("cal.calcVolume requires 'steps' param");
  }
  auto cal = readCal();
  if (!cal) {
    return makeErrorResponse("calibration not available");
  }
  auto vol = domain::stepsToMl(*steps, *cal);
  return makeCalResponse("cal.calcVolume",
                         R"(,"steps":%ld,"volume":%.1f)",
                         static_cast<long>(steps->value),
                         static_cast<double>(vol.value));
}

std::expected<CommandResponse, domain::AppError> handleCalcSpeed(
    std::optional<uint32_t> intervalUs, ReadCalCb readCal) {
  if (!intervalUs || *intervalUs == 0) {
    return makeErrorResponse("cal.calcSpeed requires 'intervalUs' param");
  }
  auto cal = readCal();
  if (!cal) {
    return makeErrorResponse("calibration not available");
  }
  // Speed = stepsPerMl * 1,000,000 / intervalUs (approx steps/sec)
  float stepsPerSec = cal->stepsPerMl * 1'000'000.0f /
      static_cast<float>(*intervalUs);
  return makeCalResponse("cal.calcSpeed",
                         R"(,"intervalUs":%lu,"stepsPerSec":%.1f)",
                         static_cast<unsigned long>(*intervalUs),
                         static_cast<double>(stepsPerSec));
}

std::expected<CommandResponse, domain::AppError> handleSaveCalibration(
    std::optional<float> stepsPerMl, std::optional<float> nomVolume,
    WriteCalCb writeCal) {
  domain::CalibrationData cal{1000.0f, 50.0f}; // defaults
  if (stepsPerMl) cal.stepsPerMl = *stepsPerMl;
  if (nomVolume) cal.nominalVolumeMl = *nomVolume;
  auto result = writeCal(cal);
  if (!result) {
    return makeErrorResponse("failed to save calibration");
  }
  return makeCalResponse("cal.save",
                         R"(,"stepsPerMl":%.1f,"nominalVolume":%.1f)",
                         static_cast<double>(cal.stepsPerMl),
                         static_cast<double>(cal.nominalVolumeMl));
}

std::expected<CommandResponse, domain::AppError> handleResetCalibration(
    WriteCalCb writeCal) {
  domain::CalibrationData defaults{1000.0f, 50.0f};
  auto result = writeCal(defaults);
  if (!result) {
    return makeErrorResponse("failed to reset calibration");
  }
  return makeCalResponse("cal.reset",
                         R"(,"stepsPerMl":%.1f,"nominalVolume":%.1f)",
                         static_cast<double>(defaults.stepsPerMl),
                         static_cast<double>(defaults.nominalVolumeMl));
}

std::expected<CommandResponse, domain::AppError> handleRunCalibration() {
  return makeAckThenResponse();
}

std::expected<CommandResponse, domain::AppError> handleGetCalResult(
    ReadCalCb readCal) {
  auto cal = readCal();
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  size_t off = 0;
  if (cal) {
    off = static_cast<size_t>(
        std::snprintf(rsp.body.data(), rsp.body.size(),
                      R"({"cmd":"cal.getResult","stepsPerMl":%.1f,"nominalVolume":%.1f})",
                      static_cast<double>(cal->stepsPerMl),
                      static_cast<double>(cal->nominalVolumeMl)));
  } else {
    off = static_cast<size_t>(
        std::snprintf(rsp.body.data(), rsp.body.size(),
                      R"({"cmd":"cal.getResult","error":"nvs_unavailable"})"));
  }
  rsp.bodySize = off;
  return rsp;
}

} // namespace ecotiter::application::handlers::burette_cal
