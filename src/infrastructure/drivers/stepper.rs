//! RMT-based stepper motor driver.
//!
//! Uses `TxChannelDriver` (RMT channel 0) on GPIO25 for step pulse generation,
//! `PinDriver::output` on GPIO26 for direction, and GPIO27 for enable (active LOW).
//!
//! Pulse generation:
//! - RMT resolution = 1 MHz (1 tick = 1 µs).
//! - Each step = 1 µs HIGH + (interval − 1) µs LOW.
//! - Symbols are batched into chunks of 128 and sent via `CopyEncoder` +
//!   `send_and_wait()`.
//!
//! # BLOCKING
//!
//! `move_steps_intervals()` (and the trait impl `move_steps()`) calls
//! `send_and_wait()` which blocks until the chunk completes. This is
//! acceptable ONLY in a dedicated motor thread (4 KB stack).
//! NEVER call these methods from the main loop.
//!
//! # Safety Integrations
//!
//! - `set_stop_flag()` connects a `&'static AtomicBool` (from `LimitSwitch`).
//!   Before each chunk, `move_steps_intervals()` checks the flag and returns
//!   `StepperError::LimitSwitchReached` if set.
//! - `emergency_stop()` calls `channel.disable()` immediately.
//! - `Drop` impl disables the channel first, then sets EN HIGH.

#![forbid(unsafe_code)]
use core::sync::atomic::{AtomicBool, AtomicI32, Ordering};

use esp_idf_hal::gpio::{AnyOutputPin, Output, PinDriver};
use esp_idf_hal::rmt::config::{Loop, MemoryAccess, TransmitConfig, TxChannelConfig};
use esp_idf_hal::rmt::encoder::CopyEncoder;
use esp_idf_hal::rmt::{PinState, Pulse, PulseTicks, RmtChannel, Symbol, TxChannelDriver};
use esp_idf_hal::units::FromValueType;

use crate::config;
use crate::diag;
use crate::domain::context::MotorContext;
use crate::domain::driver_traits::StepperMotor;
use crate::domain::types::{Direction, Hz, Steps};
use crate::errors::StepperError;
use crate::stepper::ramp::{compute_ramp, RampConfig};

/// Static assertion: RMT chunk size must fit within the RMT memory block
/// (indirect mode, 128 symbols per block).
const _: () = assert!(config::RMT_CHUNK_MAX <= 128);

/// RMT-based stepper motor controller.
///
/// Owns the RMT TX channel, DIR pin, and EN pin. Supports interrupt-driven
/// stop via an optional `&'static AtomicBool` flag (from `LimitSwitch`).
///
/// Implements the [`StepperMotor`] trait for use by domain logic. The
/// `position` field tracks the absolute motor position in steps, and
/// `motor_enabled` tracks the driver power state.
pub struct RmtStepper<'d> {
    channel: TxChannelDriver<'d>,
    dir: PinDriver<'d, Output>,
    en: PinDriver<'d, Output>,
    stop_flag: Option<&'static AtomicBool>,
    /// Current absolute position in steps (atomic for lock-free reads).
    position: AtomicI32,
    /// Whether the motor driver is powered (EN pin active LOW).
    motor_enabled: AtomicBool,
}

impl<'d> RmtStepper<'d> {
    /// Create a new RMT stepper driver.
    ///
    /// - `step_pin`: GPIO25 — RMT TX channel 0 for pulse generation.
    /// - `dir_pin`:  GPIO26 — direction output.
    /// - `en_pin`:   GPIO27 — enable output (active LOW).
    ///
    /// Configuration:
    /// | Parameter | Value |
    /// |---|---|
    /// | Resolution | 1 MHz (1 tick = 1 µs) |
    /// | Transaction queue depth | 2 |
    /// | Memory block symbols | 128 |
    ///
    /// DIR is initialised LOW (LiqOut). EN is set LOW immediately to power
    /// the motor driver (active LOW).
    ///
    /// # Errors
    ///
    /// Returns `StepperError::InitFailed` if GPIO or RMT init fails.
    pub fn new(
        step_pin: AnyOutputPin<'d>,
        dir_pin: AnyOutputPin<'d>,
        en_pin: AnyOutputPin<'d>,
    ) -> Result<Self, StepperError> {
        let mut dir = PinDriver::output(dir_pin)?;
        dir.set_low()?;
        let mut en = PinDriver::output(en_pin)?;
        // EN is active LOW — set LOW to enable motor driver
        en.set_low()?;

        let channel = TxChannelDriver::new(
            step_pin,
            &TxChannelConfig {
                resolution: config::RMT_RESOLUTION.Hz(),
                transaction_queue_depth: 2,
                memory_access: MemoryAccess::Indirect {
                    memory_block_symbols: 128,
                },
                ..Default::default()
            },
        )?;

        Ok(Self {
            channel,
            dir,
            en,
            stop_flag: None,
            position: AtomicI32::new(0),
            motor_enabled: AtomicBool::new(true), // set_low in constructor = enabled
        })
    }

