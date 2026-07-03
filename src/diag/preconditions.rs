//! Runtime contract assertions for GR-2 (stop flag) and other invariants.
//!
//! Unlike `assert!()`, these never panic — they record a diagnostic event
//! and return an error for the caller to handle.

use core::sync::atomic::AtomicBool;

use super::black_box;
use super::black_box::DiagEvent;

/// Assert a runtime contract. If `cond` is false, log an error and
/// record a `PreconditionFailed` event in the black box.
#[macro_export]
macro_rules! diag_assert {
    ($cond:expr, $contract_id:expr) => {
        if !$cond {
            $crate::diag::preconditions::record_failure($contract_id, line!() as u16);
        }
    };
    ($cond:expr, $contract_id:expr, $($arg:tt)+) => {
        if !$cond {
            $crate::diag::preconditions::record_failure($contract_id, line!() as u16);
            log::error!("PRECONDITION FAILED ({}): {}", $contract_id, format_args!($($arg)+));
        }
    };
}

// ── Contract IDs ────────────────────────────────────────────────
pub const CONTRACT_RMT_STOP_FLAG: u16 = 1;
pub const CONTRACT_RMT_THREAD: u16 = 2;
pub const CONTRACT_INIT_ORDER: u16 = 3;

/// Record a precondition failure in the black box.
pub fn record_failure(contract_id: u16, line: u16) {
    black_box::record(DiagEvent::PreconditionFailed { contract_id, line });
}

/// Pre-flight check before RMT motion (GR-2):
/// - `stop_flag` must be `Some(...)` (GR-2: mandatory stop flag)
/// - thread must be "motor" (GR-1: blocking RMT only in motor thread)
///
/// Returns `Err` with a description if any check fails.
pub fn assert_rmt_preconditions(
    stop_flag: Option<&AtomicBool>,
    thread_name: &str,
) -> Result<(), &'static str> {
    if stop_flag.is_none() {
        record_failure(CONTRACT_RMT_STOP_FLAG, 0);
        log::error!("PRECONDITION FAILED: RMT motion without stop flag — would violate GR-2");
        return Err("RMT motion requires stop flag (GR-2)");
    }
    if thread_name != "motor" {
        record_failure(CONTRACT_RMT_THREAD, 0);
        log::error!(
            "PRECONDITION FAILED: RMT motion from '{thread_name}' thread — must be 'motor' (GR-1)",
        );
        return Err("RMT motion must run in motor thread (GR-1)");
    }
    Ok(())
}
