#pragma once

#include <cstdint>
#include <string_view>

#include "domain/memory.hpp"
#include "domain/types.hpp"

namespace ecotiter::interface
{

struct BroadcastEvent
{
    uint32_t tick;
    int32_t tempCX100;
    uint16_t mv;
    domain::ValvePosition vlv;
    domain::BuretteState brt;
    float volumeMl;
    float speedMlMin;
    bool limitFull;
    bool limitEmpty;
    bool usbSerialConnected;
    bool bleConnected;
    bool stepperDrvConnected;
    bool stepperDrvOtpw;
    bool stepperDrvOt;
    uint8_t stallGuardValue;
    bool isStalled;
    uint8_t stallGuardThreshold;
    bool motorIsMoving;
    uint32_t stepsTaken;
};

// Compact broadcast for Serial/BLE (legacy format_status_response_doc)
[[nodiscard]] std::string_view serializeBroadcastCompact(const BroadcastEvent& evt,
                                                         domain::memory::ResponseBuffer& buf);

// Extended broadcast for WebSocket (legacy sse_broadcast_all)
[[nodiscard]] std::string_view serializeBroadcastExtended(const BroadcastEvent& evt,
                                                          domain::memory::ResponseBuffer& buf);

} // namespace ecotiter::interface
