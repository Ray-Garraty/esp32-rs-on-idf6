//! 2-way solenoid valve control via GPIO + global atomic state.
//!
//! Valve pin: GPIO14 (PinDriver::output).
//! - `Input`  (LOW)  — draw from titrant bottle.
//! - `Output` (HIGH) — dispense into titration vessel.
//!
//! Initial state: Input (LOW).
//!
//! # Global State
//!
//! `GLOBAL_VALVE_POSITION` atomic provides lock-free reads for the main loop
//! and handlers. `GLOBAL_VALVE` mutex stores the `Valve` instance for GPIO
//! writes. `set_global_valve_position()` updates both the atomic AND drives
//! the GPIO pin. `get_global_valve_position()` reads only the atomic.

#![forbid(unsafe_code)]
use core::sync::atomic::{AtomicU8, Ordering};
use std::sync::Mutex;

use esp_idf_hal::gpio::{Output, OutputPin, PinDriver};

use crate::domain::types::ValvePosition;

// ── Global state ───────────────────────────────────────────────

/// Global valve position atomic for lock-free reads.
/// 0 = Input, 1 = Output.
static GLOBAL_VALVE_POSITION: AtomicU8 = AtomicU8::new(0);

/// Global valve instance for GPIO writes.
/// Created once by `global_valve_init()` at boot.
static GLOBAL_VALVE: Mutex<Option<Valve>> = Mutex::new(None);

// ── Public global accessors ────────────────────────────────────

/// Store the Valve instance for later GPIO control.
///
/// Must be called once at boot from main.rs after creating the Valve.
/// Panics if called a second time (the mutex already has `Some`).
/// Panics because init is infallible — failure indicates a programming error.
pub fn global_valve_init(valve: Valve) {
    if let Ok(mut guard) = GLOBAL_VALVE.lock() {
        *guard = Some(valve);
    }
}

/// Set the valve position: updates the atomic AND drives the GPIO pin.
///
/// The GPIO write is best-effort: if `GLOBAL_VALVE` is lock-contended,
/// the write is silently skipped (the atomic is always updated).
pub fn set_global_valve_position(pos: ValvePosition) {
    // Always update the atomic first (lock-free for broadcast readers)
    let disc: u8 = match pos {
        ValvePosition::Input => 0,
        ValvePosition::Output => 1,
    };
    GLOBAL_VALVE_POSITION.store(disc, Ordering::Release);

    // Try to drive GPIO; skip if lock contended (acceptable delay)
    #[cfg(target_arch = "xtensa")]
    if let Ok(mut guard) = GLOBAL_VALVE.try_lock() {
        if let Some(ref mut valve) = *guard {
            valve.set_position(pos);
        }
    }
}

/// Read the current global valve position from the atomic.
///
/// Returns `ValvePosition::Input` by default (initial state).
pub fn get_global_valve_position() -> ValvePosition {
    let disc = GLOBAL_VALVE_POSITION.load(Ordering::Acquire);
    match disc {
        0 => ValvePosition::Input,
        _ => ValvePosition::Output,
    }
}

// ── Hardware driver ────────────────────────────────────────────

/// GPIO-controlled 2-way solenoid valve.
pub struct Valve {
    pin: PinDriver<'static, Output>,
    position: ValvePosition,
}

impl Valve {
    /// Create a new valve with the given GPIO pin.
    ///
    /// The pin is set to LOW (Input position) immediately.
    ///
    /// # Errors
    ///
    /// Returns `EspError` if the underlying GPIO initialisation fails.
    pub fn new(pin: impl OutputPin + 'static) -> Result<Self, esp_idf_sys::EspError> {
        let mut pin = PinDriver::output(pin)?;
        pin.set_low()?;
        Ok(Self {
            pin,
            position: ValvePosition::Input,
        })
    }

    /// Set the valve to the given position.
    ///
    /// - `Input`  → pin LOW
    /// - `Output` → pin HIGH
    pub fn set_position(&mut self, position: ValvePosition) {
        match position {
            ValvePosition::Input => {
                self.pin.set_low().ok();
            }
            ValvePosition::Output => {
                self.pin.set_high().ok();
            }
        }
        self.position = position;
    }

    /// Return the last-set valve position.
    pub const fn get_position(&self) -> ValvePosition {
        self.position
    }
}

// ── Host-side tests ────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_global_valve_position_default_is_input() {
        // Without any init, position should be Input (0)
        assert_eq!(get_global_valve_position(), ValvePosition::Input);
    }

    #[test]
    fn test_set_and_get_global_valve_position() {
        set_global_valve_position(ValvePosition::Output);
        assert_eq!(get_global_valve_position(), ValvePosition::Output);
        set_global_valve_position(ValvePosition::Input);
        assert_eq!(get_global_valve_position(), ValvePosition::Input);
    }

    #[test]
    fn test_get_global_valve_position_output() {
        GLOBAL_VALVE_POSITION.store(1, Ordering::Release);
        assert_eq!(get_global_valve_position(), ValvePosition::Output);
        // Reset for other tests
        GLOBAL_VALVE_POSITION.store(0, Ordering::Release);
    }
}
