#pragma once

#include <expected>
#include <optional>

#include "application/command.hpp"
#include "domain/errors.hpp"
#include "domain/types.hpp"

namespace ecotiter::application::handlers::sensors
{

using AdcCalReadCb = void (*)(uint16_t& aX1000, int16_t& b);
using AdcCalWriteCb = std::expected<void, domain::ResourceError> (*)(uint16_t aX1000, int16_t b);
using AdcSampleReadCb = uint16_t (*)(void);

[[nodiscard]] std::expected<CommandResponse, domain::AppError>
handleReadTemperature(int32_t tempCX100);
[[nodiscard]] std::expected<CommandResponse, domain::AppError> handleAdcCalGet(AdcCalReadCb read);
[[nodiscard]] std::expected<CommandResponse, domain::AppError>
handleAdcCalSave(std::optional<uint16_t> aX1000, std::optional<int16_t> b, AdcCalWriteCb write);
[[nodiscard]] std::expected<CommandResponse, domain::AppError>
handleAdcCalMeasure(std::optional<float> refMv, AdcSampleReadCb readSample, AdcCalWriteCb write);
[[nodiscard]] std::expected<CommandResponse, domain::AppError>
handleAdcCalCompute(AdcCalWriteCb write);
[[nodiscard]] std::expected<CommandResponse, domain::AppError>
handleAdcCalReset(AdcCalWriteCb write);
[[nodiscard]] std::expected<CommandResponse, domain::AppError>
handleStallGuardGet(uint8_t threshold);
[[nodiscard]] std::expected<CommandResponse, domain::AppError>
handleStallGuardSetThreshold(std::optional<uint8_t> threshold);

} // namespace ecotiter::application::handlers::sensors
