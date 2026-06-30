//! ADC calibration coefficients (lock-free atomic storage).
//!
//! Provides four free functions to get/set/reset calibration, and to
//! compute calibrated millivolt values from raw ADC readings.
//!
//! Stored as fixed-point integers to avoid `f32` atomics:
//! - Slope `a` = `COEFF_A_X1000 / 1000` (stored as `u16`).
//! - Offset `b` = `COEFF_B` (stored as `i16`).
//!
//! This module has NO hardware dependencies — it compiles on any target.

use core::sync::atomic::{AtomicI16, AtomicU16, Ordering};

// ── Default calibration constants ──────────────────────────────

const DEFAULT_A_X1000: u16 = 1000; // a = 1.0
const DEFAULT_B: i16 = 0;

// ── Atomic calibration coefficients ────────────────────────────

/// Calibration slope × 1000 (stored as integer for lock-free atomic access).
pub(crate) static COEFF_A_X1000: AtomicU16 = AtomicU16::new(DEFAULT_A_X1000);

/// Calibration offset in millivolts.
pub(crate) static COEFF_B: AtomicI16 = AtomicI16::new(DEFAULT_B);

/// Set the calibration coefficients.
///
/// - `a`: slope multiplier.
/// - `b`: offset in mV.
///
/// Uses round-to-nearest (add 0.5 before truncation) to minimise
/// quantisation error when converting from floating-point.
#[allow(clippy::cast_possible_truncation, clippy::cast_sign_loss)]
pub fn set_calibration(a: f32, b: f32) {
    COEFF_A_X1000.store((a.mul_add(1000.0, 0.5)) as u16, Ordering::Relaxed);
    COEFF_B.store((b + 0.5) as i16, Ordering::Relaxed);
}

/// Get the current calibration coefficients.
pub fn get_calibration() -> (f32, f32) {
    let a = f32::from(COEFF_A_X1000.load(Ordering::Relaxed)) / 1000.0;
    let b = f32::from(COEFF_B.load(Ordering::Relaxed));
    (a, b)
}

/// Reset calibration coefficients to defaults (a = 1.0, b = 0).
pub fn reset_calibration() {
    COEFF_A_X1000.store(DEFAULT_A_X1000, Ordering::Relaxed);
    COEFF_B.store(DEFAULT_B, Ordering::Relaxed);
}

/// Compute calibrated mV from a given raw value using the current coefficients.
#[allow(clippy::cast_possible_truncation)]
pub fn calibrated_from_raw(raw: u16) -> i16 {
    let a = i32::from(COEFF_A_X1000.load(Ordering::Relaxed));
    let b = i32::from(COEFF_B.load(Ordering::Relaxed));
    let result = (a * i32::from(raw)) / 1000 + b;
    result.clamp(i32::from(i16::MIN), i32::from(i16::MAX)) as i16
}

// ── Tests ──────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_calibration_defaults() {
        reset_calibration();
        let (a, b) = get_calibration();
        assert!((a - 1.0).abs() < 0.001);
        assert!((b - 0.0).abs() < 0.001);
    }

    #[test]
    fn test_set_and_get_calibration() {
        set_calibration(1.5, 2.0);
        let (a, b) = get_calibration();
        assert!((a - 1.5).abs() < 0.001);
        assert!((b - 2.0).abs() < 0.001);

        reset_calibration();
        let (a, b) = get_calibration();
        assert!((a - 1.0).abs() < 0.001);
        assert!((b - 0.0).abs() < 0.001);
    }

    #[test]
    fn test_calibrated_from_raw_identity() {
        reset_calibration();
        assert_eq!(calibrated_from_raw(1000), 1000);
        assert_eq!(calibrated_from_raw(0), 0);
        assert_eq!(calibrated_from_raw(2450), 2450);
    }

    #[test]
    fn test_calibrated_from_raw_scaled() {
        set_calibration(2.0, 0.0);
        assert_eq!(calibrated_from_raw(1000), 2000);
        set_calibration(1.0, 500.0);
        assert_eq!(calibrated_from_raw(1000), 1500);
        reset_calibration();
    }

    #[test]
    fn test_calibrated_from_raw_clamp() {
        set_calibration(100.0, 0.0);
        let result = calibrated_from_raw(1000);
        assert!(result <= i16::MAX);
        reset_calibration();
    }
}
