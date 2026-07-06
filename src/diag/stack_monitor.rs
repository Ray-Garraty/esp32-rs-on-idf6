//! Stack watermark monitor for all known threads.
//!
//! Uses a fixed-size static array of `ThreadInfo` (no heap, no mutex).
//! Each thread registers at creation with a compile-time slot ID.
//! Periodic `check_watermark()` logs warnings when stack is low.

use core::sync::atomic::{AtomicBool, AtomicU16, Ordering};
use std::sync::OnceLock;

use super::black_box;
use super::black_box::DiagEvent;

// ── Thread slot IDs (must match diag::constants) ────────────────
pub const MAIN: u8 = 0;
pub const MOTOR: u8 = 1;
pub const TEMP: u8 = 2;
pub const UART: u8 = 3;
pub const NET_OWNER: u8 = 4;
pub const BLE_NOTIFY: u8 = 5;
// Reserved: 6, 7 for future threads

const THREAD_COUNT: usize = 8;

const WARN_WATERMARK: u16 = 1024;
const CRITICAL_WATERMARK: u16 = 512;

struct ThreadInfo {
    name: OnceLock<&'static str>,
    min_watermark: AtomicU16,
    registered: AtomicBool,
}

/// Fixed-size registry — all threads known at compile time.
#[allow(clippy::declare_interior_mutable_const)]
static THREADS: [ThreadInfo; THREAD_COUNT] = {
    const EMPTY: ThreadInfo = ThreadInfo {
        name: OnceLock::new(),
        min_watermark: AtomicU16::new(u16::MAX),
        registered: AtomicBool::new(false),
    };
    [EMPTY; THREAD_COUNT]
};

/// Register a thread for stack monitoring.
///
/// `slot` is a compile-time constant from this module's constants.
/// `name` is the thread's name (matching `Builder::name()`).
pub fn register_thread(slot: u8, name: &'static str) {
    if let Some(info) = THREADS.get(slot as usize) {
        info.min_watermark.store(u16::MAX, Ordering::Release);
        info.name.set(name).ok();
        info.registered.store(true, Ordering::Release);
        log::info!("[DIAG] Thread '{name}' registered (slot {slot})");
    }
}

/// Check the current thread's stack watermark against thresholds.
/// Records `DiagEvent::StackLow` / `StackCritical` if below threshold.
/// Safe to call from any registered thread.
#[allow(clippy::cast_possible_truncation)]
pub fn check_watermark(slot: u8) {
    let Some(info) = THREADS.get(slot as usize).filter(|t| t.registered.load(Ordering::Acquire)) else {
        return;
    };

    let wm = u16::try_from(crate::esp_safe::stack_watermark()).unwrap_or(0);

    let prev_min = info.min_watermark.load(Ordering::Acquire);
    if wm < prev_min {
        info.min_watermark.store(wm, Ordering::Release);

        if wm < CRITICAL_WATERMARK {
            black_box::record(DiagEvent::StackCritical {
                thread_id: slot,
                watermark: wm,
            });
            log::error!(
                "CRITICAL: Thread {slot} '{name}' stack watermark {wm} bytes (min so far)!",
                name = info.name.get().unwrap_or(&""),
            );
        } else if wm < WARN_WATERMARK {
            black_box::record(DiagEvent::StackLow {
                thread_id: slot,
                watermark: wm,
            });
            log::warn!(
                "Thread {slot} '{name}' low stack: {wm} bytes (min so far)",
                name = info.name.get().unwrap_or(&""),
            );
        }
    }
}

/// Emergency dump of all thread watermarks. Called from panic handler.
pub fn emergency_dump(writer: &mut dyn core::fmt::Write) {
    let _ = writeln!(writer, "=== STACK ===");
    for (slot, info) in THREADS.iter().enumerate() {
        if info.registered.load(Ordering::Acquire) {
            let wm = info.min_watermark.load(Ordering::Acquire);
            let final_wm = if wm == u16::MAX { 0 } else { wm };
            let name = info.name.get().unwrap_or(&"");
            let _ = writeln!(writer, "t{slot} {name} watermark={final_wm}");
        }
    }
}
