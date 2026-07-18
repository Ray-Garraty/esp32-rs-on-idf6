#include "interface/broadcast.hpp"

#include <cstdio>
#include <cstring>

namespace ecotiter::interface
{

namespace
{

const char* valveStr(domain::ValvePosition v)
{
    switch (v)
    {
    case domain::ValvePosition::Input:
        return "in";
    case domain::ValvePosition::Output:
        return "out";
    default:
        return "unk";
    }
}

const char* brtStsStr(domain::BuretteState s)
{
    switch (s)
    {
    case domain::BuretteState::Error:
        return "error";
    case domain::BuretteState::Homing:
    case domain::BuretteState::Filling:
    case domain::BuretteState::Emptying:
    case domain::BuretteState::Dosing:
    case domain::BuretteState::Rinsing:
    case domain::BuretteState::Stopping:
        return "working";
    default:
        return "idle";
    }
}

} // anonymous namespace

std::string_view serializeBroadcastCompact(const BroadcastEvent& evt,
                                           domain::memory::ResponseBuffer& buf)
{

    char tempBuf[32];
    const char* tempStr;
    if (evt.tempCX100 > -99999)
    {
        std::snprintf(tempBuf, sizeof(tempBuf), "%.1f", static_cast<double>(evt.tempCX100) / 100.0);
        tempStr = tempBuf;
    }
    else
    {
        tempStr = "null";
    }

    const char* vlStr;
    char vlBuf[32];
    if (evt.brt == domain::BuretteState::Homing)
    {
        vlStr = "null";
    }
    else
    {
        std::snprintf(vlBuf, sizeof(vlBuf), "%.2f", static_cast<double>(evt.volumeMl));
        vlStr = vlBuf;
    }

    int n = std::snprintf(buf.data(), buf.size(),
                          R"({"ts":%lu,"temp":%s,"mv":%.1f,"vlv":"%s",)"
                          R"("brt":{"sts":"%s","vl":%s,"spd":%.2f})"
                          R"(})",
                          static_cast<unsigned long>(evt.tick), tempStr,
                          static_cast<double>(evt.mv), valveStr(evt.vlv), brtStsStr(evt.brt), vlStr,
                          static_cast<double>(evt.speedMlMin));

    if (n < 0 || static_cast<size_t>(n) >= buf.size())
    {
        return {};
    }
    return std::string_view(buf.data(), static_cast<size_t>(n));
}

std::string_view serializeBroadcastExtended(const BroadcastEvent& evt,
                                            domain::memory::ResponseBuffer& buf)
{

    char tempBuf[32];
    const char* tempStr;
    if (evt.tempCX100 > -99999)
    {
        std::snprintf(tempBuf, sizeof(tempBuf), "%.1f", static_cast<double>(evt.tempCX100) / 100.0);
        tempStr = tempBuf;
    }
    else
    {
        tempStr = "null";
    }

    const char* vlStr;
    char vlBuf[32];
    if (evt.brt == domain::BuretteState::Homing)
    {
        vlStr = "null";
    }
    else
    {
        std::snprintf(vlBuf, sizeof(vlBuf), "%.2f", static_cast<double>(evt.volumeMl));
        vlStr = vlBuf;
    }

    int n = std::snprintf(
        buf.data(), buf.size(),
        R"({"ts":%lu,"temp":%s,"mv":%.1f,"vlv":"%s",)"
        R"("brt":{"sts":"%s","vl":%s,"spd":%.2f},)"
        R"("limitSwitch":{"full":%s,"empty":%s},)"
        R"("usbSerialConnected":%s,"bleConnected":%s,)"
        R"("stepperDrv":{"isConnected":%s,"otpw":%s,"ot":%s,)"
        R"("motor":{"stallGuard":{"value":%u,"isStalled":%s,"threshold":%u},"isMoving":%s}},)"
        R"("buretteSteps":{"taken":%lu})"
        R"(})",
        static_cast<unsigned long>(evt.tick), tempStr, static_cast<double>(evt.mv),
        valveStr(evt.vlv), brtStsStr(evt.brt), vlStr, static_cast<double>(evt.speedMlMin),
        evt.limitFull ? "true" : "false", evt.limitEmpty ? "true" : "false",
        evt.usbSerialConnected ? "true" : "false", evt.bleConnected ? "true" : "false",
        evt.stepperDrvConnected ? "true" : "false", evt.stepperDrvOtpw ? "true" : "false",
        evt.stepperDrvOt ? "true" : "false", static_cast<unsigned>(evt.stallGuardValue),
        evt.isStalled ? "true" : "false", static_cast<unsigned>(evt.stallGuardThreshold),
        evt.motorIsMoving ? "true" : "false", static_cast<unsigned long>(evt.stepsTaken));

    if (n < 0 || static_cast<size_t>(n) >= buf.size())
    {
        return {};
    }
    return std::string_view(buf.data(), static_cast<size_t>(n));
}

} // namespace ecotiter::interface
