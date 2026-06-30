use core::ops::{Add, Sub};
use serde::{Deserialize, Serialize};

// ── Newtype wrappers ──────────────────────────────────────────

/// Steps (signed — positive is LiqIn/fill, negative is LiqOut/dispense).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Steps(pub i32);

impl Steps {
    /// Absolute value as unsigned steps.
    pub const fn abs(self) -> u32 {
        self.0.unsigned_abs()
    }
}

impl Add for Steps {
    type Output = Self;
    fn add(self, rhs: Self) -> Self {
        Self(self.0 + rhs.0)
    }
}

impl Sub for Steps {
    type Output = Self;
    fn sub(self, rhs: Self) -> Self {
        Self(self.0 - rhs.0)
    }
}

/// Step frequency in Hertz.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Hz(pub u32);

/// Volume in millilitres.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Ml(pub f32);

/// Flow rate in millilitres per minute.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct MlMin(pub f32);

/// Electrode / ADC reading in millivolts (calibrated).
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Mv(pub f32);

/// Temperature in degrees Celsius.
/// Nullability is modelled at the usage site via `Option<Celsius>`.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Celsius(pub f32);

/// StallGuard result value from TMC2209 DRV_STATUS (10-bit, 0–1023).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SgValue(pub u16);

/// StallGuard threshold written to SGTHRS register (0–255).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SgThreshold(pub u8);

// ── Enum types ────────────────────────────────────────────────

/// Stepper motor direction.
///
/// Maps to legacy `LIQ_IN` / `LIQ_OUT` from stepper.cpp.
/// - `LiqIn`:  Fill from bottle (draws liquid into syringe).
/// - `LiqOut`: Dispense to vessel (expels liquid from syringe).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum Direction {
    LiqIn,
    LiqOut,
}

impl Direction {
    pub const fn is_liq_in(&self) -> bool {
        matches!(self, Self::LiqIn)
    }
}

impl From<Direction> for bool {
    fn from(d: Direction) -> Self {
        d.is_liq_in()
    }
}

/// Limit switch identifier.
///
/// Maps to `limitswitch_state_t { full, empty }` fields.
/// - `Full`:  GPIO32 — syringe bottom plunger, burette is full.
/// - `Empty`: GPIO35 — syringe top plunger, burette is empty.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LimitSwitchId {
    Full,
    Empty,
}

/// Valve position (2-way solenoid valve).
///
/// Maps to `VALVE_POSITION_INPUT "input"` / `VALVE_POSITION_OUTPUT "output"`.
/// - `Input`:  Draw from titrant bottle.
/// - `Output`: Dispense into titration vessel.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum ValvePosition {
    Input,
    Output,
}

impl ValvePosition {
    /// Serialise to broadcast format: `"in"` or `"out"`.
    pub const fn as_str(&self) -> &'static str {
        match self {
            Self::Input => "in",
            Self::Output => "out",
        }
    }
}

/// A measurement reading (raw wire-protocol representation).
#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
pub struct Measurement {
    /// Frequency in Hz (raw u16 for wire protocol).
    pub freq_hz: u16,
    /// Speed in ml/min (raw f32 for wire protocol).
    pub speed_ml_min: f32,
}

/// Active transport connection state.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TransportState {
    UsbActive,
    BleDisconnected,
    BleConnected,
}

/// Communication transport source.
///
/// Maps to legacy `TRANSPORT_USB` / `TRANSPORT_BLE` from stepper.h.
/// Used for transport priority, PendingCmd routing, LED blink mode.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TransportSource {
    Usb,
    Ble,
}

/// LED transport mode indicator.
///
/// Controls the status LED behaviour:
/// - `UsbActive`:     Solid OFF (USB transport active).
/// - `BleAdvertising`: Solid ON (BLE advertising, no connection).
/// - `BleConnected`:   1 Hz blink (BLE connected).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TransportMode {
    UsbActive,
    BleAdvertising,
    BleConnected,
}
