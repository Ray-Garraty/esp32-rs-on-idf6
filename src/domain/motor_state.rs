//! Thread-safe atomic globals for lock-free communication between main loop and motor task.
//!
//! Provides:
//! - Atomic position, volume, state tag, and busy flag.
//! - Helper functions for encoding/decoding `BuretteState` ↔ `AtomicU8`.
//! - `PendingOpsManager` with fixed-size `heapless::Vec` for command watchdog tracking.
//!
//! Pure domain — no xtensa gate. Compiles on host.

#![forbid(unsafe_code)]

use crate::domain::burette::BuretteState;
use crate::domain::types::TransportSource;
use core::sync::atomic::{AtomicBool, AtomicI32, AtomicU8, Ordering};
use heapless::Vec;

// ── Atomic globals ─────────────────────────────────────────────

/// Target position in steps (signed: + = LiqIn, − = LiqOut).
pub static TARGET_POSITION: AtomicI32 = AtomicI32::new(0);

/// Current absolute motor position in steps.
pub static CURRENT_POSITION: AtomicI32 = AtomicI32::new(0);

/// Current syringe volume encoded as `f32 * 100` stored in `i32`.
/// E.g., `5.23 mL` → `523`. Read via `get_current_volume_ml_x100()`.
pub static CURRENT_VOLUME_ML_X100: AtomicI32 = AtomicI32::new(0);

/// Burette state discriminant encoded as `u8`.
/// Map: 0=Idle, 1=Homing, 2=Filling, 3=Emptying, 4=Dosing,
/// 5=Rinsing, 6=Stopping, 7=Error.
pub static BURETTE_STATE_TAG: AtomicU8 = AtomicU8::new(0);

/// Motor busy flag (true while a command is being executed).
pub static MOTOR_BUSY: AtomicBool = AtomicBool::new(false);

/// Set to true when homing completes (success or failure).
pub static HOMING_DONE: AtomicBool = AtomicBool::new(false);

/// Last homing limit switch steps (i.e., the position where the FULL limit switch
/// was hit during the last homing). Used to infer the real nominal volume.
pub static HOMING_STOP_STEPS: AtomicI32 = AtomicI32::new(0);

// ── Volume helpers (f32 × 100 encoding) ────────────────────────

/// Encode an `f32` volume (mL) to the `i32` ×100 representation.
/// Clamps to `i32::MAX` / `i32::MIN` on overflow.
///
/// # Lint justification
///
/// - `cast_possible_truncation`: explicit clamping to `i32::MAX`/`i32::MIN`
///   makes the truncation-safe cast the intentional clamping boundary.
/// - `cast_precision_loss`: `f32` mantissa (24 bit) is ~16.7 M; max burette
///   encoding is `50.0 × 100 = 5000`, far below the loss threshold.
#[allow(clippy::cast_possible_truncation, clippy::cast_precision_loss)]
fn encode_volume_x100(vol_ml: f32) -> i32 {
    let scaled = (vol_ml * 100.0).round();
    if scaled > (i32::MAX as f32) {
        i32::MAX
    } else if scaled < (i32::MIN as f32) {
        i32::MIN
    } else {
        scaled as i32
    }
}

/// Decode an `i32` ×100 volume back to `f32` mL.
///
/// # Lint justification
///
/// `cast_precision_loss`: max encoded volume is `50.0 × 100 = 5000`,
/// well within `f32`'s 24-bit exact-representable range (~16.7 M).
/// No precision is lost for any physically possible burette volume.
#[allow(clippy::cast_precision_loss)]
fn decode_volume_x100(encoded: i32) -> f32 {
    encoded as f32 / 100.0
}

// ── Public helpers ─────────────────────────────────────────────

/// Set the current volume from an `f32` mL value.
pub fn set_current_volume_ml(vol_ml: f32) {
    CURRENT_VOLUME_ML_X100.store(encode_volume_x100(vol_ml), Ordering::Release);
}

/// Get the current volume as `f32` mL.
pub fn get_current_volume_ml() -> f32 {
    decode_volume_x100(CURRENT_VOLUME_ML_X100.load(Ordering::Acquire))
}

/// Store a `BuretteState` discriminant into `BURETTE_STATE_TAG`.
pub fn set_burette_state_tag(state: &BuretteState) {
    let tag: u8 = match state {
        BuretteState::Idle => 0,
        BuretteState::Homing => 1,
        BuretteState::Filling { .. } => 2,
        BuretteState::Emptying { .. } => 3,
        BuretteState::Dosing { .. } => 4,
        BuretteState::Rinsing { .. } => 5,
        BuretteState::Stopping => 6,
        BuretteState::Error => 7,
    };

    #[cfg(target_arch = "xtensa")]
    {
        let prev = BURETTE_STATE_TAG.load(Ordering::Acquire);
        if prev != tag {
            crate::diag::state_tracer::log_burette_transition(prev, tag, 0);
        }
    }

    BURETTE_STATE_TAG.store(tag, Ordering::Release);
}

