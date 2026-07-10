#include "application/command.hpp"

#include <cstring>
#include <system_error>

#include <nlohmann/json.hpp>

namespace ecotiter::application {
namespace {

using json = nlohmann::json;

struct CmdName {
  const char* name;
  CommandType type;
};

constexpr CmdName kCmdNames[] = {
  {"fill",              CommandType::Fill},
  {"empty",             CommandType::Empty},
  {"doseVolume",        CommandType::DoseVolume},
  {"rinse",             CommandType::Rinse},
  {"stop",              CommandType::Stop},
  {"emergencyStop",     CommandType::EmergencyStop},
  {"getStatus",         CommandType::GetStatus},
  {"status",            CommandType::GetStatus},
  {"moveSteps",         CommandType::MoveSteps},
  {"setDirection",      CommandType::SetDirection},
  {"setSpeed",          CommandType::SetSpeed},
  {"setAccel",          CommandType::SetAccel},
  {"setVolume",         CommandType::SetVolume},
  {"configMove",        CommandType::ConfigMove},
  {"configHome",        CommandType::ConfigHome},
  {"configSensor",      CommandType::ConfigSensor},
  {"cal.get",           CommandType::CalGet},
  {"cal.calcVolume",    CommandType::CalCalcVolume},
  {"cal.calcSpeed",     CommandType::CalCalcSpeed},
  {"cal.save",          CommandType::CalSave},
  {"cal.reset",         CommandType::CalReset},
  {"cal.run",           CommandType::CalRun},
  {"cal.getResult",     CommandType::CalGetResult},
  {"temperature.read",  CommandType::TempRead},
  {"adc.cal.get",       CommandType::AdcCalGet},
  {"adc.cal.save",      CommandType::AdcCalSave},
  {"stallGuard.get",    CommandType::StallGuardGet},
  {"stallGuard.setThreshold", CommandType::StallGuardSetThreshold},
  {"valve.setPosition", CommandType::ValveSetPosition},
  {"valve.getState",    CommandType::ValveGetState},
  {"system.getStatus",  CommandType::SystemGetStatus},
  {"system.getFormattedLogs", CommandType::SystemGetFormattedLogs},
  {"system.readLog",    CommandType::SystemReadLog},
  {"system.reboot",     CommandType::SystemReboot},
  {"system.firmwareVersion", CommandType::SystemFirmwareVersion},
  {"serial.ping",       CommandType::SerialPing},
  {"ping",              CommandType::SerialPing},
};

constexpr CommandType lookupCmdType(std::string_view name) {
  for (const auto& entry : kCmdNames) {
    if (name == entry.name) return entry.type;
  }
  return static_cast<CommandType>(255); // invalid sentinel
}

[[nodiscard]] std::optional<domain::Direction> parseDirection(
    const json& j, const char* key) {
  auto it = j.find(key);
  if (it == j.end() || !it->is_string()) return std::nullopt;
  auto s = it->get<std::string>();
  if (s == "liq_in") return domain::Direction::LiqIn;
  if (s == "liq_out") return domain::Direction::LiqOut;
  return std::nullopt;
}

[[nodiscard]] std::optional<domain::ValvePosition> parseValvePosition(
    const json& j, const char* key) {
  auto it = j.find(key);
  if (it == j.end() || !it->is_string()) return std::nullopt;
  auto s = it->get<std::string>();
  if (s == "input") return domain::ValvePosition::Input;
  if (s == "output") return domain::ValvePosition::Output;
  return std::nullopt;
}

} // anonymous namespace

std::expected<Command, domain::ProtocolError> parseCommand(
    std::string_view jsonStr) {
  auto j = json::parse(jsonStr, nullptr, false);
  if (j.is_discarded()) {
    return std::unexpected(domain::ProtocolError::InvalidJson);
  }

  auto cmdIt = j.find("cmd");
  if (cmdIt == j.end() || !cmdIt->is_string()) {
    return std::unexpected(domain::ProtocolError::MissingParam);
  }

  auto cmdName = cmdIt->get<std::string>();
  auto type = lookupCmdType(cmdName);
  if (static_cast<uint8_t>(type) == 255) {
    return std::unexpected(domain::ProtocolError::UnknownCommand);
  }

  Command cmd{};
  cmd.type = type;

  // Parse optional parameters
  auto readFloat = [&](const char* key) -> std::optional<domain::Ml> {
    auto it = j.find(key);
    if (it != j.end() && it->is_number()) {
      return domain::Ml{static_cast<float>(it->get<double>())};
    }
    return std::nullopt;
  };

  auto readInt = [&](const char* key) -> std::optional<int32_t> {
    auto it = j.find(key);
    if (it != j.end() && it->is_number_integer()) {
      return it->get<int32_t>();
    }
    return std::nullopt;
  };

  auto readU32 = [&](const char* key) -> std::optional<uint32_t> {
    auto it = j.find(key);
    if (it != j.end() && it->is_number_unsigned()) {
      return it->get<uint32_t>();
    }
    return std::nullopt;
  };

  cmd.volume = readFloat("volume");
  cmd.targetVolume = readFloat("targetVolume");

  auto stepsOpt = readInt("steps");
  if (stepsOpt) {
    cmd.steps = domain::Steps{*stepsOpt};
  }

  cmd.direction = parseDirection(j, "direction");
  cmd.valvePos = parseValvePosition(j, "position");
  cmd.speed = readU32("speed");
  cmd.accel = readU32("accel");
  {
    auto it = j.find("speed_ml_min");
    if (it != j.end() && it->is_number()) {
      cmd.speedMlMin = static_cast<float>(it->get<double>());
    }
  }
  cmd.configMoveSpeed = readU32("moveSpeed");
  cmd.configMoveAccel = readU32("moveAccel");
  cmd.configHomeSpeed = readU32("homeSpeed");
  cmd.configSensorValue = readU32("sensorValue");

  auto sgOpt = readU32("threshold");
  if (sgOpt) {
    cmd.sgThreshold = static_cast<uint8_t>(*sgOpt & 0xFF);
  }

  {
    auto it = j.find("mode");
    if (it != j.end() && it->is_string()) {
      cmd.mode = it->get<std::string>();
    }
  }
  {
    auto it = j.find("freq_hz");
    if (it != j.end() && it->is_number()) {
      cmd.freqHz = static_cast<float>(it->get<double>());
    }
  }

  {
    auto it = j.find("mass_g");
    if (it != j.end() && it->is_number()) {
      cmd.massG = static_cast<float>(it->get<double>());
    }
  }
  {
    auto it = j.find("temp_c");
    if (it != j.end() && it->is_number()) {
      cmd.temperature = static_cast<float>(it->get<double>());
    }
  }
  {
    auto it = j.find("pressure_kpa");
    if (it != j.end() && it->is_number()) {
      cmd.pressure = static_cast<float>(it->get<double>());
    }
  }

  {
    auto it = j.find("measurements");
    if (it != j.end() && it->is_array()) {
      size_t count = 0;
      for (const auto& m : *it) {
        if (count >= Command::MAX_MEASUREMENTS) break;
        if (m.is_object()) {
          auto fIt = m.find("freq_hz");
          auto sIt = m.find("speed_ml_min");
          if (fIt != m.end() && fIt->is_number() &&
              sIt != m.end() && sIt->is_number()) {
            cmd.measurements.freqs[count] = static_cast<float>(fIt->get<double>());
            cmd.measurements.speeds[count] = static_cast<float>(sIt->get<double>());
            ++count;
          }
        }
      }
      cmd.measurements.count = count;
    }
  }

  return cmd;
}

std::expected<size_t, domain::ProtocolError> serializeToBuffer(
    const CommandResponse& rsp, domain::memory::ResponseBuffer& buf) {
  if (rsp.kind == ResponseKind::NoResponse) {
    buf[0] = '\0';
    return size_t{0};
  }
  if (rsp.bodySize > 0 && rsp.bodySize <= buf.size()) {
    std::memcpy(buf.data(), rsp.body.data(), rsp.bodySize);
    if (rsp.bodySize < buf.size()) {
      buf[rsp.bodySize] = '\0';
    }
    return rsp.bodySize;
  }
  // If body is empty but we have a response kind, produce a minimal JSON
  size_t offset = 0;
  switch (rsp.kind) {
    case ResponseKind::Single:
      offset = static_cast<size_t>(
          std::snprintf(buf.data(), buf.size(), "{}"));
      break;
    case ResponseKind::Error:
      offset = static_cast<size_t>(
          std::snprintf(buf.data(), buf.size(), R"({"error":"unknown"})"));
      break;
    case ResponseKind::AckThen:
      offset = static_cast<size_t>(
          std::snprintf(buf.data(), buf.size(), R"({"ack":true})"));
      break;
    default:
      break;
  }
  return offset;
}

CommandResponse makeSingleResponse(std::string_view payload, size_t size) {
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  rsp.bodySize = (std::min)(size, domain::memory::MAX_RSP_SIZE);
  if (rsp.bodySize > 0) {
    std::memcpy(rsp.body.data(), payload.data(), rsp.bodySize);
  }
  return rsp;
}

CommandResponse makeErrorResponse(std::string_view message) {
  CommandResponse rsp;
  rsp.kind = ResponseKind::Error;
  rsp.bodySize = static_cast<size_t>(
      std::snprintf(rsp.body.data(), rsp.body.size(),
                    R"({"error":")"));
  // Append message, escaping minimally
  for (size_t i = 0; i < message.size() && rsp.bodySize < rsp.body.size() - 2; ++i) {
    char c = message[i];
    if (c == '"' || c == '\\') {
      rsp.body[rsp.bodySize++] = '\\';
    }
    rsp.body[rsp.bodySize++] = c;
  }
  if (rsp.bodySize < rsp.body.size() - 1) {
    rsp.body[rsp.bodySize++] = '"';
    rsp.body[rsp.bodySize++] = '}';
  }
  return rsp;
}

CommandResponse makeAckThenResponse() {
  CommandResponse rsp;
  rsp.kind = ResponseKind::AckThen;
  rsp.bodySize = static_cast<size_t>(
      std::snprintf(rsp.body.data(), rsp.body.size(), R"({"ack":true})"));
  return rsp;
}

void appendCmdField(domain::memory::ResponseBuffer& buf, size_t& offset,
                    std::string_view cmdName) {
  if (offset >= buf.size()) return;
  int n = std::snprintf(buf.data() + offset, buf.size() - offset,
                        R"("cmd":"%.*s")",
                        static_cast<int>(cmdName.size()), cmdName.data());
  if (n > 0) offset += static_cast<size_t>(n);
  if (offset >= buf.size()) offset = buf.size() - 1;
}

void serializeStatusJson(domain::memory::ResponseBuffer& buf, size_t& offset,
                         domain::BuretteState state, int32_t tempCX100,
                         domain::ValvePosition valvePos, float mv,
                         domain::Direction dir, uint32_t speed,
                         uint32_t accel, float volumeMl,
                         bool volumeIsNull) {
  constexpr auto S = domain::memory::MAX_RSP_SIZE;
  auto w = [&](const char* fmt, auto... args) {
    if (offset >= S) return;
    int n = std::snprintf(buf.data() + offset, S - offset, fmt, args...);
    if (n > 0) offset += static_cast<size_t>(n);
    if (offset >= S) offset = S - 1;
  };

  const char* stateStr = "";
  switch (state) {
    case domain::BuretteState::Idle:      stateStr = "idle"; break;
    case domain::BuretteState::Homing:    stateStr = "working"; break;
    case domain::BuretteState::Filling:   stateStr = "filling"; break;
    case domain::BuretteState::Emptying:  stateStr = "emptying"; break;
    case domain::BuretteState::Dosing:    stateStr = "dosing"; break;
    case domain::BuretteState::Rinsing:   stateStr = "rinsing"; break;
    case domain::BuretteState::Stopping:  stateStr = "stopping"; break;
    case domain::BuretteState::Error:     stateStr = "error"; break;
  }

  const char* valveStr = (valvePos == domain::ValvePosition::Input) ? "input" : "output";
  const char* dirStr = (dir == domain::Direction::LiqIn) ? "liq_in" : "liq_out";

  float tempC = (tempCX100 > -99999)
      ? static_cast<float>(tempCX100) / 100.0f
      : 0.0f;

  w(R"({"state":"%s","temperature":%.1f,"valve":"%s","mv":%.1f,)",
    stateStr, static_cast<double>(tempC), valveStr, static_cast<double>(mv));
  if (volumeIsNull) {
    w(R"("direction":"%s","speed":%lu,"accel":%lu,"volume":null})",
      dirStr, static_cast<unsigned long>(speed),
      static_cast<unsigned long>(accel));
  } else {
    w(R"("direction":"%s","speed":%lu,"accel":%lu,"volume":%.1f})",
      dirStr, static_cast<unsigned long>(speed),
      static_cast<unsigned long>(accel), static_cast<double>(volumeMl));
  }
}

} // namespace ecotiter::application
