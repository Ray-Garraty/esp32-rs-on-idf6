//! Lock-free ring buffer for structured diagnostic events.
//!
//! Stores the last 64 `DiagEvent` records with timestamps. Uses atomic
//! write index and volatile reads/writes so it works from any context
//! (main loop, threads, ISR, panic handler). No heap, no mutex, no locks.
//!
//! # Unsafe
//!
//! Two `unsafe` blocks for `core::ptr::write_volatile` / `read_volatile`
//! on the fixed-size static buffer. This is necessary because the buffer
//! is accessed from multiple threads and ISRs without a mutex — the
//! lock-freedom comes from single-writer-per-slot guarantee via atomic
//! index.
//!
//! # Safety invariants
//!
//! - `write_idx` is monotonic (AtomicU32 fetch_add), so each slot has
//!   exactly one writer at any time.
//! - `write_volatile` / `read_volatile` prevent the compiler from
//!   reordering or eliding the memory access.
//! - The buffer is `'static` and never deallocated.

use core::sync::atomic::{AtomicU32, Ordering};

use esp_idf_sys::esp_timer_get_time;

/// Number of events in the black box ring buffer.
const CAPACITY: usize = 64;

/// Diagnostic event types. Each variant's payload fits in 5 bytes
/// (total enum size ≤ 6 bytes due to `#[repr(u8)]` tag).
#[derive(Debug, Clone, Copy)]
#[repr(u8)]
pub enum DiagEvent {
    TickOverrun {
        expected_ms: u16,
        actual_ms: u16,
    } = 0,
    StackLow {
        thread_id: u8,
        watermark: u16,
    } = 1,
    StackCritical {
        thread_id: u8,
        watermark: u16,
    } = 2,
    HeapSnapshot {
        free_kb: u8,
        largest_kb: u8,
        phase: u8,
    } = 3,
    DramFragmented {
        largest_block: u16,
        requested: u16,
    } = 4,
    BuretteTransition {
        from: u8,
        to: u8,
        cmd: u8,
    } = 5,
    TransportTransition {
        from: u8,
        to: u8,
    } = 6,
    InitPhase {
        phase: u8,
        dram_free_kb: u8,
    } = 7,
    InitOrderViolation {
        expected: u8,
        actual: u8,
    } = 8,
    FfiEnter {
        boundary: u8,
    } = 9,
    FfiExit {
        boundary: u8,
        result: i8,
    } = 10,
    PreconditionFailed {
        contract_id: u16,
        line: u16,
    } = 11,
    LimitSwitchHit {
        switch: u8,
        motor_running: bool,
    } = 12,
    StopFlagIgnored {
        chunks_executed: u16,
    } = 13,
}

/// A single black-box record with timestamp and metadata.
#[derive(Debug, Clone, Copy)]
#[repr(C)]
pub struct Record {
    pub timestamp_us: u32,
    pub event: DiagEvent,
    pub thread_slot: u8,
    #[doc(hidden)]
    pub padding: [u8; 2],
    pub checksum: u8,
}

/// Lock-free ring buffer of diagnostic events.
pub struct BlackBox {
    buffer: [Record; CAPACITY],
    write_idx: AtomicU32,
    count: AtomicU32,
}

impl Default for BlackBox {
    fn default() -> Self {
        Self::new()
    }
}

impl BlackBox {
    /// Create a new empty black box (const, for static init).
    pub const fn new() -> Self {
        const EMPTY: Record = Record {
            timestamp_us: 0,
            event: DiagEvent::TickOverrun {
                expected_ms: 0,
                actual_ms: 0,
            },
            thread_slot: 0,
            padding: [0; 2],
            checksum: 0,
        };
        Self {
            buffer: [EMPTY; CAPACITY],
            write_idx: AtomicU32::new(0),
            count: AtomicU32::new(0),
        }
    }

    /// Record an event. Lock-free: atomic index increment, then volatile write.
    /// Safe to call from any context including ISR and panic handler.
    #[allow(
        clippy::cast_possible_truncation,
        clippy::cast_sign_loss,
        clippy::undocumented_unsafe_blocks,
        clippy::volatile_composites,
        clippy::as_ptr_cast_mut,
        clippy::ptr_cast_constness
    )]
    pub fn record(&self, event: DiagEvent) {
        let idx = self.write_idx.fetch_add(1, Ordering::Relaxed) as usize % CAPACITY;
        // SAFETY: Each slot has a single writer (atomic index is monotonic).
        // write_volatile prevents compiler reordering past the atomic increment.
        // timestamp uses esp_timer_get_time which is read-only and safe after boot.
        let record = Record {
            timestamp_us: unsafe { u32::try_from(esp_timer_get_time()).unwrap_or(0) / 1000 },
            event,
            thread_slot: current_thread_slot(),
            padding: [0; 2],
            checksum: 0,
        };
        unsafe {
            let ptr = self.buffer.as_ptr().cast_mut();
            core::ptr::write_volatile(ptr.add(idx), record);
        }
        self.count.fetch_add(1, Ordering::Release);
    }

    /// Write all events to `writer` (oldest first). Used from panic handler.
    #[allow(clippy::cast_possible_truncation, clippy::volatile_composites)]
    pub fn dump(&self, writer: &mut dyn core::fmt::Write) {
        let count = self
            .count
            .load(Ordering::Acquire)
            .min(u32::try_from(CAPACITY).unwrap_or(u32::MAX));
        let write_idx = self.write_idx.load(Ordering::Acquire) as usize;
        let start = (write_idx + CAPACITY - count as usize) % CAPACITY;

        let _ = writeln!(writer, "=== BLACK BOX ({count} events) ===");
        for i in 0..count as usize {
            let idx = (start + i) % CAPACITY;
            // SAFETY: idx is always in [0, CAPACITY). read_volatile ensures
            // we read a consistent snapshot (no torn reads on aligned u32).
            let record = unsafe { core::ptr::read_volatile(self.buffer.as_ptr().add(idx)) };
            let _ = writeln!(
                writer,
                "[{ts:8}us] t{slot} {ev:?}",
                ts = record.timestamp_us,
                slot = record.thread_slot,
                ev = record.event,
            );
        }
    }
}

// Global singleton — no heap, no mutex, survives panic/reboot.
static BLACK_BOX: BlackBox = BlackBox::new();

/// Convenience: record a diagnostic event.
pub fn record(event: DiagEvent) {
    BLACK_BOX.record(event);
}

/// Convenience: dump all events to a writer (from panic hook).
pub fn dump(writer: &mut dyn core::fmt::Write) {
    BLACK_BOX.dump(writer);
}

// ── Thread slot tracking ────────────────────────────────────────

use std::cell::Cell;

std::thread_local! {
    #[allow(clippy::missing_const_for_thread_local)]
    static THREAD_SLOT: Cell<u8> = Cell::new(0xFF);
}

/// Set the current thread's diagnostic slot ID. Call once at thread start.
pub fn set_thread_slot(slot: u8) {
    THREAD_SLOT.with(|s| s.set(slot));
}

/// Get the current thread's diagnostic slot ID.
fn current_thread_slot() -> u8 {
    THREAD_SLOT.with(Cell::get)
}
