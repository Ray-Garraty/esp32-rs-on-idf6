//! Burette state machine types and transition validation.
//!
//! Defines the core burette state enum, command enum, and the validation
//! pipeline (safety → concurrency → state logic → params).
//! This module is pure domain logic — no ESP-IDF imports.

use crate::domain::types::{Ml, MlMin};
use crate::errors::StateError;

// ── Volume bounds ──────────────────────────────────────────────

/// Minimum dose volume in millilitres.
pub const BURETTE_MIN_VOLUME_ML: f32 = 0.01;

/// Maximum dose volume in millilitres.
pub const BURETTE_MAX_VOLUME_ML: f32 = 50.0;

// ── Rinse phase ────────────────────────────────────────────────

/// Phase of a rinse cycle.
///
/// - `Fill`:    Draw from bottle (LiqIn).
/// - `Empty`:   Dispense to vessel (LiqOut).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RinsePhase {
    Fill,
    Empty,
}

// ── Burette operation ─────────────────────────────────────────

/// High-level operation kind (used for status reporting).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BuretteOperation {
    None,
    Fill,
    Empty,
    Dose,
    Rinse,
}

// ── Burette state ──────────────────────────────────────────────

/// Burette state machine variants with data-carrying payloads.
#[derive(Debug, Clone, PartialEq)]
pub enum BuretteState {
    /// Idle — ready to accept commands.
    Idle,
    /// Homing sequence in progress.
    Homing,
    /// Filling the syringe from the bottle.
    Filling { target_ml: Ml },
    /// Emptying the syringe to the vessel.
    Emptying { target_ml: Ml },
    /// Dosing a precise volume.
    Dosing { remaining_ml: Ml },
    /// Rinse cycle in progress.
    Rinsing { phase: RinsePhase, cycles_left: u32 },
    /// Stop requested — transitioning to Idle.
    Stopping,
    /// Error state — requires reset.
    Error,
}

impl BuretteState {
    /// Returns `true` if the burette is idle.
    pub const fn is_idle(&self) -> bool {
        matches!(self, Self::Idle)
    }

    /// Returns `true` if the burette is currently moving (any active operation).
    pub const fn is_moving(&self) -> bool {
        matches!(
            self,
            Self::Homing
                | Self::Filling { .. }
                | Self::Emptying { .. }
                | Self::Dosing { .. }
                | Self::Rinsing { .. }
        )
    }

    /// Returns `true` if the burette is in an error state.
    pub const fn is_error(&self) -> bool {
        matches!(self, Self::Error)
    }
}

// ── Burette command ────────────────────────────────────────────

/// Commands that can be sent to the burette state machine.
#[derive(Debug, Clone, PartialEq)]
pub enum BuretteCommand {
    /// Fill the syringe at the given speed.
    Fill { speed: MlMin },
    /// Empty the syringe at the given speed.
    Empty { speed: MlMin },
    /// Dose a precise volume at the given speed.
    Dose { volume: Ml, speed: MlMin },
    /// Rinse the burette for the given number of cycles.
    Rinse { cycles: u8, speed: MlMin },
    /// Soft stop — finish current step then idle.
    Stop,
    /// Emergency stop — disable motor immediately.
    EmergencyStop,
    /// Reset from error state.
    Reset,
}

// ── State validation pipeline ──────────────────────────────────

/// Validate whether a command can be applied to the current state.
///
/// The validation pipeline follows this order:
/// 1. **Safety**   — not applicable at the domain level (handled by infrastructure)
/// 2. **Concurrency** — return `Busy` if already moving and cmd is non-Stop
/// 3. **State Logic** — reject invalid transitions via exhaustive matching
/// 4. **Params**   — bounds and ranges checked by the planner module
///
/// Returns `Ok(true)` if the transition is valid, `Err(StateError)` otherwise.
#[allow(clippy::needless_pass_by_value)]
pub const fn can_transition_to(
    state: &BuretteState,
    cmd: &BuretteCommand,
) -> Result<bool, StateError> {
    // EmergencyStop is always allowed from any state.
    if matches!(cmd, BuretteCommand::EmergencyStop) {
        return Ok(true);
    }

    // Stop is always allowed from any non-idle, non-error state.
    if matches!(cmd, BuretteCommand::Stop) {
        return match state {
            BuretteState::Idle | BuretteState::Error => Err(StateError::InvalidTransition),
            _ => Ok(true),
        };
    }

    // Concurrency: if moving, reject non-Stop/EmergencyStop commands.
    if state.is_moving() {
        return Err(StateError::Busy);
    }

    // Reset is only valid from Error state.
    if matches!(cmd, BuretteCommand::Reset) {
        return match state {
            BuretteState::Error => Ok(true),
            _ => Err(StateError::InvalidTransition),
        };
    }

    // State logic: idle → any command (except Stop/EmergencyStop/Reset handled above).
    // Error is handled by the Reset check above; moving states by the concurrency check.
    match state {
        BuretteState::Idle => Ok(true),
        _ => Err(StateError::InvalidTransition),
    }
}

