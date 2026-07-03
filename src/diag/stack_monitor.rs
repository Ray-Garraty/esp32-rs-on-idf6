//! Stack watermark monitor for all known threads.
//!
//! Uses a fixed-size static array of `ThreadInfo` (no heap, no mutex).
//! Each thread registers at creation with a compile-time slot ID.
//! Periodic `check_watermark()` logs warnings when stack is low.

use core::sync::atomic::{AtomicU16, Ordering};

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
    name: &'static str,
    min_watermark: AtomicU16,
    registered: bool,
}

/// Fixed-size registry — all threads known at compile time.
#[allow(clippy::declare_interior_mutable_const)]
static THREADS: [ThreadInfo; THREAD_COUNT] = {
    const EMPTY: ThreadInfo = ThreadInfo {
        name: "",
        min_watermark: AtomicU16::new(u16::MAX),
        registered: false,
    };
    [EMPTY; THREAD_COUNT]
};

/// Register a thread for stack monitoring.
///
/// `slot` is a compile-time constant from this module's constants.
/// `name` is the thread's name (matching `Builder::name()`).
#[allow(clippy::borrow_as_ptr, clippy::ptr_cast_constness)]
pub fn register_thread(slot: u8, name: &'static str) {
    if let Some(info) = THREADS.get(slot as usize) {
        info.min_watermark.store(u16::MAX, Ordering::Release);
        // SAFETY: We write `name` once at init then only read it.
        // UnsafeCell would be more correct, but for static init this is safe
        // because we write once before any concurrent access.
        unsafe {
            (core::ptr::addr_of!(info.name) as *mut &str).write(name);
        }
        // SAFETY: Same as above — single write before concurrent access.
        unsafe {
            (core::ptr::addr_of!(info.registered) as *mut bool).write(true);
        }
        log::info!("[DIAG] Thread '{name}' registered (slot {slot})");
    }
}

/// Check the current thread's stack watermark against thresholds.
/// Records `DiagEvent::StackLow` / `StackCritical` if below threshold.
/// Safe to call from any registered thread.
#[allow(clippy::cast_possible_truncation)]
pub fn check_watermark(slot: u8) {
    let Some(info) = THREADS.get(slot as usize).filter(|t| t.registered) else {
        return;
    };

    // SAFETY: uxTaskGetStackHighWaterMark(NULL) is read-only, returns
    // watermark for the calling task. Safe from any FreeRTOS task context.
    let wm = unsafe {
        u16::try_from(esp_idf_sys::uxTaskGetStackHighWaterMark(
            core::ptr::null_mut(),
        ))
    }
    .unwrap_or(0);

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
                name = info.name,
            );
        } else if wm < WARN_WATERMARK {
            black_box::record(DiagEvent::StackLow {
                thread_id: slot,
                watermark: wm,
            });
            log::warn!(
                "Thread {slot} '{name}' low stack: {wm} bytes (min so far)",
                name = info.name,
            );
        }
    }
}

/// Emergency dump of all thread watermarks. Called from panic handler.
pub fn emergency_dump(writer: &mut dyn core::fmt::Write) {
    let _ = writeln!(writer, "=== STACK WATERMARKS ===");
    for (slot, info) in THREADS.iter().enumerate() {
        if info.registered {
            let wm = info.min_watermark.load(Ordering::Acquire);
            let final_wm = if wm == u16::MAX { 0 } else { wm };
            let _ = writeln!(
                writer,
                "  t{slot} {name}: min_watermark={final_wm} bytes",
                name = info.name,
            );
        }
    }
}
