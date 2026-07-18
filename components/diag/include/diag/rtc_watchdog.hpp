#pragma once

#include <cstdint>
#include "esp_err.h"
#include "hal/wdt_hal.h"

namespace ecotiter::diag
{

// RAII wrapper for the RTC Watchdog Timer (RWDT) on ESP32-S3.
//
// RWDT is the ONLY watchdog that fires when interrupts are disabled
// (spinlock deadlock, PHY calibration hang). It uses the independent
// RTC slow clock and does NOT require CPU interrupts to operate.
//
// Architecture:
//   IWDT (500ms) → panic handler → backtrace + BlackBox dump
//     ↓ (if panic handler itself crashes / can't run)
//   RWDT (6s) → RESET_SYSTEM → reboot
//
// Together with IWDT (LL-031/LL-032 fix): every deadlock produces either
// a crash dump with full backtrace (IWDT) or at minimum a reboot with
// rst:0x10 (RTCWDT_SYS_RESET) in the boot log.
//
// Usage:
//   RtcWatchdog wdt;  // init + configure at boot
//   wdt.feed();        // call from main loop every < 6s
//   // ~RtcWatchdog()  // deinit on destruction
//
class RtcWatchdog
{
public:
    RtcWatchdog() noexcept;
    ~RtcWatchdog() noexcept;

    RtcWatchdog(const RtcWatchdog&) = delete;
    RtcWatchdog& operator=(const RtcWatchdog&) = delete;
    RtcWatchdog(RtcWatchdog&&) = delete;
    RtcWatchdog& operator=(RtcWatchdog&&) = delete;

    // Feed the RWDT (reset countdown). Must be called at least once
    // every 6 seconds. Call from main loop every 10ms tick.
    void feed() noexcept;

    // True if init succeeded and RWDT is running
    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }

private:
    bool enabled_{false};
    wdt_hal_context_t hal_;
};

// Global pointer for cross-task RWDT access (motor task feeds during long SMs)
// Set by app_main() after construction.
extern RtcWatchdog* gRtcWdt;

} // namespace ecotiter::diag