// ── Status struct ──────────────────────────────────────────────

/// Snapshot of burette status for reporting.
#[derive(Debug, Clone)]
pub struct BuretteStatus {
    pub state: BuretteState,
    pub current_vol_ml: Ml,
    pub operation: BuretteOperation,
}

#[cfg(test)]
mod tests {
    use super::*;

    fn idle() -> BuretteState {
        BuretteState::Idle
    }

    fn moving() -> BuretteState {
        BuretteState::Filling { target_ml: Ml(5.0) }
    }

    fn error() -> BuretteState {
        BuretteState::Error
    }

    fn fill_cmd() -> BuretteCommand {
        BuretteCommand::Fill { speed: MlMin(10.0) }
    }

    fn dose_cmd() -> BuretteCommand {
        BuretteCommand::Dose {
            volume: Ml(5.0),
            speed: MlMin(10.0),
        }
    }

    // ── state predicates ──

    #[test]
    fn is_idle_true() {
        assert!(idle().is_idle());
    }

    #[test]
    fn is_idle_false_when_moving() {
        assert!(!moving().is_idle());
    }

    #[test]
    fn is_moving_true() {
        assert!(moving().is_moving());
    }

    #[test]
    fn is_moving_false_when_idle() {
        assert!(!idle().is_moving());
    }

    #[test]
    fn is_error_true() {
        assert!(error().is_error());
    }

    #[test]
    fn is_error_false_when_idle() {
        assert!(!idle().is_error());
    }

    // ── can_transition_to: idle state ──

    #[test]
    fn idle_accepts_fill() {
        assert!(can_transition_to(&idle(), &fill_cmd()).unwrap());
    }

    #[test]
    fn idle_accepts_empty() {
        assert!(can_transition_to(&idle(), &BuretteCommand::Empty { speed: MlMin(10.0) }).unwrap());
    }

    #[test]
    fn idle_accepts_dose() {
        assert!(can_transition_to(&idle(), &dose_cmd()).unwrap());
    }

    #[test]
    fn idle_accepts_rinse() {
        assert!(can_transition_to(
            &idle(),
            &BuretteCommand::Rinse {
                cycles: 3,
                speed: MlMin(10.0),
            }
        )
        .unwrap());
    }

    #[test]
    fn idle_rejects_stop() {
        assert!(matches!(
            can_transition_to(&idle(), &BuretteCommand::Stop),
            Err(StateError::InvalidTransition)
        ));
    }

    #[test]
    fn idle_rejects_reset() {
        assert!(matches!(
            can_transition_to(&idle(), &BuretteCommand::Reset),
            Err(StateError::InvalidTransition)
        ));
    }

    // ── can_transition_to: moving state ──

    #[test]
    fn moving_rejects_fill() {
        assert!(matches!(
            can_transition_to(&moving(), &fill_cmd()),
            Err(StateError::Busy)
        ));
    }

    #[test]
    fn moving_rejects_dose() {
        assert!(matches!(
            can_transition_to(&moving(), &dose_cmd()),
            Err(StateError::Busy)
        ));
    }

    #[test]
    fn moving_accepts_stop() {
        assert!(can_transition_to(&moving(), &BuretteCommand::Stop).unwrap());
    }

    #[test]
    fn moving_accepts_emergency_stop() {
        assert!(can_transition_to(&moving(), &BuretteCommand::EmergencyStop).unwrap());
    }

    #[test]
    fn moving_rejects_reset() {
        // Concurrency check fires first → returns Busy
        assert!(matches!(
            can_transition_to(&moving(), &BuretteCommand::Reset),
            Err(StateError::Busy)
        ));
    }

    // ── can_transition_to: error state ──

    #[test]
    fn error_accepts_reset() {
        assert!(can_transition_to(&error(), &BuretteCommand::Reset).unwrap());
    }

    #[test]
    fn error_accepts_emergency_stop() {
        assert!(can_transition_to(&error(), &BuretteCommand::EmergencyStop).unwrap());
    }

    #[test]
    fn error_rejects_fill() {
        assert!(matches!(
            can_transition_to(&error(), &fill_cmd()),
            Err(StateError::InvalidTransition)
        ));
    }

    #[test]
    fn error_rejects_stop() {
        assert!(matches!(
            can_transition_to(&error(), &BuretteCommand::Stop),
            Err(StateError::InvalidTransition)
        ));
    }

    // ── EmergencyStop always allowed ──

    #[test]
    fn emergency_stop_always_allowed() {
        assert!(can_transition_to(&idle(), &BuretteCommand::EmergencyStop).unwrap());
        assert!(can_transition_to(&moving(), &BuretteCommand::EmergencyStop).unwrap());
        assert!(can_transition_to(&error(), &BuretteCommand::EmergencyStop).unwrap());
    }
}
