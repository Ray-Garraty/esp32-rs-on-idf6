#include "application/handlers/sensors.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "application/command.hpp"
#include "domain/memory.hpp"

namespace ecotiter::application::handlers::sensors {
namespace {

// ── Pending ADC calibration data ──────────────────────────────────
// Holds up to 5 measurement points and computed (but unsaved) coefficients.

struct AdcCalPoint {
  float ref_mv;
  uint16_t raw_mv;
  bool collected;
};

constexpr size_t ADC_CAL_MAX_POINTS = 5;
constexpr size_t ADC_CAL_STAB_SAMPLES = 32;
constexpr uint16_t ADC_CAL_STAB_TOLERANCE = 5; // ±5 mV
constexpr int ADC_CAL_STAB_MAX_ATTEMPTS = 10;
constexpr float ADC_CAL_REFS[ADC_CAL_MAX_POINTS] = {0.0f, -177.5f, 177.5f, 350.0f, -350.0f};

AdcCalPoint gPoints[ADC_CAL_MAX_POINTS]{};
size_t gPointCount{0};
uint16_t gPendingAX1000{1000};
int16_t gPendingB{0};

int compareU16(const void* a, const void* b) {
  uint16_t va = *static_cast<const uint16_t*>(a);
  uint16_t vb = *static_cast<const uint16_t*>(b);
  if (va < vb) return -1;
  if (va > vb) return 1;
  return 0;
}

} // anonymous namespace

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
  uint16_t aX1000 = 0;
  int16_t b = 0;
  read(aX1000, b);
  float aVal = (aX1000 > 0) ? static_cast<float>(aX1000) / 1000.0f : 1.0f;
  bool isDefault = (aX1000 == 1000 && b == 0);
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  size_t off = static_cast<size_t>(
      std::snprintf(rsp.body.data(), rsp.body.size(),
                    R"({"cmd":"adc.cal.get","status":"ok","a":%.6f,"b":%d,)"
                    R"("r_squared":0.0,"calibrated_at":null,"is_default":%s,)"
                    R"("points":[],"raw_mv":0,"calibrated_mv":0})",
                    static_cast<double>(aVal), static_cast<int>(b),
                    isDefault ? "true" : "false"));
  rsp.bodySize = off;
  return rsp;
}

std::expected<CommandResponse, domain::AppError> handleAdcCalSave(
    std::optional<uint16_t> aX1000, std::optional<int16_t> b,
    AdcCalWriteCb write) {
  uint16_t a = aX1000.value_or(gPendingAX1000);
  int16_t bVal = b.value_or(gPendingB);
  auto result = write(a, bVal);
  if (!result) {
    return makeErrorResponse("failed to save ADC calibration");
  }
  gPendingAX1000 = a;
  gPendingB = bVal;
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  rsp.bodySize = static_cast<size_t>(
      std::snprintf(rsp.body.data(), rsp.body.size(),
                    R"({"cmd":"adc.cal.save","status":"ok","aX1000":%u,"b":%d})",
                    static_cast<unsigned>(a), static_cast<int>(bVal)));
  return rsp;
}

std::expected<CommandResponse, domain::AppError> handleAdcCalMeasure(
    std::optional<float> refMv,
    AdcSampleReadCb readSample,
    AdcCalWriteCb write) {
  (void)write;
  if (!refMv) {
    return makeErrorResponse("adc.cal.measure requires 'ref_mv' param");
  }

  if (gPointCount >= ADC_CAL_MAX_POINTS) {
    return makeErrorResponse("all points collected — call adc.cal.compute or adc.cal.reset");
  }

  // Stabilization: take ADC_CAL_STAB_SAMPLES, check max-min tolerance
  uint16_t samples[ADC_CAL_STAB_SAMPLES];
  bool stable = false;
  for (int attempt = 0; attempt < ADC_CAL_STAB_MAX_ATTEMPTS; ++attempt) {
    for (size_t i = 0; i < ADC_CAL_STAB_SAMPLES; ++i) {
      samples[i] = readSample();
    }
    uint16_t minVal = samples[0];
    uint16_t maxVal = samples[0];
    for (size_t i = 1; i < ADC_CAL_STAB_SAMPLES; ++i) {
      if (samples[i] < minVal) minVal = samples[i];
      if (samples[i] > maxVal) maxVal = samples[i];
    }
    if (maxVal - minVal <= ADC_CAL_STAB_TOLERANCE) {
      stable = true;
      break;
    }
  }

  if (!stable) {
    return makeErrorResponse("signal not stable — check probe connection");
  }

  // Compute median of the last stable batch
  std::qsort(samples, ADC_CAL_STAB_SAMPLES, sizeof(uint16_t), compareU16);
  uint16_t median = samples[ADC_CAL_STAB_SAMPLES / 2];

  // Store point
  size_t index = gPointCount;
  gPoints[index].ref_mv = *refMv;
  gPoints[index].raw_mv = median;
  gPoints[index].collected = true;
  ++gPointCount;

  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  rsp.bodySize = static_cast<size_t>(
      std::snprintf(rsp.body.data(), rsp.body.size(),
                    R"({"status":"ok","point":%zu,"voltage_mV":%u})",
                    index, static_cast<unsigned>(median)));
  return rsp;
}

