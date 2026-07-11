#pragma once

#include <expected>
#include <optional>

#include "application/command.hpp"
#include "domain/calibration.hpp"
#include "domain/errors.hpp"
#include "domain/types.hpp"

namespace ecotiter::application::handlers::burette_cal {

// Callback type: read current calibration from NVS
using ReadCalCb = std::expected<domain::CalibrationData, domain::ResourceError>(*)();
// Callback type: write calibration to NVS
using WriteCalCb = std::expected<void, domain::ResourceError>(*)(const domain::CalibrationData&);

[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleGetCalibration(
    ReadCalCb readCal);
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleCalcVolume(
    std::optional<domain::Steps> steps,
    std::optional<float> massG,
    std::optional<float> temperature,
    std::optional<float> pressure,
    ReadCalCb readCal);
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleCalcSpeed(
    const float* frequencies, const float* speeds, size_t count,
    ReadCalCb readCal);
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleSaveCalibration(
    std::optional<float> stepsPerMl, std::optional<float> nomVolume,
    WriteCalCb writeCal);
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleResetCalibration(
    WriteCalCb writeCal);
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleRunCalibration(
    const float* freqs, size_t freqsCount, float speedMlMin);
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleGetCalResult(
    ReadCalCb readCal);

} // namespace ecotiter::application::handlers::burette_cal
