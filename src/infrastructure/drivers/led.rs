//! Status LED with transport-mode blink state machine.
//!
//! LED pin: GPIO2 (PinDriver::output, active HIGH).
//!
//! Behaviour per transport mode:
//! - `UsbActive`     → always LOW (OFF)
//! - `BleAdvertising` → always HIGH (ON)
//! - `BleConnected`  → 1 Hz blink (500 ms ON, 500 ms OFF)
//!
//! The blink state machine runs from `process()` which must be called
//! from the main loop every tick (~10 ms). No blocking calls.

use esp_idf_hal::gpio::{Output, OutputPin, PinDriver};

use crate::domain::types::TransportMode;

/// Blink half-period in milliseconds (500 ms ON / 500 ms OFF = 1 Hz).
const BLINK_HALF_PERIOD_MS: u64 = 500;

/// Status LED indicator with non-blocking blink state machine.
pub struct Led {
    pin: PinDriver<'static, Output>,
    mode: TransportMode,
    blink_timer: u64,
    blink_state: bool,
}

impl Led {
    /// Create a new LED on the given GPIO pin.
    ///
    /// Initial state: OFF (LOW) with `UsbActive` mode.
    ///
    /// # Errors
    ///
    /// Returns `EspError` if the GPIO initialisation fails.
    pub fn new(pin: impl OutputPin + 'static) -> Result<Self, esp_idf_sys::EspError> {
        let mut pin = PinDriver::output(pin)?;
        pin.set_low()?;
        Ok(Self {
            pin,
            mode: TransportMode::UsbActive,
            blink_timer: 0,
            blink_state: false,
        })
    }

    /// Configure the LED behaviour for the given transport mode.
    ///
    /// Resets the blink timer so the next blink cycle starts fresh.
    pub fn set_transport_mode(&mut self, mode: TransportMode) {
        self.mode = mode;
        self.blink_timer = 0;
        self.blink_state = false;
        self.apply_state();
    }

    /// Advance the blink state machine.
    ///
    /// Must be called periodically (every main loop tick at ~10 ms)
    /// with the elapsed milliseconds since last call.
    ///
    /// # Errors
    ///
    /// This function is infallible — GPIO set errors are logged and
    /// silently ignored since there is no meaningful recovery.
    pub fn process(&mut self, elapsed_ms: u64) {
        match self.mode {
            // UsbActive (OFF) and BleAdvertising (ON) have no timing logic
            TransportMode::UsbActive | TransportMode::BleAdvertising => {}
            TransportMode::BleConnected => {
                // 1 Hz blink: toggle every 500 ms
                self.blink_timer += elapsed_ms;
                if self.blink_timer >= BLINK_HALF_PERIOD_MS {
                    self.blink_timer -= BLINK_HALF_PERIOD_MS;
                    self.blink_state = !self.blink_state;
                    self.apply_state();
                }
            }
        }
    }

    /// Apply the current blink state to the GPIO pin.
    fn apply_state(&mut self) {
        let target = match self.mode {
            TransportMode::UsbActive => false,
            TransportMode::BleAdvertising => true,
            TransportMode::BleConnected => self.blink_state,
        };
        if target {
            self.pin.set_high().ok();
        } else {
            self.pin.set_low().ok();
        }
    }
}

// ── Tests ──────────────────────────────────────────────────────
// LED tests require hardware (GPIO pin). These are on-device integration
// tests (AC-019, verification_method: code_review). Unit testing the blink
// state machine logic is done via the domain types (TransportMode enum).