    /// Enable the motor driver (set EN LOW).
    ///
    /// # Errors
    ///
    /// Returns `StepperError::Rmt` on GPIO error.
    pub fn enable(&mut self) -> Result<(), StepperError> {
        self.en.set_low()?;
        self.motor_enabled.store(true, Ordering::Release);
        Ok(())
    }

    /// Disable the motor driver: stop RMT, then set EN HIGH.
    ///
    /// # Errors
    ///
    /// Returns `StepperError::Rmt` on GPIO or RMT error.
    pub fn disable(&mut self) -> Result<(), StepperError> {
        self.channel.disable()?;
        self.en.set_high()?;
        self.motor_enabled.store(false, Ordering::Release);
        Ok(())
    }

    /// Emergency stop: immediately disable the RMT channel.
    ///
    /// Leaves EN LOW (motor driver powered but no pulses).
    /// The motor will hold position with current coil energisation.
    ///
    /// # Errors
    ///
    /// Returns `StepperError::Rmt` on RMT error.
    pub fn emergency_stop(&mut self) -> Result<(), StepperError> {
        self.channel.disable()?;
        Ok(())
    }

    /// Set the motor direction.
    ///
    /// - `Direction::LiqIn`  → DIR = HIGH (fill from bottle).
    /// - `Direction::LiqOut` → DIR = LOW (dispense to vessel).
    pub fn set_direction(&mut self, dir: Direction) {
        match dir {
            Direction::LiqIn => {
                self.dir.set_high().ok();
            }
            Direction::LiqOut => {
                self.dir.set_low().ok();
            }
        }
    }

    /// Attach a stop flag (typically from a `LimitSwitch` static atomic).
    ///
    /// Before each RMT chunk, `move_steps_intervals()` checks this flag. If `true`,
    /// it returns `StepperError::LimitSwitchReached`.
    pub const fn set_stop_flag(&mut self, flag: &'static AtomicBool) {
        self.stop_flag = Some(flag);
    }

    /// Remove the stop flag (disable limit-switch polling during motion).
    pub const fn clear_stop_flag(&mut self) {
        self.stop_flag = None;
    }

    /// Move the motor using a pre-computed sequence of step intervals.
    ///
    /// Each entry in `intervals_us` is the total period for one step in
    /// microseconds. The RMT hardware generates the pulse train with zero
    /// CPU load during transmission.
    ///
    /// # Symbol Encoding
    ///
    /// Each interval is converted to two RMT symbols:
    /// 1. `{Pulse::High, ticks = RMT_PULSE_WIDTH_TICKS}` (1 µs pulse).
    /// 2. `{Pulse::Low, ticks = interval - RMT_PULSE_WIDTH_TICKS}`.
    ///
    /// Symbols are batched into chunks of `RMT_CHUNK_MAX` and sent via
    /// `CopyEncoder` + `channel.send_and_wait()`.
    ///
    /// # Stop Flag Check
    ///
    /// Before each chunk, the stop flag (if set) is checked with
    /// `Ordering::Acquire`. If `true`, motion stops immediately and
    /// `StepperError::LimitSwitchReached` is returned.
    ///
    /// # Empty Input
    ///
    /// An empty slice returns `Ok(())` immediately with no RMT operations.
    ///
    /// # BLOCKING
    ///
    /// This function blocks during `send_and_wait()`. It requires
    /// `&MotorContext` and MUST be called from a dedicated motor thread,
    /// NOT from the main loop.
    ///
    /// # Errors
    ///
    /// - `StepperError::LimitSwitchReached` — stop flag was set.
    /// - `StepperError::Rmt` — RMT transmit or encoder error.
    pub fn move_steps_intervals(
        &mut self,
        _ctx: &MotorContext,
        intervals_us: &[u32],
    ) -> Result<(), StepperError> {
        if intervals_us.is_empty() {
            return Ok(());
        }

        // Pre-flight: verify GR-2 (stop flag) and GR-1 (motor thread)
        let _ = diag::preconditions::assert_rmt_preconditions(
            self.stop_flag,
            std::thread::current().name().unwrap_or("?"),
        );

        // Check stop flag before the first chunk
        if let Some(flag) = self.stop_flag {
            if flag.load(Ordering::Acquire) {
                return Err(StepperError::LimitSwitchReached);
            }
        }

        // Build and transmit chunks of symbols
        // SAFETY: `RMT_CHUNK_MAX` symbols per chunk is within RMT memory limits
        // (128 symbols fits in the indirect memory block).
        // Vec is allowed here per docs/refs/coding_style.md §5 (heap in motor thread).
        #[allow(clippy::disallowed_types)]
        let mut symbols = Vec::with_capacity(config::RMT_CHUNK_MAX);

        for &interval in intervals_us {
            if symbols.len() >= config::RMT_CHUNK_MAX {
                self.transmit(&symbols)?;
                symbols.clear();

                // Check stop flag between chunks
                if let Some(flag) = self.stop_flag {
                    if flag.load(Ordering::Acquire) {
                        return Err(StepperError::LimitSwitchReached);
                    }
                }
            }

            let low_ticks = (interval.saturating_sub(u32::from(config::RMT_PULSE_WIDTH_TICKS)))
                .min(32767) as u16;

            // Build the RMT symbol step by step, propagating each Result.
            // PulseTicks::new returns Result; Pulse::new returns Pulse directly.
            let high_pulse_ticks = PulseTicks::new(config::RMT_PULSE_WIDTH_TICKS)?;
            let low_pulse_ticks = PulseTicks::new(low_ticks)?;
            let level0 = Pulse::new(PinState::High, high_pulse_ticks);
            let level1 = Pulse::new(PinState::Low, low_pulse_ticks);
            let symbol = Symbol::new(level0, level1);
            symbols.push(symbol);
        }

        // Transmit remaining symbols
        if !symbols.is_empty() {
            self.transmit(&symbols)?;
        }

        Ok(())
    }

