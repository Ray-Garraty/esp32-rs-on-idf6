#pragma once

#include <atomic>
#include <cstdint>

namespace ecotiter::application
{

// Global monotonic tick counter — incremented by main loop pacing tick (10ms)
// At FREERTOS_HZ=1000, 1 tick = 1ms, but main loop advances by 10 per iteration.
inline std::atomic<uint32_t> gTick{0};

class TickScheduler
{
public:
    // Tick intervals (at 10ms per increment):
    static constexpr uint32_t BROADCAST_INTERVAL = 30; // 300ms (30 ticks × 10ms)

    void tick() noexcept;

    [[nodiscard]] bool shouldBroadcast() const noexcept;

private:
    uint32_t lastBroadcastTick_{0};
};

} // namespace ecotiter::application
