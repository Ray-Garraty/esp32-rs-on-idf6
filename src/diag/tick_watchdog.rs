//! Main loop tick watchdog — detects blocking calls (GR-1).
//!
//! Wraps the main loop body with `tick_begin()` / `tick_end()`.
//! If the elapsed time exceeds thresholds, logs a warning or error
//! and records a `DiagEvent::TickOverrun` in the black box.

use core::sync::atomic::{AtomicU32, Ordering};

use super::black_box;
use super::black_box::DiagEvent;

const EXPECTED_TICK_MS: u16 = 10;

/// Warn if main loop takes >50 ms (5× expected).
const WARN_THRESHOLD_US: u32 = 50_000;
/// Critical if main loop takes >500 ms (50× expected) — e.g., blocking call.
const CRITICAL_THRESHOLD_US: u32 = 500_000;

static LAST_TICK_US: AtomicU32 = AtomicU32::new(0);

/// Call at the START of each main loop iteration.
#[allow(clippy::cast_possible_truncation, clippy::cast_sign_loss)]
pub fn tick_begin() {
    let now = u32::try_from(crate::esp_safe::micros()).unwrap_or(0);
    LAST_TICK_US.store(now, Ordering::Release);
}

/// Call at the END of each main loop iteration (after all work, before sleep).
/// Detects overruns and records diagnostic events.
#[allow(clippy::cast_possible_truncation, clippy::cast_sign_loss)]
pub fn tick_end() {
    let now = u32::try_from(crate::esp_safe::micros()).unwrap_or(0);
    let start = LAST_TICK_US.load(Ordering::Acquire);
    let elapsed_us = now.wrapping_sub(start);
    let elapsed_ms = u16::try_from(elapsed_us / 1000).unwrap_or(u16::MAX);

    if elapsed_us > CRITICAL_THRESHOLD_US {
        black_box::record(DiagEvent::TickOverrun {
            expected_ms: EXPECTED_TICK_MS,
            actual_ms: elapsed_ms,
        });
        log::error!(
            "CRITICAL: Main loop blocked for {elapsed_ms}ms (expected {EXPECTED_TICK_MS}ms)!",
        );
    } else if elapsed_us > WARN_THRESHOLD_US {
        black_box::record(DiagEvent::TickOverrun {
            expected_ms: EXPECTED_TICK_MS,
            actual_ms: elapsed_ms,
        });
        log::warn!("Main loop slow: {elapsed_ms}ms (expected {EXPECTED_TICK_MS}ms)");
    }
}
