//! Application-level state machines.
//!
//! Combines the burette state machine with transport state tracking.
//! Compiles on host — pure domain logic.

use crate::domain::burette::BuretteState;
use crate::domain::types::TransportState;
use crate::errors::{AppError, StateError};

// ── Application state machine ─────────────────────────────────

/// Application-level state combining burette and transport state.
#[derive(Debug, Clone, PartialEq)]
pub struct ApplicationStateMachine {
    /// Current burette state.
    pub burette: BuretteState,
    /// Current transport state.
    pub transport: TransportState,
}

impl ApplicationStateMachine {
    /// Create a new state machine in the idle + USB-active state.
    pub const fn new() -> Self {
        Self {
            burette: BuretteState::Idle,
            transport: TransportState::UsbActive,
        }
    }

    /// Update the transport state.
    pub const fn set_transport(&mut self, state: TransportState) {
        self.transport = state;
    }

    /// Update the burette state.
    pub const fn set_burette(&mut self, state: BuretteState) {
        self.burette = state;
    }

    /// Returns `true` if the application is in a ready state (burette idle).
    pub const fn is_ready(&self) -> bool {
        self.burette.is_idle()
    }
}

impl Default for ApplicationStateMachine {
    fn default() -> Self {
        Self::new()
    }
}

// ── Pending operation ─────────────────────────────────────────

/// Describes an operation that was acknowledged but not yet completed.
#[derive(Debug, Clone)]
pub enum PendingOperation {
    /// No pending operation.
    None,
    /// A burette operation is in progress.
    BuretteOp,
    /// A calibration run is in progress.
    CalibrationRun,
    /// A calibration speed sequence is in progress.
    CalibrationSpeedSeq,
}

impl PendingOperation {
    /// Returns `true` if there is a pending operation.
    pub const fn is_pending(&self) -> bool {
        !matches!(self, Self::None)
    }
}

/// Validate that no operation is pending.
///
/// # Errors
///
/// Returns `StateError::Busy` if an operation is already pending.
pub const fn require_no_pending(pending: &PendingOperation) -> Result<(), AppError> {
    if pending.is_pending() {
        Err(AppError::State(StateError::Busy))
    } else {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_state_machine_defaults() {
        let sm = ApplicationStateMachine::new();
        assert_eq!(sm.burette, BuretteState::Idle);
        assert_eq!(sm.transport, TransportState::UsbActive);
        assert!(sm.is_ready());
    }

    #[test]
    fn test_state_machine_transport_update() {
        let mut sm = ApplicationStateMachine::new();
        sm.set_transport(TransportState::BleConnected);
        assert_eq!(sm.transport, TransportState::BleConnected);
    }

    #[test]
    fn test_pending_operation_none() {
        let p = PendingOperation::None;
        assert!(!p.is_pending());
        assert!(require_no_pending(&p).is_ok());
    }

    #[test]
    fn test_pending_operation_busy() {
        let p = PendingOperation::BuretteOp;
        assert!(p.is_pending());
        assert!(require_no_pending(&p).is_err());
    }

    #[test]
    fn test_is_ready_with_moving_burette() {
        let mut sm = ApplicationStateMachine::new();
        sm.set_burette(BuretteState::Filling {
            target_ml: crate::domain::types::Ml(5.0),
        });
        assert!(!sm.is_ready());
    }
}