    /// Transmit a chunk of RMT symbols.
    ///
    /// Uses `CopyEncoder` and `send_and_wait()` for blocking transmission.
    fn transmit(&mut self, symbols: &[Symbol]) -> Result<(), StepperError> {
        let encoder = CopyEncoder::new()?;
        self.channel.send_and_wait(
            encoder,
            symbols,
            &TransmitConfig {
                loop_count: Loop::None,
                eot_level: false,
                ..Default::default()
            },
        )?;
        Ok(())
    }
}

impl Drop for RmtStepper<'_> {
    fn drop(&mut self) {
        // Disable the RMT channel first to stop any ongoing transmission.
        let _ = self.channel.disable();
        // Set EN HIGH to disable the motor driver (active LOW).
        let _ = self.en.set_high();
    }
}

impl StepperMotor for RmtStepper<'_> {
    fn move_steps(
        &mut self,
        ctx: &MotorContext,
        steps: Steps,
        speed: Hz,
    ) -> Result<(), StepperError> {
        if steps.0 == 0 {
            return Ok(());
        }

        // Set direction based on sign of steps
        if steps.0 > 0 {
            self.set_direction(Direction::LiqIn);
        } else {
            self.set_direction(Direction::LiqOut);
        }

        // Compute ramp from Steps and Hz
        let ramp_config = RampConfig::new(
            config::RAMP_ACCEL_STEPS,
            config::RAMP_DECEL_STEPS,
            speed.0,
            config::STEPPER_MIN_HZ,
        );
        let intervals = compute_ramp(steps.abs(), &ramp_config);
        self.move_steps_intervals(ctx, &intervals)?;

        // Update position after successful motion (steps is signed)
        self.position.fetch_add(steps.0, Ordering::Release);

        Ok(())
    }

    fn stop(&mut self) -> Result<(), StepperError> {
        self.emergency_stop()?;
        Ok(())
    }

    fn position(&self) -> Steps {
        Steps(self.position.load(Ordering::Acquire))
    }

    fn enabled(&self) -> bool {
        self.motor_enabled.load(Ordering::Acquire)
    }
}

// ── Tests ──────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn direction_liq_in_maps_to_true() {
        // Direction::LiqIn must convert to `true` (HIGH pin level).
        assert!(bool::from(Direction::LiqIn));
    }

    #[test]
    fn direction_liq_out_maps_to_false() {
        // Direction::LiqOut must convert to `false` (LOW pin level).
        assert!(!bool::from(Direction::LiqOut));
    }

    #[test]
    fn atomic_bool_default_is_false() {
        // AtomicBool::new(false) must load as false.
        let flag = AtomicBool::new(false);
        assert!(!flag.load(Ordering::Acquire));
    }

    #[test]
    fn atomic_bool_store_and_load() {
        // AtomicBool must support store(true) / load(Acquire) round-trip.
        let flag = AtomicBool::new(false);
        flag.store(true, Ordering::Release);
        assert!(flag.load(Ordering::Acquire));
    }

    #[test]
    fn atomic_i32_default_is_zero() {
        // AtomicI32::new(0) must load as 0.
        let pos = AtomicI32::new(0);
        assert_eq!(pos.load(Ordering::Acquire), 0);
    }

    #[test]
    fn atomic_i32_fetch_add() {
        // AtomicI32 must support fetch_add round-trip.
        let pos = AtomicI32::new(10);
        let prev = pos.fetch_add(5, Ordering::Release);
        assert_eq!(prev, 10);
        assert_eq!(pos.load(Ordering::Acquire), 15);
    }

    #[test]
    fn steps_abs_positive() {
        // Steps::abs must return unsigned absolute value.
        assert_eq!(Steps(42).abs(), 42);
    }

    #[test]
    fn steps_abs_negative() {
        // Steps::abs must return unsigned absolute value for negative input.
        assert_eq!(Steps(-42).abs(), 42);
    }
}
