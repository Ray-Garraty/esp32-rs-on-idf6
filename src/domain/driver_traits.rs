//! Domain-level hardware abstraction traits.
//!
//! These traits define the interface between domain logic and hardware drivers.
//! They contain NO ESP-IDF imports — pure domain abstractions that can be
//! mocked in host-based unit tests.
//!
//! See `docs/refs/coding_style.md §3` for trait versus enum guidance.

use crate::domain::types::{Hz, Steps};
use crate::errors::StepperError;

/// Abstraction for a stepper motor with position tracking.
///
/// Implementors provide the concrete hardware control (RMT, UART, etc.).
/// Domain logic uses this trait to issue motion commands without
/// coupling to any specific driver.
pub trait StepperMotor {
    /// Move the motor by `steps` at the given `speed`.
    ///
    /// - Positive `steps` moves in the LiqIn (fill) direction.
    /// - Negative `steps` moves in the LiqOut (dispense) direction.
    ///
    /// # Errors
    ///
    /// Returns `StepperError` on RMT failure, limit switch hit, or timeout.
    fn move_steps(&mut self, steps: Steps, speed: Hz) -> Result<(), StepperError>;

    /// Stop the motor immediately (soft stop with position tracking).
    fn stop(&mut self) -> Result<(), StepperError>;

    /// Return the current absolute position in steps.
    fn position(&self) -> Steps;

    /// Return `true` if the motor driver is powered (EN pin active LOW).
    fn enabled(&self) -> bool;
}
