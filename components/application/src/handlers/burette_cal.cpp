#include "application/handlers/burette_cal.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "application/command.hpp"
#include "domain/calibration.hpp"
#include "domain/memory.hpp"
#include "domain/ols.hpp"
#include "domain/z_factor.hpp"
#include "infrastructure/motor_task.hpp"

namespace ecotiter::application::handlers::burette_cal {
namespace {

CommandResponse makeCalResponse(const char* cmdName,
                                const char* fmt = nullptr, ...) {
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  size_t off = static_cast<size_t>(
      std::snprintf(rsp.body.data(), rsp.body.size(),
                    R"({"cmd":"%s","status":"ok")", cmdName));
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

static domain::CalibrationData gPendingCal = []() {
  domain::CalibrationData c{};
  c.stepsPerMl = domain::CalibrationData::kDefaultStepsPerMl;
  c.nominalVolumeMl = domain::CalibrationData::kDefaultNominalVolumeMl;
  c.speedCoeff = domain::CalibrationData::kDefaultSpeedCoeff;
  c.minFreqHz = domain::CalibrationData::kDefaultMinFreqHz;
  c.maxFreqHz = domain::CalibrationData::kDefaultMaxFreqHz;
  return c;
}();

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
                      R"({"cmd":"cal.get","status":"ok","steps_per_ml":%.1f,)"
                      R"("nominal_vol":%.2f,"speed_coeff":%.5f,)"
                      R"("min_freq":%u,"max_freq":%u,)"
                      R"("calibration_date":%ld,"is_default":true})",
                      static_cast<double>(cal->stepsPerMl),
                      static_cast<double>(cal->nominalVolumeMl),
                      static_cast<double>(cal->speedCoeff),
                      static_cast<unsigned>(cal->minFreqHz),
                      static_cast<unsigned>(cal->maxFreqHz),
                      static_cast<long>(0)));  // calibration_date not yet tracked
  } else {
    off = static_cast<size_t>(
        std::snprintf(rsp.body.data(), rsp.body.size(),
                      R"({"cmd":"cal.get","error":"nvs_unavailable"})"));
  }
  rsp.bodySize = off;
  return rsp;
}

std::expected<CommandResponse, domain::AppError> handleCalcVolume(
    std::optional<domain::Steps> steps,
    std::optional<float> massG,
    std::optional<float> temperature,
    std::optional<float> pressure,
    ReadCalCb readCal) {
  (void)steps;
  if (!massG || *massG <= 0.0f) {
    return makeErrorResponse("cal.calcVolume requires 'mass_g' > 0");
  }
  auto cal = readCal();
  if (!cal) {
    return makeErrorResponse("calibration not available");
  }
  float temp = temperature.value_or(25.0f);
  float press = pressure.value_or(101.3f);
  float z = domain::getZFactor(temp, press);
  float actualVol = *massG * z;
  float newSpm = domain::calculateNewStepsPerMl(
      cal->stepsPerMl, cal->nominalVolumeMl, actualVol);
  float relError = (actualVol - cal->nominalVolumeMl) /
      cal->nominalVolumeMl * 100.0f;

  gPendingCal.stepsPerMl = newSpm;
  gPendingCal.nominalVolumeMl = actualVol;

  return makeCalResponse("cal.calcVolume",
      R"(,"z_factor":%.6f,"actual_volume_ml":%.4f,)"
      R"("new_steps_per_ml":%.1f,"relative_error_pct":%.2f)",
      static_cast<double>(z), static_cast<double>(actualVol),
      static_cast<double>(newSpm), static_cast<double>(relError));
}

std::expected<CommandResponse, domain::AppError> handleCalcSpeed(
    const float* frequencies, const float* speeds, size_t count,
    ReadCalCb readCal) {
  if (count < 2) {
    return makeErrorResponse("cal.calcSpeed requires >= 2 measurements");
  }
  auto cal = readCal();
  if (!cal) {
    return makeErrorResponse("calibration not available");
  }
  auto result = domain::calculateSpeedCalibration(frequencies, speeds, count);

  gPendingCal.speedCoeff = result.k;

  return makeCalResponse("cal.calcSpeed",
      R"(,"k":%.6f,"r_squared":%.4f,"min_freq":%u,"max_freq":%u)",
      static_cast<double>(result.k),
      static_cast<double>(result.rSquared),
      cal->minFreqHz, cal->maxFreqHz);
}

std::expected<CommandResponse, domain::AppError> handleSaveCalibration(
    std::optional<float> stepsPerMl, std::optional<float> nomVolume,
    WriteCalCb writeCal) {
  domain::CalibrationData cal = gPendingCal;
  if (stepsPerMl) cal.stepsPerMl = *stepsPerMl;
  if (nomVolume) cal.nominalVolumeMl = *nomVolume;
  auto result = writeCal(cal);
  if (!result) {
    return makeErrorResponse("failed to save calibration");
  }
  gPendingCal = cal;
  return makeCalResponse("cal.save",
                         R"(,"stepsPerMl":%.1f,"nominalVolume":%.1f)",
                         static_cast<double>(cal.stepsPerMl),
                         static_cast<double>(cal.nominalVolumeMl));
}

