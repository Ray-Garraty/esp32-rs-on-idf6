#include "interface/broadcast.hpp"

#include <cstdio>
#include <cstring>

#include "domain/calibration.hpp"

namespace ecotiter::interface {

namespace {

const char* valveStr(domain::ValvePosition v) {
    return (v == domain::ValvePosition::Input) ? "in" : "out";
}

const char* brtStsStr(domain::BuretteState s) {
    switch (s) {
        case domain::BuretteState::Error:    return "error";
        case domain::BuretteState::Homing:
        case domain::BuretteState::Filling:
        case domain::BuretteState::Emptying:
        case domain::BuretteState::Dosing:
        case domain::BuretteState::Rinsing:
        case domain::BuretteState::Stopping: return "working";
        default:                             return "idle";
    }
}

const char* dirStr(domain::Direction d) {
    return (d == domain::Direction::Cw) ? "cw" : "ccw";
}

} // anonymous namespace

std::string_view serializeBroadcast(
    const BroadcastEvent& evt,
    domain::memory::ResponseBuffer& buf) {

    char tempBuf[32];
    const char* tempStr;
    if (evt.tempCX100 > -99999) {
        std::snprintf(tempBuf, sizeof(tempBuf), "%.1f",
            static_cast<double>(evt.tempCX100) / 100.0);
        tempStr = tempBuf;
    } else {
        tempStr = "null";
    }

    const char* vlStr;
    char vlBuf[32];
    if (evt.brt == domain::BuretteState::Homing) {
        vlStr = "null";
    } else {
        std::snprintf(vlBuf, sizeof(vlBuf), "%.1f",
            static_cast<double>(evt.volumeMl));
        vlStr = vlBuf;
    }

    double spdMlMin = static_cast<double>(evt.speed) * 60.0
        / static_cast<double>(domain::CalibrationData::kDefaultStepsPerMl);

    int n = std::snprintf(buf.data(), buf.size(),
        R"({"t":%lu,"temp":%s,"mv":%u,"vlv":"%s",)"
        R"("brt":{"sts":"%s","vl":%s,"spd":%.1f},)"
        R"("dir":"%s","spd":%lu,"acc":%lu,"vol":%.1f,"steps":%lu})",
        static_cast<unsigned long>(evt.tick),
        tempStr,
        static_cast<unsigned>(evt.mv),
        valveStr(evt.vlv),
        brtStsStr(evt.brt),
        vlStr,
        spdMlMin,
        dirStr(evt.dir),
        static_cast<unsigned long>(evt.speed),
        static_cast<unsigned long>(evt.accel),
        static_cast<double>(evt.volumeMl),
        static_cast<unsigned long>(evt.dispensedSteps));

    if (n < 0 || static_cast<size_t>(n) >= buf.size()) {
        // Truncation or error — return empty view
        return {};
    }
    return std::string_view(buf.data(), static_cast<size_t>(n));
}

} // namespace ecotiter::interface
