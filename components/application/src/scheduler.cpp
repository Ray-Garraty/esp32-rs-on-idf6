#include "application/scheduler.hpp"

namespace ecotiter::application
{

void TickScheduler::tick() noexcept
{
    gTick.fetch_add(1, std::memory_order_relaxed);
}

bool TickScheduler::shouldBroadcast() const noexcept
{
    uint32_t now = gTick.load(std::memory_order_relaxed);
    if (now - lastBroadcastTick_ >= BROADCAST_INTERVAL)
    {
        const_cast<TickScheduler*>(this)->lastBroadcastTick_ = now;
        return true;
    }
    return false;
}

} // namespace ecotiter::application
