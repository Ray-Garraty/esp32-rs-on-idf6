//! Hardware driver modules.
//!
//! Each submodule wraps a specific peripheral:
//! - `stepper` — RMT-based stepper motor control
//! - `adc` — pH electrode ADC read + calibration
//! - `onewire` — DS18B20 temperature sensor (software bitbang)
//! - `valve` — 2-way solenoid valve GPIO control
//! - `limitswitch` — Endstop limit switches with ISR atomic flags
//! - `led` — Status LED with transport-mode blink state machine

#[cfg(target_arch = "xtensa")]
pub mod adc;
#[cfg(target_arch = "xtensa")]
pub mod led;
#[cfg(target_arch = "xtensa")]
pub mod limitswitch;
#[cfg(target_arch = "xtensa")]
pub mod onewire;
#[cfg(target_arch = "xtensa")]
pub mod stepper;
#[cfg(target_arch = "xtensa")]
pub mod valve;
