//! 2-way solenoid valve control via GPIO.
//!
//! Valve pin: GPIO14 (PinDriver::output).
//! - `Input`  (LOW)  — draw from titrant bottle.
//! - `Output` (HIGH) — dispense into titration vessel.
//!
//! Initial state: Input (LOW).

use esp_idf_hal::gpio::{Output, OutputPin, PinDriver};

use crate::domain::types::ValvePosition;

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
