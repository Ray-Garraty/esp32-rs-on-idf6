#pragma once

#include <cstdint>
#include <string_view>

#include "domain/memory.hpp"
#include "domain/types.hpp"

namespace ecotiter::interface {

struct BroadcastEvent {
    uint32_t tick;
    int32_t tempCX100;
    uint16_t mv;
    domain::ValvePosition vlv;
    domain::BuretteState brt;
    float volumeMl;
    float speedMlMin;
    bool limitFull;
    bool limitEmpty;
};

// Serialize BroadcastEvent to a pre-allocated JSON buffer.
// Returns a string_view into buf, or empty view on truncation.
[[nodiscard]] std::string_view serializeBroadcast(
    const BroadcastEvent& evt,
    domain::memory::ResponseBuffer& buf);

} // namespace ecotiter::interface