/// Read the `BuretteState` from `BURETTE_STATE_TAG`.
pub fn get_burette_state_tag() -> BuretteState {
    let tag = BURETTE_STATE_TAG.load(Ordering::Acquire);
    match tag {
        0 => BuretteState::Idle,
        1 => BuretteState::Homing,
        2 => BuretteState::Filling {
            target_ml: crate::domain::types::Ml(0.0),
        },
        3 => BuretteState::Emptying {
            target_ml: crate::domain::types::Ml(0.0),
        },
        4 => BuretteState::Dosing {
            remaining_ml: crate::domain::types::Ml(0.0),
        },
        5 => BuretteState::Rinsing {
            phase: crate::domain::burette::RinsePhase::Fill,
            cycles_left: 0,
        },
        6 => BuretteState::Stopping,
        _ => BuretteState::Error,
    }
}

/// Get status string for broadcast matching `BuretteState::to_broadcast_sts()`.
pub fn get_broadcast_sts() -> &'static str {
    let tag = BURETTE_STATE_TAG.load(Ordering::Acquire);
    match tag {
        0 => "idle",
        7 => "error",
        _ => "working",
    }
}

// ── Pending operation tracking for command watchdog ────────────

/// A tracked command that has been acknowledged but not yet completed.
#[derive(Debug)]
pub struct PendingOpEntry {
    /// Command ID for correlating the response.
    pub id: u64,
    /// Transport source (USB or BLE) for routing the response.
    pub transport: TransportSource,
    /// Monotonic millisecond timestamp when the command was dispatched.
    pub started_at_ms: u64,
}

/// Manager for pending command operations.
///
/// Fixed-size storage of up to `MAX_PENDING_RESPONSES` entries using
/// `heapless::Vec`. Used for command watchdog: if an entry exceeds
/// `WATCHDOG_CMD_TIMEOUT_MS`, it is considered expired and triggers
/// an emergency stop.
#[derive(Debug)]
pub struct PendingOpsManager {
    pending: Vec<PendingOpEntry, 4>,
}

impl PendingOpsManager {
    /// Create a new empty manager.
    pub const fn new() -> Self {
        Self {
            pending: Vec::new(),
        }
    }

    /// Add a new pending entry.
    ///
    /// Returns `Err("pending_ops_full")` if the fixed-size buffer is full.
    pub fn push(&mut self, entry: PendingOpEntry) -> Result<(), &'static str> {
        if self.pending.len() >= self.pending.capacity() {
            return Err("pending_ops_full");
        }
        self.pending.push(entry).ok();
        Ok(())
    }

    /// Remove a pending entry by its command ID.
    pub fn remove(&mut self, id: u64) {
        // heapless::Vec has no `retain`, so we use positional swap-remove
        if let Some(pos) = self.pending.iter().position(|e| e.id == id) {
            self.pending.swap_remove(pos);
        }
    }

    /// Check all pending entries and return those that have expired.
    ///
    /// Expired entries are automatically removed from the manager.
    /// Returns up to 1 expired entry (caller handles one at a time).
    pub fn watchdog_check(&mut self, now_ms: u64, timeout_ms: u64) -> Vec<PendingOpEntry, 1> {
        let mut expired: Vec<PendingOpEntry, 1> = Vec::new();
        let mut remaining: Vec<PendingOpEntry, 4> = Vec::new();

        while let Some(entry) = self.pending.pop() {
            if now_ms.wrapping_sub(entry.started_at_ms) > timeout_ms {
                // Expired — add to expired list (up to capacity)
                if expired.len() < expired.capacity() {
                    expired.push(entry).ok();
                }
            } else {
                // Still valid — keep
                remaining.push(entry).ok();
            }
        }

        // Restore remaining entries
        self.pending = remaining;
        expired
    }

    /// Returns `true` if there are pending operations.
    pub fn is_pending(&self) -> bool {
        !self.pending.is_empty()
    }

    /// Returns the number of pending entries.
    pub fn len(&self) -> usize {
        self.pending.len()
    }

    /// Returns `true` if there are no pending entries.
    pub fn is_empty(&self) -> bool {
        self.pending.is_empty()
    }
}

impl Default for PendingOpsManager {
    fn default() -> Self {
        Self::new()
    }
}

