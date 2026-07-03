//! Diagnostic subsystem for structured event logging and post-mortem analysis.
//!
//! Provides lock-free black box, tick watchdog, stack/heap monitors,
//! state tracer, FFI guard, and runtime preconditions.
//!
//! # Architecture
//!
//! ```text
//! panic handler → dump black box + stack watermarks to UART
//! main loop     → tick watchdog (blocking detect), periodic heap/stack snapshots
//! motor task    → stack monitor, state tracer
//! thread init   → stack monitor registration
//! FFI calls     → ffi_guard enter/exit records
//! stepper       → preconditions (stop flag, thread check)
//! ```

pub mod black_box;
pub mod ffi_guard;
pub mod heap_snapshot;
pub mod preconditions;
pub mod stack_monitor;
pub mod state_tracer;
pub mod tick_watchdog;

pub use black_box::dump;
/// Convenience re-exports for common operations.
pub use black_box::record;
pub use heap_snapshot::snapshot;
pub use stack_monitor::check_watermark;
pub use stack_monitor::register_thread;

/// Initialize the diagnostic subsystem.
///
/// Must be called once at boot after `logger::init()`.
/// Currently performs thread registration for the main task.
pub fn init() {
    register_thread(stack_monitor::MAIN, "main");
    snapshot("boot");
    log::info!("[DIAG] diagnostic subsystem initialized");
}