std::expected<CommandResponse, domain::AppError> handleAdcCalCompute(
    AdcCalWriteCb write) {
  (void)write;

  if (gPointCount < ADC_CAL_MAX_POINTS) {
    return makeErrorResponse("not enough points — need 5");
  }

  // Copy points under lock-free assumption (single-threaded handler context)
  float ref[ADC_CAL_MAX_POINTS];
  uint16_t raw[ADC_CAL_MAX_POINTS];
  for (size_t i = 0; i < ADC_CAL_MAX_POINTS; ++i) {
    ref[i] = gPoints[i].ref_mv;
    raw[i] = gPoints[i].raw_mv;
  }

  // OLS: ref_mv (x) → raw_mv (y)
  // raw = a_raw * ref + b_raw
  // Then calibrated_mv = (1/a_raw) * raw + (-b_raw/a_raw)
  // We store a_x1000 = round(1/a_raw * 1000), b = round(-b_raw/a_raw)

  double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumX2 = 0.0, sumY2 = 0.0;
  int n = static_cast<int>(ADC_CAL_MAX_POINTS);

  for (int i = 0; i < n; ++i) {
    double x = static_cast<double>(ref[i]);
    double y = static_cast<double>(raw[i]);
    sumX  += x;
    sumY  += y;
    sumXY += x * y;
    sumX2 += x * x;
    sumY2 += y * y;
  }

  double denom = static_cast<double>(n) * sumX2 - sumX * sumX;
  if (std::fabs(denom) < 1e-12) {
    return makeErrorResponse("degenerate calibration points");
  }

  double aRaw = (static_cast<double>(n) * sumXY - sumX * sumY) / denom;
  double bRaw = (sumY - aRaw * sumX) / static_cast<double>(n);

  // Invert to get calibration coefficients
  // calibrated = (1/aRaw) * raw + (-bRaw/aRaw)
  double coeffA = 1.0 / aRaw;
  double coeffB = -bRaw / aRaw;

  // R-squared
  double num = static_cast<double>(n) * sumXY - sumX * sumY;
  double denomR = (static_cast<double>(n) * sumX2 - sumX * sumX) *
                  (static_cast<double>(n) * sumY2 - sumY * sumY);
  double rSquared = (denomR > 0.0) ? (num / std::sqrt(denomR)) * (num / std::sqrt(denomR)) : 0.0;

  // Store as integer: a_x1000 = a * 1000, b = b (rounded)
  uint16_t aX1000 = static_cast<uint16_t>(
      std::clamp(coeffA * 1000.0 + 0.5, 0.0, 65535.0));
  int16_t bVal = static_cast<int16_t>(
      std::clamp(coeffB + 0.5,
                 static_cast<double>(std::numeric_limits<int16_t>::min()),
                 static_cast<double>(std::numeric_limits<int16_t>::max())));

  gPendingAX1000 = aX1000;
  gPendingB = bVal;

  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  rsp.bodySize = static_cast<size_t>(
      std::snprintf(rsp.body.data(), rsp.body.size(),
                    R"({"status":"ok","data":{"a":%.6f,"b":%d,"r_squared":%.4f}})",
                    static_cast<double>(coeffA), static_cast<int>(bVal),
                    static_cast<double>(rSquared)));
  return rsp;
}

std::expected<CommandResponse, domain::AppError> handleAdcCalReset(
    AdcCalWriteCb write) {
  // Clear pending points
  for (size_t i = 0; i < ADC_CAL_MAX_POINTS; ++i) {
    gPoints[i].raw_mv = 0;
    gPoints[i].collected = false;
  }
  gPointCount = 0;
  gPendingAX1000 = 1000;
  gPendingB = 0;

  // Persist defaults to NVS
  auto result = write(1000, 0);
  if (!result) {
    return makeErrorResponse("failed to reset ADC calibration");
  }

  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  rsp.bodySize = static_cast<size_t>(
      std::snprintf(rsp.body.data(), rsp.body.size(),
                    R"({"status":"ok"})"));
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
