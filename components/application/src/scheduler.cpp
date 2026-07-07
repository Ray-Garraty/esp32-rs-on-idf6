#include "application/scheduler.hpp"

namespace ecotiter::application {

void TickScheduler::tick() noexcept {
  gTick.fetch_add(1, std::memory_order_relaxed);
}

bool TickScheduler::shouldBroadcast() const noexcept {
  uint32_t now = gTick.load(std::memory_order_relaxed);
  if (now - lastBroadcastTick_ >= 30) {
    const_cast<TickScheduler*>(this)->lastBroadcastTick_ = now;
    return true;
  }
  return false;
}

bool TickScheduler::shouldSample() const noexcept {
  uint32_t now = gTick.load(std::memory_order_relaxed);
  if (now - lastSampleTick_ >= 10) {
    const_cast<TickScheduler*>(this)->lastSampleTick_ = now;
    return true;
  }
  return false;
}

bool TickScheduler::shouldCheckWatermarks() const noexcept {
  uint32_t now = gTick.load(std::memory_order_relaxed);
  if (now - lastWatermarkCheckTick_ >= 100) {
    const_cast<TickScheduler*>(this)->lastWatermarkCheckTick_ = now;
    return true;
  }
  return false;
}

bool TickScheduler::shouldMaintain() const noexcept {
  uint32_t now = gTick.load(std::memory_order_relaxed);
  if (now - lastMaintainTick_ >= 6000) {
    const_cast<TickScheduler*>(this)->lastMaintainTick_ = now;
    return true;
  }
  return false;
}

} // namespace ecotiter::application
