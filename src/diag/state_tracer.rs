//! State machine transition logger.
//!
//! Records `BuretteState` and `TransportState` transitions into the
//! black box for post-mortem analysis. The numeric IDs are kept in sync
//! with `motor_state::set_burette_state_tag()` encoding.

use super::black_box;
use super::black_box::DiagEvent;

/// Log a burette state machine transition.
///
/// `from`, `to`: u8 discriminant matching the encoding in `motor_state.rs`
/// (0=Idle, 1=Homing, 2=Filling, 3=Emptying, 4=Dosing, 5=Rinsing,
///  6=Stopping, 7=Error).
/// `cmd`: u8 command code (0=none, 1=Fill, 2=Empty, 3=Dose, 4=Rinse,
///  5=Stop, 6=EmergencyStop, 7=MoveToStop, 8=Reset).
pub fn log_burette_transition(from: u8, to: u8, cmd: u8) {
    black_box::record(DiagEvent::BuretteTransition { from, to, cmd });
    log::info!("[DIAG] burette: {from} → {to}");
}

/// Log a transport state machine transition.
///
/// `from`, `to`: 0=UsbActive, 1=BleAdvertising, 2=BleConnected
pub fn log_transport_transition(from: u8, to: u8) {
    black_box::record(DiagEvent::TransportTransition { from, to });
    log::info!("[DIAG] transport: {from} → {to}");
}