// ── Tests ──────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_encode_decode_volume_roundtrip() {
        let volumes = [0.0, 0.01, 5.23, 8.14, 50.0, -1.0, 1234.56];
        for &v in &volumes {
            let encoded = encode_volume_x100(v);
            let decoded = decode_volume_x100(encoded);
            let expected = (v * 100.0).round() / 100.0;
            assert!(
                (decoded - expected).abs() < 0.005,
                "vol={v} enc={encoded} dec={decoded}"
            );
        }
    }

    #[test]
    fn test_burette_state_tag_roundtrip() {
        let states = [
            BuretteState::Idle,
            BuretteState::Homing,
            BuretteState::Filling {
                target_ml: crate::domain::types::Ml(5.0),
            },
            BuretteState::Emptying {
                target_ml: crate::domain::types::Ml(5.0),
            },
            BuretteState::Dosing {
                remaining_ml: crate::domain::types::Ml(2.0),
            },
            BuretteState::Rinsing {
                phase: crate::domain::burette::RinsePhase::Fill,
                cycles_left: 3,
            },
            BuretteState::Stopping,
            BuretteState::Error,
        ];
        for state in &states {
            set_burette_state_tag(state);
            let read_back = get_burette_state_tag();
            // Compare discriminants (payloads may differ for data-carrying variants)
            assert_eq!(
                core::mem::discriminant(state),
                core::mem::discriminant(&read_back),
                "tag mismatch for {state:?}"
            );
        }
    }

    #[test]
    fn test_broadcast_sts() {
        set_burette_state_tag(&BuretteState::Idle);
        assert_eq!(get_broadcast_sts(), "idle");
        set_burette_state_tag(&BuretteState::Error);
        assert_eq!(get_broadcast_sts(), "error");
        set_burette_state_tag(&BuretteState::Filling {
            target_ml: crate::domain::types::Ml(5.0),
        });
        assert_eq!(get_broadcast_sts(), "working");
    }

    #[test]
    fn test_current_volume_default() {
        assert!((get_current_volume_ml() - 0.0).abs() < 0.001);
    }

    #[test]
    fn test_set_current_volume() {
        set_current_volume_ml(8.14);
        assert!((get_current_volume_ml() - 8.14).abs() < 0.01);
    }

    #[test]
    fn test_pending_ops_push_and_remove() {
        let mut mgr = PendingOpsManager::new();
        assert!(!mgr.is_pending());
        let entry = PendingOpEntry {
            id: 42,
            transport: TransportSource::Usb,
            started_at_ms: 1000,
        };
        assert!(mgr.push(entry).is_ok());
        assert!(mgr.is_pending());
        assert_eq!(mgr.len(), 1);
        mgr.remove(42);
        assert!(!mgr.is_pending());
    }

    #[test]
    fn test_pending_ops_full_rejects() {
        let mut mgr = PendingOpsManager::new();
        for i in 0..4 {
            let entry = PendingOpEntry {
                id: i as u64,
                transport: TransportSource::Usb,
                started_at_ms: 1000,
            };
            assert!(mgr.push(entry).is_ok(), "push {i} should succeed");
        }
        let extra = PendingOpEntry {
            id: 99,
            transport: TransportSource::Usb,
            started_at_ms: 1000,
        };
        assert!(mgr.push(extra).is_err());
    }

    #[test]
    fn test_watchdog_expires_entry() {
        let mut mgr = PendingOpsManager::new();
        let entry = PendingOpEntry {
            id: 1,
            transport: TransportSource::Usb,
            started_at_ms: 100,
        };
        mgr.push(entry).ok();
        // now_ms = 200, timeout = 50 → 200 - 100 = 100 > 50 → expired
        let expired = mgr.watchdog_check(200, 50);
        assert_eq!(expired.len(), 1);
        assert_eq!(expired[0].id, 1);
        assert!(!mgr.is_pending());
    }

    #[test]
    fn test_watchdog_keeps_valid_entry() {
        let mut mgr = PendingOpsManager::new();
        let entry = PendingOpEntry {
            id: 1,
            transport: TransportSource::Usb,
            started_at_ms: 100,
        };
        mgr.push(entry).ok();
        // now_ms = 120, timeout = 50 → 120 - 100 = 20 ≤ 50 → not expired
        let expired = mgr.watchdog_check(120, 50);
        assert_eq!(expired.len(), 0);
        assert!(mgr.is_pending());
    }

    #[test]
    fn test_watchdog_removes_expired_keeps_valid() {
        let mut mgr = PendingOpsManager::new();
        mgr.push(PendingOpEntry {
            id: 1,
            transport: TransportSource::Usb,
            started_at_ms: 50,
        })
        .ok();
        mgr.push(PendingOpEntry {
            id: 2,
            transport: TransportSource::Ble,
            started_at_ms: 150,
        })
        .ok();
        // now_ms = 200, timeout = 100 → id1: 200-50=150 > 100 expired; id2: 200-150=50 ≤ 100 valid
        let expired = mgr.watchdog_check(200, 100);
        assert_eq!(expired.len(), 1);
        assert_eq!(expired[0].id, 1);
        assert_eq!(mgr.len(), 1);
    }
}
