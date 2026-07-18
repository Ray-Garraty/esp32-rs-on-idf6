#include "application/command.hpp"

#include <cctype>
#include <cstring>
#include <string>
#include <system_error>

#include <nlohmann/json.hpp>

namespace ecotiter::application
{
namespace
{

using json = nlohmann::json;

struct CmdName
{
    const char* name;
    CommandType type;
};

constexpr CmdName kCmdNames[] = {
    {"burette.fill", CommandType::Fill},
    {"burette.empty", CommandType::Empty},
    {"burette.doseVolume", CommandType::DoseVolume},
    {"burette.rinse", CommandType::Rinse},
    {"burette.stop", CommandType::Stop},
    {"burette.emergencyStop", CommandType::EmergencyStop},
    {"burette.getStatus", CommandType::GetStatus},
    {"burette.status", CommandType::GetStatus},
    {"burette.moveSteps", CommandType::MoveSteps},
    {"burette.setDirection", CommandType::SetDirection},
    {"burette.setSpeed", CommandType::SetSpeed},
    {"burette.setAccel", CommandType::SetAccel},
    {"burette.cal.get", CommandType::CalGet},
    {"burette.cal.calcVolume", CommandType::CalCalcVolume},
    {"burette.cal.calcSpeed", CommandType::CalCalcSpeed},
    {"burette.cal.save", CommandType::CalSave},
    {"burette.cal.reset", CommandType::CalReset},
    {"burette.cal.run", CommandType::CalRun},
    {"burette.cal.getResult", CommandType::CalGetResult},
    {"burette.cal.runSpeedSeq", CommandType::CalRunSpeedSeq},
    {"burette.moveToStop", CommandType::MoveToStop},
    {"temperature.read", CommandType::TempRead},
    {"adc.cal.get", CommandType::AdcCalGet},
    {"adc.cal.save", CommandType::AdcCalSave},
    {"adc.cal.measure", CommandType::AdcCalMeasure},
    {"adc.cal.compute", CommandType::AdcCalCompute},
    {"adc.cal.reset", CommandType::AdcCalReset},
    {"stallGuard.get", CommandType::StallGuardGet},
    {"stallGuard.setThreshold", CommandType::StallGuardSetThreshold},
    {"valve.setPosition", CommandType::ValveSetPosition},
    {"valve.getState", CommandType::ValveGetState},
    {"system.getStatus", CommandType::SystemGetStatus},
    {"system.getFormattedLogs", CommandType::SystemGetFormattedLogs},
    {"system.readLog", CommandType::SystemReadLog},
    {"system.firmwareVersion", CommandType::SystemFirmwareVersion},
    {"serial.ping", CommandType::SerialPing},
    {"ping", CommandType::SerialPing},
};

constexpr CommandType lookupCmdType(std::string_view name)
{
    for (const auto& entry : kCmdNames)
    {
        if (name == entry.name)
            return entry.type;
    }
    return static_cast<CommandType>(255); // invalid sentinel
}

[[nodiscard]] std::optional<domain::Direction> parseDirection(const json& j, const char* key)
{
    auto it = j.find(key);
    if (it == j.end() || !it->is_string())
        return std::nullopt;
    auto s = it->get<std::string>();
    if (s.size() < 6)
        return std::nullopt;
    char prefix[4]{};
    std::copy_n(s.data(), 3, prefix);
    for (auto& c : prefix)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (std::string_view(prefix, 3) == "liq")
    {
        char suffix[4]{};
        std::copy_n(s.data() + 4, 3, suffix);
        for (auto& c : suffix)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (std::string_view(suffix, 2) == "in")
            return domain::Direction::LiqIn;
        if (std::string_view(suffix, 3) == "out")
            return domain::Direction::LiqOut;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<domain::ValvePosition> parseValvePosition(const json& j,
                                                                      const char* key)
{
    auto it = j.find(key);
    if (it == j.end() || !it->is_string())
        return std::nullopt;
    auto s = it->get<std::string>();
    if (s == "input")
        return domain::ValvePosition::Input;
    if (s == "output")
        return domain::ValvePosition::Output;
    return std::nullopt;
}

} // anonymous namespace

std::expected<Command, domain::ProtocolError>
parseCommand(
    std::string_view jsonStr)
{
    auto j = json::parse(jsonStr, nullptr, false);
    if (j.is_discarded())
    {
        return std::unexpected(domain::ProtocolError::InvalidJson);
    }

    auto cmdIt = j.find("cmd");
    if (cmdIt == j.end() || !cmdIt->is_string())
    {
        return std::unexpected(domain::ProtocolError::MissingParam);
    }

    auto cmdName = cmdIt->get<std::string>();
    auto type = lookupCmdType(cmdName);
    if (static_cast<uint8_t>(type) == 255)
    {
        return std::unexpected(domain::ProtocolError::UnknownCommand);
    }

    Command cmd{};
    cmd.type = type;

    {
        auto it = j.find("id");
        if (it != j.end() && it->is_number_unsigned())
        {
            cmd.id = it->get<uint64_t>();
        }
    }

    // Parse optional parameters
    auto readFloat = [&](const char* key) -> std::optional<domain::Ml> {
        auto it = j.find(key);
        if (it != j.end() && it->is_number())
        {
            return domain::Ml{static_cast<float>(it->get<double>())};
        }
        return std::nullopt;
    };

    auto readInt = [&](const char* key) -> std::optional<int32_t> {
        auto it = j.find(key);
        if (it != j.end() && it->is_number_integer())
        {
            return it->get<int32_t>();
        }
        return std::nullopt;
    };

    auto readU32 = [&](const char* key) -> std::optional<uint32_t> {
        auto it = j.find(key);
        if (it != j.end() && it->is_number_unsigned())
        {
            return it->get<uint32_t>();
        }
        return std::nullopt;
    };

    cmd.volume = readFloat("volume");
    cmd.targetVolume = readFloat("targetVolume");

    auto stepsOpt = readInt("steps");
    if (stepsOpt)
    {
        cmd.steps = domain::Steps{*stepsOpt};
    }

    cmd.direction = parseDirection(j, "direction");
    cmd.valvePos = parseValvePosition(j, "position");
    cmd.speed = readU32("speed");
    cmd.accel = readU32("accel");
    {
        auto it = j.find("speed_ml_min");
        if (it != j.end() && it->is_number())
        {
            cmd.speedMlMin = static_cast<float>(it->get<double>());
        }
    }

    auto sgOpt = readU32("threshold");
    if (sgOpt)
    {
        cmd.sgThreshold = static_cast<uint8_t>(*sgOpt & 0xFF);
    }

    {
        auto it = j.find("mode");
        if (it != j.end() && it->is_string())
        {
            cmd.mode = it->get<std::string>();
        }
    }
    {
        auto it = j.find("freq_hz");
        if (it != j.end() && it->is_number())
        {
            cmd.freqHz = static_cast<float>(it->get<double>());
        }
    }
    {
        auto it = j.find("ref_mv");
        if (it != j.end() && it->is_number())
        {
            cmd.refMv = static_cast<float>(it->get<double>());
        }
    }

    {
        auto it = j.find("mass_g");
        if (it != j.end() && it->is_number())
        {
            cmd.massG = static_cast<float>(it->get<double>());
        }
    }
    {
        auto it = j.find("temp_c");
        if (it != j.end() && it->is_number())
        {
            cmd.temperature = static_cast<float>(it->get<double>());
        }
    }
    {
        auto it = j.find("pressure_kpa");
        if (it != j.end() && it->is_number())
        {
            cmd.pressure = static_cast<float>(it->get<double>());
        }
    }

    {
        // For burette.cal.runSpeedSeq: flat freqs array
        auto freqsIt = j.find("freqs");
        if (freqsIt != j.end() && freqsIt->is_array())
        {
            size_t count = 0;
            for (const auto& v : *freqsIt)
            {
                if (count >= Command::MAX_MEASUREMENTS)
                    break;
                if (v.is_number())
                {
                    cmd.freqsArray[count] = static_cast<float>(v.get<double>());
                    ++count;
                }
            }
            cmd.freqsCount = count;
        }
    }
    {
        auto it = j.find("measurements");
        if (it != j.end() && it->is_array())
        {
            size_t count = 0;
            for (const auto& m : *it)
            {
                if (count >= Command::MAX_MEASUREMENTS)
                    break;
                if (m.is_object())
                {
                    auto fIt = m.find("freq_hz");
                    auto sIt = m.find("speed_ml_min");
                    if (fIt != m.end() && fIt->is_number() && sIt != m.end() && sIt->is_number())
                    {
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

std::expected<size_t, domain::ProtocolError> serializeToBuffer(const CommandResponse& rsp,
                                                               domain::memory::ResponseBuffer& buf)
{
    if (rsp.kind == ResponseKind::NoResponse)
    {
        buf[0] = '\0';
        return size_t{0};
    }
    if (rsp.bodySize > 0 && rsp.bodySize <= buf.size())
    {
        // Inject id into the body: insert "id":N, after the opening {
        if (rsp.id != 0 && rsp.body[0] == '{')
        {
            char idStr[32];
            int idLen = std::snprintf(idStr, sizeof(idStr), R"({"id":%llu,)",
                                      static_cast<unsigned long long>(rsp.id));
            size_t payloadSize = rsp.bodySize;
            size_t totalSize = static_cast<size_t>(idLen) + payloadSize - 1;
            if (totalSize < buf.size())
            {
                // Build: {"id":N,<rest_of_body_without_first_brace>
                std::memcpy(buf.data(), idStr, static_cast<size_t>(idLen));
                std::memcpy(buf.data() + idLen, rsp.body.data() + 1, payloadSize - 1);
                if (totalSize < buf.size())
                {
                    buf[totalSize] = '\0';
                }
                return totalSize;
            }
        }
        // Fallback: copy as-is
        std::memcpy(buf.data(), rsp.body.data(), rsp.bodySize);
        if (rsp.bodySize < buf.size())
        {
            buf[rsp.bodySize] = '\0';
        }
        return rsp.bodySize;
    }
    // If body is empty but we have a response kind, produce a minimal JSON
    size_t offset = 0;
    switch (rsp.kind)
    {
    case ResponseKind::Single:
        offset = static_cast<size_t>(std::snprintf(buf.data(), buf.size(), "{}"));
        break;
    case ResponseKind::Error:
        offset = static_cast<size_t>(
            std::snprintf(buf.data(), buf.size(), R"({"status":"error","message":"unknown"})"));
        break;
    case ResponseKind::AckThen:
        offset = static_cast<size_t>(std::snprintf(
            buf.data(), buf.size(), R"({"status":"ok","data":{"status":"accepted"}})"));
        break;
    default:
        break;
    }
    return offset;
}

CommandResponse makeSingleResponse(std::string_view payload, size_t size)
{
    CommandResponse rsp;
    rsp.kind = ResponseKind::Single;
    rsp.bodySize = static_cast<size_t>(std::snprintf(rsp.body.data(), rsp.body.size(),
                                                     R"({"status":"ok","data":%.*s})",
                                                     static_cast<int>(size),
                                                     // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage) // reason: %.*s with explicit precision does not require null termination
                                                     payload.data()));
    return rsp;
}

CommandResponse makeErrorResponse(std::string_view message)
{
    CommandResponse rsp;
    rsp.kind = ResponseKind::Error;
    rsp.bodySize = static_cast<size_t>(
        std::snprintf(rsp.body.data(), rsp.body.size(), R"({"status":"error","message":")"));
    for (size_t i = 0; i < message.size() && rsp.bodySize < rsp.body.size() - 2; ++i)
    {
        char c = message[i];
        if (c == '"' || c == '\\')
        {
            rsp.body[rsp.bodySize++] = '\\';
        }
        rsp.body[rsp.bodySize++] = c;
    }
    if (rsp.bodySize < rsp.body.size() - 1)
    {
        rsp.body[rsp.bodySize++] = '"';
        rsp.body[rsp.bodySize++] = '}';
    }
    return rsp;
}

CommandResponse makeAckThenResponse()
{
    CommandResponse rsp;
    rsp.kind = ResponseKind::AckThen;
    rsp.bodySize = static_cast<size_t>(std::snprintf(
        rsp.body.data(), rsp.body.size(), R"({"status":"ok","data":{"status":"accepted"}})"));
    return rsp;
}

void serializeStatusJson(domain::memory::ResponseBuffer& buf, size_t& offset,
                         domain::BuretteState state, int32_t tempCX100,
                         domain::ValvePosition valvePos, float mv, domain::Direction dir,
                         uint32_t speed, uint32_t accel, float volumeMl, bool volumeIsNull)
{
    constexpr auto S = domain::memory::MAX_RSP_SIZE;
    auto w = [&](const char* fmt, auto... args) {
        if (offset >= S)
            return;
        int n = std::snprintf(buf.data() + offset, S - offset, fmt, args...);
        if (n > 0)
            offset += static_cast<size_t>(n);
        if (offset >= S)
            offset = S - 1;
    };

    const char* stateStr = domain::buretteStateStr(state);
    if (state == domain::BuretteState::Homing)
        stateStr = "working";

    (void)valvePos;
    (void)dir;
    (void)tempCX100;

    if (volumeIsNull)
    {
        w(R"("status":"%s","volume_ml":null,"speed_ml_min":%.1f)", stateStr,
          static_cast<double>(0.0));
    }
    else
    {
        w(R"("status":"%s","volume_ml":%.2f,"speed_ml_min":%.1f)", stateStr,
          static_cast<double>(volumeMl), static_cast<double>(0.0));
    }
}

CommandResponse makeStatusResponse(uint64_t id, domain::BuretteState state, int32_t tempCX100,
                                   domain::ValvePosition valvePos, float mv, domain::Direction dir,
                                   uint32_t speed, uint32_t accel, float volumeMl,
                                   bool volumeIsNull)
{
    CommandResponse rsp;
    rsp.kind = ResponseKind::Single;
    rsp.id = id;
    size_t off = 0;
    off = static_cast<size_t>(
        std::snprintf(rsp.body.data(), rsp.body.size(), R"({"status":"ok","data":{)"));
    serializeStatusJson(rsp.body, off, state, tempCX100, valvePos, mv, dir, speed, accel, volumeMl,
                        volumeIsNull);
    if (off < rsp.body.size() - 1)
    {
        rsp.body[off++] = '}';
        rsp.body[off++] = '}';
    }
    if (off < rsp.body.size())
    {
        rsp.body[off] = '\0';
    }
    rsp.bodySize = off;
    return rsp;
}

} // namespace ecotiter::application