std::expected<CommandResponse, domain::AppError> handleResetCalibration(
    WriteCalCb writeCal) {
  domain::CalibrationData defaults{};
  defaults.stepsPerMl = domain::CalibrationData::kDefaultStepsPerMl;
  defaults.nominalVolumeMl = domain::CalibrationData::kDefaultNominalVolumeMl;
  defaults.speedCoeff = domain::CalibrationData::kDefaultSpeedCoeff;
  defaults.minFreqHz = domain::CalibrationData::kDefaultMinFreqHz;
  defaults.maxFreqHz = domain::CalibrationData::kDefaultMaxFreqHz;
  auto result = writeCal(defaults);
  if (!result) {
    return makeErrorResponse("failed to reset calibration");
  }
  return makeCalResponse("cal.reset",
                         R"(,"stepsPerMl":%.1f,"nominalVolume":%.1f)",
                         static_cast<double>(defaults.stepsPerMl),
                         static_cast<double>(defaults.nominalVolumeMl));
}

std::expected<CommandResponse, domain::AppError> handleRunCalibration(
    const float* freqs, size_t freqsCount, float speedMlMin) {
  if (freqsCount < 2 || !freqs) {
    return makeErrorResponse("cal.runSpeedSeq requires >= 2 frequencies");
  }
  size_t count = (freqsCount > 3) ? 3 : freqsCount;
  infrastructure::MotorCommand cmd{};
  cmd.type = infrastructure::MotorCommandType::StartCalSpeedSeq;
  cmd.startCalSpeedSeq.fillSpeedMlMin = (speedMlMin > 0.0f) ? speedMlMin : 20.0f;
  for (size_t i = 0; i < count; ++i) {
    cmd.startCalSpeedSeq.freqs[i] = (freqs[i] > 0.0f)
        ? static_cast<uint16_t>(freqs[i] + 0.5f) : 0;
  }
  if (infrastructure::gMotorCmdQueue == nullptr) {
    return makeErrorResponse("motor task not ready");
  }
  if (xQueueSend(infrastructure::gMotorCmdQueue, &cmd, pdMS_TO_TICKS(10)) != pdTRUE) {
    return makeErrorResponse("motor command queue full");
  }
  return makeAckThenResponse();
}

std::expected<CommandResponse, domain::AppError> handleGetCalResult(
    ReadCalCb readCal) {
  auto& result = infrastructure::gSmResult;
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  size_t off = 0;

  if (result.type == infrastructure::SmResult::Type::None) {
    off = static_cast<size_t>(
        std::snprintf(rsp.body.data(), rsp.body.size(),
                      R"({"cmd":"cal.getResult","status":"no_result"})"));
    rsp.bodySize = off;
    return rsp;
  }

  off = static_cast<size_t>(
      std::snprintf(rsp.body.data(), rsp.body.size(),
                    R"({"cmd":"cal.getResult","status":"ok")"));

  switch (result.type) {
    case infrastructure::SmResult::Type::RinseComplete:
      off += static_cast<size_t>(
          std::snprintf(rsp.body.data() + off, rsp.body.size() - off,
                        R"(,"resultType":"rinse")"));
      break;
    case infrastructure::SmResult::Type::CalDoseComplete:
      off += static_cast<size_t>(
          std::snprintf(rsp.body.data() + off, rsp.body.size() - off,
                        R"(,"resultType":"cal_dose","stepsTaken":%ld)",
                        static_cast<long>(result.stepsTaken)));
      break;
    case infrastructure::SmResult::Type::CalSpeedComplete:
      off += static_cast<size_t>(
          std::snprintf(rsp.body.data() + off, rsp.body.size() - off,
                        R"(,"resultType":"cal_speed","speedMlMin":%.2f)",
                        static_cast<double>(result.measuredSpeedMlMin)));
      break;
    case infrastructure::SmResult::Type::CalSpeedSeqComplete: {
      off += static_cast<size_t>(
          std::snprintf(rsp.body.data() + off, rsp.body.size() - off,
                        R"(,"resultType":"cal_speed_seq","results":[)"));
      for (int i = 0; i < result.resultCount && i < 3; ++i) {
        if (i > 0) {
          off += static_cast<size_t>(
              std::snprintf(rsp.body.data() + off, rsp.body.size() - off, ","));
        }
        off += static_cast<size_t>(
            std::snprintf(rsp.body.data() + off, rsp.body.size() - off,
                          "%.2f", static_cast<double>(result.results[i])));
      }
      off += static_cast<size_t>(
          std::snprintf(rsp.body.data() + off, rsp.body.size() - off, "]"));
      break;
    }
    default:
      off += static_cast<size_t>(
          std::snprintf(rsp.body.data() + off, rsp.body.size() - off,
                        R"(,"resultType":"error")"));
      break;
  }

  if (off < rsp.body.size() - 1) {
    rsp.body[off++] = '}';
  }
  rsp.bodySize = off;
  return rsp;
}

} // namespace ecotiter::application::handlers::burette_cal
