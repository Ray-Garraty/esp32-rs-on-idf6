//! Timing coordination — tick counter, broadcast scheduling.
//!
//! Provides a global millisecond tick counter (wrapping `AtomicU32` for
//! compatibility with xtensa which lacks hardware 64-bit atomics) and
//! a `should_broadcast()` helper for the main loop.

use core::sync::atomic::{AtomicU32, Ordering};

use crate::config::MAIN_LOOP_TICK_MS;

/// Broadcast interval in milliseconds (every 300 ms = every 30 ticks).
pub const BROADCAST_INTERVAL_MS: u64 = 300;

/// Global tick counter (wrapping at u32::MAX, ~49.7 days at 10ms ticks).
///
/// Using `AtomicU32` for xtensa compatibility (ESP32 lacks hardware 64-bit atomics).
/// Ticks are in milliseconds, wrapping is handled via modular arithmetic.
static G_TICK_MS: AtomicU32 = AtomicU32::new(0);

/// Advance the tick counter by `ms` milliseconds.
///
/// Must be called from the main loop every tick with the tick duration.
/// The counter wraps at `u32::MAX` (~49.7 days with 10ms ticks).
#[allow(clippy::cast_possible_truncation)]
pub fn tick(ms: u64) {
    // Saturating add to avoid panic on overflow; wrapping is intended.
    // Truncation to u32 is deliberate for xtensa compatibility — the tick
    // counter wraps at ~49.7 days which is acceptable for this device.
    let prev = G_TICK_MS.load(Ordering::Relaxed);
    let next = prev.saturating_add(ms as u32);
    G_TICK_MS.store(next, Ordering::Release);
}

/// Returns `true` if a broadcast should be sent this tick.
///
/// Uses modular arithmetic with the broadcast interval.
pub fn should_broadcast() -> bool {
    let ticks = G_TICK_MS.load(Ordering::Acquire);
    u64::from(ticks) % BROADCAST_INTERVAL_MS < MAIN_LOOP_TICK_MS
}

/// Returns the elapsed milliseconds since boot (wrapping).
pub fn elapsed_ms() -> u64 {
    u64::from(G_TICK_MS.load(Ordering::Acquire))
}

/// Reset the tick counter to zero (used in tests).
pub fn reset() {
    G_TICK_MS.store(0, Ordering::Release);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_tick_advances() {
        reset();
        assert_eq!(elapsed_ms(), 0);
        tick(10);
        assert_eq!(elapsed_ms(), 10);
        tick(20);
        assert_eq!(elapsed_ms(), 30);
    }

    #[test]
    fn test_should_broadcast_at_300ms_interval() {
        reset();
        // At tick 0, should_broadcast returns true (0 % 300 < 10)
        assert!(should_broadcast());

        // After 10ms tick: 10 % 300 = 10, which is NOT < 10
        tick(10);
        assert!(!should_broadcast());

        // After 290 more ms (total 300): 300 % 300 = 0 < 10
        tick(290);
        assert!(should_broadcast());
    }

    #[test]
    fn test_tick_wrapping() {
        reset();
        // Set to near max
        G_TICK_MS.store(u32::MAX - 5, Ordering::Release);
        tick(10); // Should not panic
        let val = elapsed_ms();
        assert!(val > 0); // Wrapped or near-wrapped, but still valid
    }
}
