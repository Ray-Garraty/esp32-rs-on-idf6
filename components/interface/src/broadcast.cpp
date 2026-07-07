#include "interface/broadcast.hpp"

#include <cstdio>
#include <cstring>

namespace ecotiter::interface {

namespace {

const char* valveStr(domain::ValvePosition v) {
    return (v == domain::ValvePosition::Input) ? "input" : "output";
}

const char* buretteStr(domain::BuretteState s) {
    switch (s) {
        case domain::BuretteState::Idle:     return "idle";
        case domain::BuretteState::Homing:   return "homing";
        case domain::BuretteState::Filling:  return "filling";
        case domain::BuretteState::Emptying: return "emptying";
        case domain::BuretteState::Dosing:   return "dosing";
        case domain::BuretteState::Rinsing:  return "rinsing";
        case domain::BuretteState::Stopping: return "stopping";
        case domain::BuretteState::Error:    return "error";
    }
    return "unknown";
}

const char* dirStr(domain::Direction d) {
    return (d == domain::Direction::Cw) ? "cw" : "ccw";
}

} // anonymous namespace

std::string_view serializeBroadcast(
    const BroadcastEvent& evt,
    domain::memory::ResponseBuffer& buf) {

    int n = std::snprintf(buf.data(), buf.size(),
        R"({"t":%lu,"temp":%ld,"mv":%u,"vlv":"%s","brt":"%s",)"
        R"("dir":"%s","spd":%lu,"acc":%lu,"vol":%.1f,"steps":%lu})",
        static_cast<unsigned long>(evt.tick),
        static_cast<long>(evt.tempCX100),
        static_cast<unsigned>(evt.mv),
        valveStr(evt.vlv),
        buretteStr(evt.brt),
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
