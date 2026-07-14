#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include "domain/burette.hpp"
#include "domain/errors.hpp"
#include "domain/memory.hpp"
#include "domain/types.hpp"

namespace ecotiter::application {

enum class CommandType : uint8_t {
  // Burette operations
  Fill,
  Empty,
  DoseVolume,
  Rinse,
  Stop,
  EmergencyStop,
  GetStatus,
  MoveSteps,
  SetDirection,
  SetSpeed,
  SetAccel,

  // Calibration
  CalGet,
  CalCalcVolume,
  CalCalcSpeed,
  CalSave,
  CalReset,
  CalRun,
  CalGetResult,
  CalRunSpeedSeq,
  MoveToStop,
  // Sensors
  TempRead,
  AdcCalGet,
  AdcCalSave,
  AdcCalMeasure,
  AdcCalCompute,
  AdcCalReset,
  StallGuardGet,
  StallGuardSetThreshold,
  // Valve
  ValveSetPosition,
  ValveGetState,
  // System
  SystemGetStatus,
  SystemGetFormattedLogs,
  SystemReadLog,
  SystemReboot,
  SystemFirmwareVersion,
  // Serial
  SerialPing
};

struct Command {
  CommandType type;

  // Optional parameters — read only when appropriate for `type`
  std::optional<domain::Ml> volume;
  std::optional<domain::Steps> steps;
  std::optional<domain::Direction> direction;
  std::optional<uint32_t> speed;
  std::optional<uint32_t> accel;
  std::optional<float> speedMlMin;
  std::optional<domain::Ml> targetVolume;
  std::optional<domain::ValvePosition> valvePos;
  std::optional<uint8_t> sgThreshold;
  std::optional<std::string> mode;
  std::optional<float> freqHz;

  // For cal.calcVolume
  std::optional<float> massG;
  std::optional<float> temperature;
  std::optional<float> pressure;
  // For adc.cal.measure
  std::optional<float> refMv;

  uint64_t id{0};

  // For cal.calcSpeed
  static constexpr size_t MAX_MEASUREMENTS = 16;
  struct {
    float freqs[MAX_MEASUREMENTS];
    float speeds[MAX_MEASUREMENTS];
    size_t count;
  } measurements;

  // For burette.cal.runSpeedSeq: flat freqs array
  float freqsArray[MAX_MEASUREMENTS]{};
  size_t freqsCount{0};
};

enum class ResponseKind : uint8_t {
  Single,
  Error,
  AckThen,
  NoResponse
};

struct CommandResponse {
  ResponseKind kind = ResponseKind::Single;
  uint64_t id{0};
  domain::memory::ResponseBuffer body{};
  size_t bodySize{0};
};

// Deserialize a JSON string into a Command
[[nodiscard]] std::expected<Command, domain::ProtocolError> parseCommand(
    std::string_view json);

// Serialize a single JSON value into the response buffer
[[nodiscard]] std::expected<size_t, domain::ProtocolError> serializeToBuffer(
    const CommandResponse& rsp,
    domain::memory::ResponseBuffer& buf);

// Convenience: build a single-value response from a json-like payload
CommandResponse makeAckThenResponse();
CommandResponse makeErrorResponse(std::string_view message);
CommandResponse makeSingleResponse(std::string_view payload, size_t size);

void serializeStatusJson(domain::memory::ResponseBuffer& buf, size_t& offset,
                         domain::BuretteState state, int32_t tempCX100,
                         domain::ValvePosition valvePos, float mv,
                         domain::Direction dir, uint32_t speed,
                         uint32_t accel, float volumeMl,
                         bool volumeIsNull = false);
CommandResponse makeStatusResponse(uint64_t id,
                                   domain::BuretteState state, int32_t tempCX100,
                                   domain::ValvePosition valvePos, float mv,
                                   domain::Direction dir, uint32_t speed,
                                   uint32_t accel, float volumeMl,
                                   bool volumeIsNull = false);

} // namespace ecotiter::application
