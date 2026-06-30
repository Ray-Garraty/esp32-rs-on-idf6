//! Limit switch driver with PosEdge ISR and atomic flag.
//!
//! Each limit switch is associated with a static `AtomicBool` flag.
//! When the switch triggers (rising edge), the ISR stores `true` in the
//! atomic with `Ordering::Relaxed`. The main loop or motor thread polls
//! the flag via `is_triggered()` with `Ordering::Acquire`.
//!
//! Pin assignments:
//! - `STOP_FULL` — GPIO32 (syringe bottom plunger, burette full),
//!   pull-down, pos-edge interrupt.
//! - `STOP_EMPTY` — GPIO35 (syringe top plunger, burette empty),
//!   floating, pos-edge interrupt.

use core::sync::atomic::{AtomicBool, Ordering};

use esp_idf_hal::gpio::{Input, InputPin, InterruptType, PinDriver, Pull};

use crate::errors::StepperError;

/// Static atomic flag for the FULL limit switch (GPIO32).
pub static STOP_FULL: AtomicBool = AtomicBool::new(false);

/// Static atomic flag for the EMPTY limit switch (GPIO35).
pub static STOP_EMPTY: AtomicBool = AtomicBool::new(false);

/// GPIO-based limit switch with interrupt-driven atomic notification.
pub struct LimitSwitch {
    pin: PinDriver<'static, Input>,
    triggered: &'static AtomicBool,
}

impl LimitSwitch {
    /// Create a new limit switch with PosEdge interrupt.
    ///
    /// - `pin`: The GPIO input pin.
    /// - `pull`: Pull configuration (use `Pull::Down` for FULL, `Pull::Floating` for EMPTY).
    /// - `triggered`: Reference to the static `AtomicBool` flag for this switch.
    ///
    /// The interrupt callback stores `true` in the atomic flag with
    /// `Ordering::Relaxed` (no blocking, no heap, safe in ISR context).
    ///
    /// # Safety
    ///
    /// The `subscribe()` call installs an ISR callback. The callback must be
    /// safe for ISR context: no blocking, no heap allocation, no `println!`.
    /// Our callback only does a single `AtomicBool` store with `Ordering::Relaxed`,
    /// which is safe in any context.
    ///
    /// # Errors
    ///
    /// Returns `StepperError::InitFailed` if GPIO, interrupt configuration,
    /// or ISR subscription fails.
    pub fn new(
        pin: impl InputPin + 'static,
        pull: Pull,
        triggered: &'static AtomicBool,
    ) -> Result<Self, StepperError> {
        let mut pin = PinDriver::input(pin, pull).map_err(|_e| StepperError::InitFailed {
            reason: "LimitSwitch GPIO input init",
        })?;

        pin.set_interrupt_type(InterruptType::PosEdge)
            .map_err(|_e| StepperError::InitFailed {
                reason: "LimitSwitch set_interrupt_type",
            })?;

        // SAFETY: The ISR callback stores `true` in an AtomicBool with
        // Ordering::Relaxed. This is safe in ISR context because:
        // 1. No blocking calls, no heap allocation, no I/O.
        // 2. Atomic stores are lock-free on ESP32.
        // 3. The `'static` lifetime ensures the reference is valid for
        //    the entire program lifetime.
        unsafe {
            pin.subscribe(move || {
                triggered.store(true, Ordering::Relaxed);
            })
            .map_err(|_e| StepperError::InitFailed {
                reason: "LimitSwitch subscribe",
            })?;
        }

        pin.enable_interrupt()
            .map_err(|_e| StepperError::InitFailed {
                reason: "LimitSwitch enable_interrupt",
            })?;

        Ok(Self { pin, triggered })
    }

    /// Check if the limit switch has been triggered since last `clear()`.
    ///
    /// Uses `Ordering::Acquire` to synchronise with the ISR's `Relaxed` store.
    pub fn is_triggered(&self) -> bool {
        self.triggered.load(Ordering::Acquire)
    }

    /// Clear the triggered flag with `Ordering::Release`.
    ///
    /// Must be called after handling the trigger event to re-arm the flag.
    pub fn clear(&self) {
        self.triggered.store(false, Ordering::Release);
    }

    /// Re-arm the interrupt after handling a trigger.
    ///
    /// # Errors
    ///
    /// Returns `StepperError::InitFailed` if `enable_interrupt()` fails.
    pub fn rearm(&mut self) -> Result<(), StepperError> {
        self.pin
            .enable_interrupt()
            .map_err(|_e| StepperError::InitFailed {
                reason: "LimitSwitch rearm enable_interrupt",
            })?;
        Ok(())
    }

    /// Read the current GPIO pin level (not the latched flag).
    ///
    /// Returns `true` if the pin is HIGH.
    pub fn level(&self) -> bool {
        self.pin.is_high()
    }
}
