#pragma once

#include <atomic>
#include <cstdint>

namespace ecotiter::application {

// Global monotonic tick counter — incremented by main loop pacing tick (10ms)
inline std::atomic<uint32_t> gTick{0};

class TickScheduler {
public:
  // Called once per main loop iteration (every 10ms)
  void tick() noexcept;

  // Returns true every 30 ticks (300ms) — for status broadcasts
  [[nodiscard]] bool shouldBroadcast() const noexcept;

  // Returns true every 10 ticks (100ms) — for sensor sampling
  [[nodiscard]] bool shouldSample() const noexcept;

  // Returns true every 100 ticks (1000ms) — for stack watermark checks
  [[nodiscard]] bool shouldCheckWatermarks() const noexcept;

  // Returns true every 6000 ticks (60s) — for periodic NVS flush / maintenance
  [[nodiscard]] bool shouldMaintain() const noexcept;

private:
  uint32_t lastBroadcastTick_{0};
  uint32_t lastSampleTick_{0};
  uint32_t lastWatermarkCheckTick_{0};
  uint32_t lastMaintainTick_{0};
};

} // namespace ecotiter::application
