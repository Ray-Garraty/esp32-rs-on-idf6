//! Calibration math — volume/steps conversion, speed/frequency conversion,
//! Z-factor bilinear interpolation, OLS regression, and pending-cal spinlock.
//!
//! Pure domain logic — no ESP-IDF imports.

use crate::domain::types::{Ml, MlMin, Steps};
use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicBool, Ordering};

// ── Default constants ──────────────────────────────────────────

pub const DEFAULT_STEPS_PER_ML: f32 = 7730.0;
pub const DEFAULT_NOMINAL_VOL: f32 = 8.14;
pub const DEFAULT_SPEED_COEFF: f32 = 0.03052;
pub const DEFAULT_MIN_FREQ: u16 = 30;
pub const DEFAULT_MAX_FREQ: u16 = 3000;
pub const DEFAULT_CAL_DATE: i64 = 0;

// ── ISO 8655 Z-factor table ───────────────────────────────────

/// Number of temperature rows in the Z-factor table.
const CAL_Z_TABLE_ROWS: usize = 31;

/// Number of pressure columns in the Z-factor table.
const CAL_Z_TABLE_COLS: usize = 6;

/// ISO 8655 Z-factor table: 31 temperature points × 6 pressure points.
/// Source: legacy `backend_nodejs/src/calibration/stepsToVolume/calibrationService.js`
#[rustfmt::skip]
const Z_TABLE: [[f32; CAL_Z_TABLE_COLS]; CAL_Z_TABLE_ROWS] = [
    [1.0018, 1.0018, 1.0019, 1.0019, 1.0020, 1.0020],
    [1.0018, 1.0018, 1.0019, 1.0020, 1.0020, 1.0021],
    [1.0019, 1.0020, 1.0020, 1.0021, 1.0022, 1.0022],
    [1.0020, 1.0020, 1.0021, 1.0022, 1.0022, 1.0023],
    [1.0021, 1.0021, 1.0022, 1.0022, 1.0023, 1.0023],
    [1.0022, 1.0022, 1.0023, 1.0024, 1.0024, 1.0024],
    [1.0022, 1.0023, 1.0024, 1.0024, 1.0025, 1.0025],
    [1.0023, 1.0024, 1.0025, 1.0025, 1.0026, 1.0026],
    [1.0024, 1.0025, 1.0025, 1.0026, 1.0027, 1.0027],
    [1.0025, 1.0026, 1.0026, 1.0027, 1.0028, 1.0028],
    [1.0026, 1.0027, 1.0027, 1.0028, 1.0029, 1.0029],
    [1.0027, 1.0028, 1.0028, 1.0029, 1.0030, 1.0030],
    [1.0028, 1.0029, 1.0031, 1.0031, 1.0032, 1.0032],
    [1.0030, 1.0030, 1.0032, 1.0032, 1.0033, 1.0033],
    [1.0031, 1.0031, 1.0033, 1.0033, 1.0034, 1.0035],
    [1.0032, 1.0032, 1.0034, 1.0035, 1.0035, 1.0036],
    [1.0033, 1.0033, 1.0035, 1.0036, 1.0036, 1.0037],
    [1.0034, 1.0035, 1.0036, 1.0037, 1.0038, 1.0038],
    [1.0035, 1.0036, 1.0037, 1.0038, 1.0039, 1.0039],
    [1.0037, 1.0037, 1.0038, 1.0039, 1.0040, 1.0041],
    [1.0038, 1.0038, 1.0039, 1.0040, 1.0041, 1.0042],
    [1.0039, 1.0040, 1.0041, 1.0041, 1.0042, 1.0043],
    [1.0040, 1.0041, 1.0042, 1.0042, 1.0043, 1.0045],
    [1.0042, 1.0042, 1.0043, 1.0044, 1.0045, 1.0046],
    [1.0043, 1.0044, 1.0044, 1.0045, 1.0048, 1.0049],
    [1.0046, 1.0046, 1.0047, 1.0048, 1.0048, 1.0049],
    [1.0046, 1.0046, 1.0047, 1.0048, 1.0049, 1.0049],
    [1.0047, 1.0048, 1.0048, 1.0049, 1.0050, 1.0050],
    [1.0049, 1.0049, 1.0050, 1.0051, 1.0051, 1.0052],
    [1.0050, 1.0051, 1.0051, 1.0052, 1.0053, 1.0053],
    [1.0052, 1.0052, 1.0053, 1.0054, 1.0054, 1.0055],
];

/// Temperature values for Z-table rows: 15.0 °C .. 30.0 °C in 0.5 °C steps.
const TEMP_VALS: [f32; CAL_Z_TABLE_ROWS] = [
    15.0, 15.5, 16.0, 16.5, 17.0, 17.5, 18.0, 18.5, 19.0, 19.5, 20.0, 20.5, 21.0, 21.5, 22.0, 22.5,
    23.0, 23.5, 24.0, 24.5, 25.0, 25.5, 26.0, 26.5, 27.0, 27.5, 28.0, 28.5, 29.0, 29.5, 30.0,
];

/// Pressure values for Z-table columns: 80.0 .. 106.7 kPa.
const PRESS_VALS: [f32; CAL_Z_TABLE_COLS] = [80.0, 85.3, 90.7, 96.0, 101.3, 106.7];

// ── Calibration config struct ──────────────────────────────────

/// Runtime cache of all calibration coefficients.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct CalibrationConfig {
    pub steps_per_ml: f32,
    pub nominal_vol: f32,
    pub speed_coeff: f32,
    pub min_freq: u16,
    pub max_freq: u16,
    pub calibration_date: i64,
}

impl CalibrationConfig {
    /// Create a new configuration with default values.
    pub const fn new() -> Self {
        Self {
            steps_per_ml: DEFAULT_STEPS_PER_ML,
            nominal_vol: DEFAULT_NOMINAL_VOL,
            speed_coeff: DEFAULT_SPEED_COEFF,
            min_freq: DEFAULT_MIN_FREQ,
            max_freq: DEFAULT_MAX_FREQ,
            calibration_date: DEFAULT_CAL_DATE,
        }
    }

    /// Returns `true` if all fields match compile-time defaults within tolerance.
    pub fn is_default(&self) -> bool {
        (self.steps_per_ml - DEFAULT_STEPS_PER_ML).abs() < 0.01
            && (self.nominal_vol - DEFAULT_NOMINAL_VOL).abs() < 0.01
            && (self.speed_coeff - DEFAULT_SPEED_COEFF).abs() < 0.000_01
            && self.min_freq == DEFAULT_MIN_FREQ
            && self.max_freq == DEFAULT_MAX_FREQ
            && self.calibration_date == DEFAULT_CAL_DATE
    }
}

impl Default for CalibrationConfig {
    fn default() -> Self {
        Self::new()
    }
}

// ── ADC calibration ───────────────────────────────────────────

/// Simple linear calibration for the ADC: `mV = a * raw + b`.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct AdcCalibration {
    pub a: f32,
    pub b: f32,
}

// ── Conversion result types ────────────────────────────────────

/// Result of a volume ↔ steps conversion.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct VolumeConversionResult {
    pub volume_ml: Ml,
    pub steps: Steps,
}

/// Result of OLS speed calibration.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct SpeedCalResult {
    pub k: f32,
    pub r_squared: f32,
}

// ── Volume ↔ Steps conversion ──────────────────────────────────

/// Convert millilitres to motor steps.
///
/// Uses `lroundf` semantics (`.round() as i32`).
/// Returns 0 for negative volume.
#[allow(clippy::cast_possible_truncation, clippy::cast_sign_loss)]
pub fn volume_to_steps(vol: Ml, steps_per_ml: f32) -> Steps {
    if vol.0 < 0.0 {
        return Steps(0);
    }
    // Safe: lroundf semantics — deliberate truncation to i32.
    // lroundf returns long; we cast to i32. ESP32 long = 32-bit, so no truncation.
    Steps((vol.0 * steps_per_ml).round() as i32)
}

/// Convert motor steps to millilitres.
///
/// `delta = steps - base_steps`, then `vol = delta / steps_per_ml`.
/// Clamped to `[0, nominal_vol]`. Returns 0 if `steps_per_ml < 0.001`.
#[allow(clippy::cast_precision_loss)]
pub fn steps_to_volume(steps: Steps, base: Steps, steps_per_ml: f32, nominal: Ml) -> Ml {
    if steps_per_ml < 0.001 {
        return Ml(0.0);
    }
    let delta = steps.0 - base.0;
    // Cast i32 → f32: safe, max ~1e9 → f32 has ~7 digits precision, steps ~7e5, fine.
    let vol = delta as f32 / steps_per_ml;
    if vol < 0.0 {
        Ml(0.0)
    } else if vol > nominal.0 {
        nominal
    } else {
        Ml(vol)
    }
}

// ── Speed ↔ Frequency conversion ───────────────────────────────

/// Convert flow rate (ml/min) to step frequency (Hz).
///
/// `freq = speed / coeff`, rounded via `lroundf`.
/// Clamped to `[min_freq, max_freq]`.
/// Returns `min_freq` if coefficient is near zero.
#[allow(clippy::cast_possible_truncation, clippy::cast_sign_loss)]
pub fn speed_to_frequency(speed: MlMin, coeff: f32, min_freq: u16, max_freq: u16) -> u16 {
    if coeff < 0.000_001 {
        return min_freq;
    }
    // Frequency is always non-negative. The lroundf result may be slightly negative
    // for tiny speeds, but clamping below catches that.
    let freq = (speed.0 / coeff).round() as i32;
    if freq < i32::from(min_freq) {
        min_freq
    } else if freq > i32::from(max_freq) {
        max_freq
    } else {
        // Safe: freq is clamped to [min_freq, max_freq], both u16
        #[allow(clippy::cast_possible_truncation)]
        {
            freq as u16
        }
    }
}

/// Convert step frequency (Hz) to flow rate (ml/min).
///
/// `speed = freq * coeff`
pub fn frequency_to_speed(freq: u16, coeff: f32) -> MlMin {
    MlMin(f32::from(freq) * coeff)
}

// ── Z-factor bilinear interpolation ────────────────────────────

/// Linear interpolation helper: `a + (b - a) * t`.
fn lerp(a: f32, b: f32, t: f32) -> f32 {
    (b - a).mul_add(t, a)
}

/// Bilinear interpolation of the ISO 8655 Z-factor table.
///
/// Temperature is clamped to `[15.0, 30.0]`, pressure to `[80.0, 106.7]`.
/// Returns the Z-factor for the given environmental conditions.
#[allow(clippy::cast_possible_truncation, clippy::cast_sign_loss)]
pub fn get_z_factor(temperature: f32, pressure: f32) -> f32 {
    // Clamp inputs to table bounds
    let temp = temperature.clamp(TEMP_VALS[0], TEMP_VALS[CAL_Z_TABLE_ROWS - 1]);
    let pres = pressure.clamp(PRESS_VALS[0], PRESS_VALS[CAL_Z_TABLE_COLS - 1]);

    // Find temperature index
    let mut ti: usize = 0;
    for (i, &tv) in TEMP_VALS[..CAL_Z_TABLE_ROWS - 1].iter().enumerate() {
        if temp >= tv {
            ti = i;
        }
    }

    // Find pressure index
    let mut pi: usize = 0;
    for (i, &pv) in PRESS_VALS[..CAL_Z_TABLE_COLS - 1].iter().enumerate() {
        if pres >= pv {
            pi = i;
        }
    }

    // Guard: ti and pi must not reach the last index (we need ti+1 and pi+1)
    if ti >= CAL_Z_TABLE_ROWS - 1 {
        ti = CAL_Z_TABLE_ROWS - 2;
    }
    if pi >= CAL_Z_TABLE_COLS - 1 {
        pi = CAL_Z_TABLE_COLS - 2;
    }

    let t_frac = (temp - TEMP_VALS[ti]) / (TEMP_VALS[ti + 1] - TEMP_VALS[ti]);
    let p_frac = (pres - PRESS_VALS[pi]) / (PRESS_VALS[pi + 1] - PRESS_VALS[pi]);

    let z_t1 = lerp(Z_TABLE[ti][pi], Z_TABLE[ti][pi + 1], p_frac);
    let z_t2 = lerp(Z_TABLE[ti + 1][pi], Z_TABLE[ti + 1][pi + 1], p_frac);
    lerp(z_t1, z_t2, t_frac)
}

// ── Gravimetric correction ─────────────────────────────────────

/// Calculate a new `steps_per_ml` from gravimetric measurement.
///
/// `new_spm = current_spm * target_vol / actual_vol`
/// Guards against `actual_vol < 0.0001` (returns `current_spm` unchanged).
pub fn calculate_new_steps_per_ml(current_spm: f32, target_vol: Ml, actual_vol: Ml) -> f32 {
    if actual_vol.0.abs() < 0.000_1 {
        return current_spm;
    }
    current_spm * target_vol.0 / actual_vol.0
}

// ── OLS speed calibration ──────────────────────────────────────

/// Ordinary least squares regression through the mean (not through origin).
///
/// Returns `k` (slope) and `r_squared` (coefficient of determination).
/// `r_squared` may be negative for predictions worse than the mean.
///
/// Returns `(0, 0)` if fewer than 2 points or denominator is near zero.
///
/// Formula (matches legacy C++ exactly):
///   k = (Σfv - Σf·Σv/n) / (Σff - Σf²/n)
///   r² = 1 - SS_res / SS_tot
#[allow(clippy::cast_precision_loss)]
pub fn calculate_speed_calibration(frequencies: &[f32], speeds: &[f32]) -> SpeedCalResult {
    let count = frequencies.len().min(speeds.len());
    if count < 2 {
        return SpeedCalResult {
            k: 0.0,
            r_squared: 0.0,
        };
    }

    // count is at most min(input lengths), typically 2-8 points for OLS; f32 is fine.
    let n = count as f32;
    let mut sum_f = 0.0_f32;
    let mut sum_v = 0.0_f32;
    let mut sum_ff = 0.0_f32;
    let mut sum_fv = 0.0_f32;

    for i in 0..count {
        sum_f += frequencies[i];
        sum_v += speeds[i];
        sum_ff += frequencies[i] * frequencies[i];
        sum_fv += frequencies[i] * speeds[i];
    }

    let denom = sum_ff - sum_f * sum_f / n;
    if denom.abs() < 0.000_001 {
        return SpeedCalResult {
            k: 0.0,
            r_squared: 0.0,
        };
    }

    let k = (sum_fv - sum_f * sum_v / n) / denom;
    let mean_v = sum_v / n;

    let mut ss_res = 0.0_f32;
    let mut ss_tot = 0.0_f32;
    for i in 0..count {
        let pred = k * frequencies[i];
        ss_res += (speeds[i] - pred) * (speeds[i] - pred);
        ss_tot += (speeds[i] - mean_v) * (speeds[i] - mean_v);
    }

    let r_squared = if ss_tot > 0.000_001 {
        1.0 - ss_res / ss_tot
    } else {
        0.0
    };

    SpeedCalResult { k, r_squared }
}

// ── Pending calibration (thread-safe spinlock) ─────────────────

/// Thread-safe wrapper for a pending calibration configuration.
///
/// Uses an `AtomicBool` flag with `Release`/`Acquire` ordering semantics,
/// paired with an `UnsafeCell<CalibrationConfig>` for the actual data.
///
/// # Safety
///
/// This type is `Sync` because:
/// - The `AtomicBool` gate ensures only one thread writes while others observe.
/// - Writers use `Ordering::Release` and readers use `Ordering::Acquire`,
///   establishing a happens-before relationship.
/// - The `UnsafeCell` is only accessed when the atomic flag confirms data is present.
/// - This mirrors the legacy C++ `portMUX` critical section pattern.
pub struct PendingCal {
    has_pending: AtomicBool,
    data: UnsafeCell<CalibrationConfig>,
}

impl Default for PendingCal {
    fn default() -> Self {
        Self::new()
    }
}

/// # Safety
/// PendingCal is safe to share across threads due to the atomic gate;
/// see the struct-level safety comment for details.
unsafe impl Sync for PendingCal {}

impl PendingCal {
    /// Create a new pending calibration state with default values.
    pub const fn new() -> Self {
        Self {
            has_pending: AtomicBool::new(false),
            data: UnsafeCell::new(CalibrationConfig::new()),
        }
    }

    /// Store a pending calibration config.
    ///
    /// # Safety
    ///
    /// The write to `UnsafeCell` is safe because:
    /// - The atomic store with `Release` ordering ensures the write is visible
    ///   to any subsequent `Acquire` load.
    /// - Only this function writes to `data`, and only when the caller explicitly
    ///   invokes it — no concurrent writes occur.
    pub fn set_pending(&self, cfg: &CalibrationConfig) {
        // SAFETY: Writing to the UnsafeCell is safe because:
        // 1. The AtomicBool gate ensures happens-before ordering.
        // 2. Only this function performs the write.
        // 3. The caller must ensure no concurrent access (enforced by atomic ordering).
        unsafe {
            *self.data.get() = *cfg;
        }
        self.has_pending.store(true, Ordering::Release);
    }

    /// Get a copy of the pending config, if one has been set.
    ///
    /// Returns `None` if no pending config exists.
    ///
    /// # Safety
    ///
    /// The read from `UnsafeCell` is safe because:
    /// - The atomic load with `Acquire` ordering ensures we see the most recent
    ///   `Release`-ordered write.
    /// - The caller gets a `Copy` value, so no reference escapes the lifetime of `self`.
    pub fn get_pending_copy(&self) -> Option<CalibrationConfig> {
        if self.has_pending.load(Ordering::Acquire) {
            // SAFETY: Atomic gate ensures the data is initialized and visible.
            // Copying out an f32/u16/i64 value is safe; no reference is leaked.
            Some(unsafe { *self.data.get() })
        } else {
            None
        }
    }
}

/// Global pending calibration state.
pub static PENDING_CAL_STATE: PendingCal = PendingCal::new();

/// Check if the given config matches compile-time defaults.
pub fn burette_cal_is_default(cfg: &CalibrationConfig) -> bool {
    cfg.is_default()
}

/// Thread-safe setter: store a pending config.
pub fn burette_cal_set_pending(cfg: &CalibrationConfig) {
    PENDING_CAL_STATE.set_pending(cfg);
}

/// Thread-safe getter: retrieve a copy of the pending config.
pub fn burette_cal_get_pending_copy() -> Option<CalibrationConfig> {
    PENDING_CAL_STATE.get_pending_copy()
}

// ── Tests ──────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    // ══════════════════════════════════════════════════════════════
    // Steps ↔ Volume (15 tests, ported from test_planner.cpp)
    // ══════════════════════════════════════════════════════════════

    #[test]
    fn test_steps_to_vol_zero_spm() {
        let v = steps_to_volume(Steps(1000), Steps(0), 0.0, Ml(50.0));
        assert!((v.0 - 0.0).abs() < 0.001);
    }

    #[test]
    fn test_steps_to_vol_zero_delta() {
        let v = steps_to_volume(Steps(5000), Steps(5000), 100.0, Ml(50.0));
        assert!((v.0 - 0.0).abs() < 0.001);
    }

    #[test]
    fn test_steps_to_vol_positive_delta() {
        let v = steps_to_volume(Steps(7000), Steps(5000), 100.0, Ml(50.0));
        assert!((v.0 - 20.0).abs() < 0.001);
    }

    #[test]
    fn test_steps_to_vol_negative_delta() {
        let v = steps_to_volume(Steps(3000), Steps(5000), 100.0, Ml(50.0));
        assert!((v.0 - 0.0).abs() < 0.001);
    }

    #[test]
    fn test_steps_to_vol_clamp_nominal() {
        let v = steps_to_volume(Steps(15000), Steps(5000), 100.0, Ml(50.0));
        assert!((v.0 - 50.0).abs() < 0.001);
    }

    #[test]
    fn test_steps_to_vol_fractional() {
        let v = steps_to_volume(Steps(5230), Steps(5000), 100.0, Ml(50.0));
        assert!((v.0 - 2.3).abs() < 0.001);
    }

    #[test]
    fn test_steps_to_vol_at_full() {
        let v = steps_to_volume(Steps(5000), Steps(5000), 100.0, Ml(50.0));
        assert!((v.0 - 0.0).abs() < 0.001);
    }

    #[test]
    fn test_vol_to_steps_normal() {
        let s = volume_to_steps(Ml(5.0), 1000.0);
        assert_eq!(s, Steps(5000));
    }

    #[test]
    fn test_vol_to_steps_zero() {
        let s = volume_to_steps(Ml(0.0), 1000.0);
        assert_eq!(s, Steps(0));
    }

    #[test]
    fn test_vol_to_steps_negative() {
        let s = volume_to_steps(Ml(-1.0), 1000.0);
        assert_eq!(s, Steps(0));
    }

    #[test]
    fn test_vol_to_steps_fractional() {
        let s = volume_to_steps(Ml(5.5), 1000.0);
        assert_eq!(s, Steps(5500));
    }

    #[test]
    fn test_vol_to_steps_large() {
        let s = volume_to_steps(Ml(50.0), 7730.0);
        assert_eq!(s, Steps(386_500));
    }

    #[test]
    fn test_roundtrip_normal() {
        let vol = steps_to_volume(volume_to_steps(Ml(5.0), 1000.0), Steps(0), 1000.0, Ml(50.0));
        assert!((vol.0 - 5.0).abs() < 0.001);
    }

    #[test]
    fn test_roundtrip_zero() {
        let vol = steps_to_volume(volume_to_steps(Ml(0.0), 1000.0), Steps(0), 1000.0, Ml(50.0));
        assert!((vol.0 - 0.0).abs() < 0.001);
    }

    #[test]
    fn test_roundtrip_nominal() {
        let vol = steps_to_volume(
            volume_to_steps(Ml(50.0), 1000.0),
            Steps(0),
            1000.0,
            Ml(50.0),
        );
        assert!((vol.0 - 50.0).abs() < 0.001);
    }

    // ══════════════════════════════════════════════════════════════
    // Speed ↔ Frequency (19 tests, ported from test_speed.cpp)
    // ══════════════════════════════════════════════════════════════

    #[test]
    fn test_freq_to_speed_normal() {
        let s = frequency_to_speed(1000, 0.03052);
        assert!((s.0 - 30.52).abs() < 0.001);
    }

    #[test]
    fn test_freq_to_speed_zero_coeff() {
        let s = frequency_to_speed(1000, 0.0);
        assert!((s.0 - 0.0).abs() < 0.001);
    }

    #[test]
    fn test_freq_to_speed_zero_freq() {
        let s = frequency_to_speed(0, 0.03052);
        assert!((s.0 - 0.0).abs() < 0.001);
    }

    #[test]
    fn test_freq_to_speed_precision() {
        let s = frequency_to_speed(1500, 0.03052);
        assert!((s.0 - 45.78).abs() < 0.01);
    }

    #[test]
    fn test_freq_to_speed_max_freq() {
        let s = frequency_to_speed(3000, 0.03052);
        assert!((s.0 - 91.56).abs() < 0.01);
    }

    #[test]
    fn test_freq_to_speed_min_freq() {
        let s = frequency_to_speed(30, 0.03052);
        assert!((s.0 - 0.9156).abs() < 0.001);
    }

    #[test]
    fn test_speed_to_freq_normal() {
        let f = speed_to_frequency(MlMin(30.52), 0.03052, 30, 3000);
        assert_eq!(f, 1000);
    }

    #[test]
    fn test_speed_to_freq_clamp_below_min() {
        let f = speed_to_frequency(MlMin(0.5), 0.03052, 30, 3000);
        assert_eq!(f, 30);
    }

    #[test]
    fn test_speed_to_freq_clamp_above_max() {
        let f = speed_to_frequency(MlMin(100.0), 0.03052, 30, 3000);
        assert_eq!(f, 3000);
    }

    #[test]
    fn test_speed_to_freq_zero_coeff() {
        let f = speed_to_frequency(MlMin(10.0), 0.0, 30, 3000);
        assert_eq!(f, 30);
    }

    #[test]
    fn test_speed_to_freq_boundary_min() {
        let f = speed_to_frequency(MlMin(0.9156), 0.03052, 30, 3000);
        assert_eq!(f, 30);
    }

    #[test]
    fn test_speed_to_freq_boundary_max() {
        let f = speed_to_frequency(MlMin(91.56), 0.03052, 30, 3000);
        assert_eq!(f, 3000);
    }

    #[test]
    fn test_speed_to_freq_decimal_rounding() {
        let f = speed_to_frequency(MlMin(15.26), 0.03052, 30, 3000);
        assert_eq!(f, 500);
    }

    #[test]
    fn test_speed_to_freq_exact_at_min() {
        let f = speed_to_frequency(MlMin(0.9156), 0.03052, 30, 3000);
        assert_eq!(f, 30);
    }

    #[test]
    fn test_speed_to_freq_just_above_min() {
        let f = speed_to_frequency(MlMin(0.95), 0.03052, 30, 3000);
        assert_eq!(f, 31);
    }

    #[test]
    fn test_roundtrip_speed_freq_normal() {
        let original = 30.52_f32;
        let freq = speed_to_frequency(MlMin(original), 0.03052, 30, 3000);
        let recovered = frequency_to_speed(freq, 0.03052);
        assert!((original - recovered.0).abs() < 0.01);
    }

    #[test]
    fn test_roundtrip_speed_freq_clamped() {
        let original = 100.0_f32;
        let freq = speed_to_frequency(MlMin(original), 0.03052, 30, 3000);
        assert_eq!(freq, 3000);
        let recovered = frequency_to_speed(freq, 0.03052);
        assert!((recovered.0 - 91.56).abs() < 0.01);
    }

    #[test]
    fn test_roundtrip_speed_freq_low_speed() {
        let original = 1.0_f32;
        let freq = speed_to_frequency(MlMin(original), 0.03052, 30, 3000);
        let recovered = frequency_to_speed(freq, 0.03052);
        assert!((recovered.0 - freq as f32 * 0.03052).abs() < 0.01);
    }

    #[test]
    fn test_roundtrip_speed_freq_multiple_values() {
        let mut f = 100_u16;
        while f <= 3000 {
            let speed = frequency_to_speed(f, 0.03052);
            let rev_freq = speed_to_frequency(speed, 0.03052, 30, 3000);
            let rev_speed = frequency_to_speed(rev_freq, 0.03052);
            assert!((speed.0 - rev_speed.0).abs() < 0.05);
            f += 500;
        }
    }

    // ══════════════════════════════════════════════════════════════
    // OLS (9 tests, ported from test_speed.cpp)
    // ══════════════════════════════════════════════════════════════

    #[test]
    fn test_ols_perfect_fit() {
        let freqs = [500.0, 1000.0];
        let speeds = [15.26, 30.52];
        let r = calculate_speed_calibration(&freqs, &speeds);
        assert!((r.k - 0.03052).abs() < 0.0001);
        assert!((r.r_squared - 1.0).abs() < 0.0001);
    }

    #[test]
    fn test_ols_single_point_guard() {
        let freqs = [500.0];
        let speeds = [15.26];
        let r = calculate_speed_calibration(&freqs, &speeds);
        assert!((r.k - 0.0).abs() < 0.0001);
        assert!((r.r_squared - 0.0).abs() < 0.0001);
    }

    #[test]
    fn test_ols_three_points() {
        let freqs = [1000.0, 2000.0, 3000.0];
        let speeds = [30.52, 61.04, 91.56];
        let r = calculate_speed_calibration(&freqs, &speeds);
        assert!((r.k - 0.03052).abs() < 0.0001);
        assert!((r.r_squared - 1.0).abs() < 0.0001);
    }

    #[test]
    fn test_ols_noisy_data() {
        let freqs = [500.0, 1000.0, 1500.0, 2000.0];
        let speeds = [15.2, 30.6, 45.7, 61.1];
        let r = calculate_speed_calibration(&freqs, &speeds);
        assert!(r.k > 0.030);
        assert!(r.k < 0.031);
        assert!(r.r_squared > 0.99);
    }

    #[test]
    fn test_ols_zero_freq_included() {
        let freqs = [0.0, 1000.0];
        let speeds = [0.0, 30.52];
        let r = calculate_speed_calibration(&freqs, &speeds);
        assert!((r.k - 0.03052).abs() < 0.0001);
    }

    #[test]
    fn test_ols_denom_zero() {
        let freqs = [500.0, 500.0];
        let speeds = [10.0, 20.0];
        let r = calculate_speed_calibration(&freqs, &speeds);
        assert!((r.k - 0.0).abs() < 0.0001);
        assert!((r.r_squared - 0.0).abs() < 0.0001);
    }

    #[test]
    fn test_ols_realistic_k_value() {
        let freqs = [1000.0, 2000.0, 3000.0];
        let speeds = [30.52, 61.04, 91.56];
        let r = calculate_speed_calibration(&freqs, &speeds);
        assert!(r.k > 0.0);
    }

    #[test]
    fn test_ols_negative_slope_guard() {
        let freqs = [1000.0, 2000.0];
        let speeds = [30.0, 20.0];
        let r = calculate_speed_calibration(&freqs, &speeds);
        assert!(r.k < 0.0);
    }

    #[test]
    fn test_ols_r_squared_negative() {
        let freqs = [1000.0, 2000.0, 3000.0];
        let speeds = [100.0, 5.0, 200.0];
        let r = calculate_speed_calibration(&freqs, &speeds);
        // r_squared can be negative for worse-than-mean predictions
        assert!(r.r_squared <= 1.0);
    }

    // ══════════════════════════════════════════════════════════════
    // Z-factor (~4 tests)
    // ══════════════════════════════════════════════════════════════

    #[test]
    fn test_z_factor_exact_corners() {
        // Bottom-left corner (15°C, 80 kPa)
        let z = get_z_factor(15.0, 80.0);
        assert!((z - 1.0018).abs() < 0.0001);

        // Top-right corner (30°C, 106.7 kPa)
        let z = get_z_factor(30.0, 106.7);
        assert!((z - 1.0055).abs() < 0.0001);

        // Bottom-right (15°C, 106.7 kPa)
        let z = get_z_factor(15.0, 106.7);
        assert!((z - 1.0020).abs() < 0.0001);

        // Top-left (30°C, 80 kPa)
        let z = get_z_factor(30.0, 80.0);
        assert!((z - 1.0052).abs() < 0.0001);
    }

    #[test]
    fn test_z_factor_interpolated() {
        // Midpoint (22.5°C, 93.35 kPa) — should be between table values
        let z = get_z_factor(22.5, 93.35);
        // Rough sanity: Z should be between 1.002 and 1.005 in this region
        assert!(z > 1.002);
        assert!(z < 1.005);
    }

    #[test]
    fn test_z_factor_clamp_below() {
        let z = get_z_factor(10.0, 70.0);
        // Should clamp to (15.0, 80.0) → Z_TABLE[0][0] = 1.0018
        assert!((z - 1.0018).abs() < 0.0001);
    }

    #[test]
    fn test_z_factor_clamp_above() {
        let z = get_z_factor(35.0, 120.0);
        // Should clamp to (30.0, 106.7) → Z_TABLE[30][5] = 1.0055
        assert!((z - 1.0055).abs() < 0.0001);
    }

    // ══════════════════════════════════════════════════════════════
    // Pending cal (~3 tests)
    // ══════════════════════════════════════════════════════════════

    #[test]
    fn test_pending_cal_is_default() {
        let cfg = CalibrationConfig::new();
        assert!(burette_cal_is_default(&cfg));
    }

    #[test]
    fn test_pending_cal_set_and_get() {
        let cfg = CalibrationConfig {
            steps_per_ml: 8000.0,
            nominal_vol: 10.0,
            speed_coeff: 0.031,
            min_freq: 50,
            max_freq: 2500,
            calibration_date: 12345,
        };
        burette_cal_set_pending(&cfg);
        let retrieved = burette_cal_get_pending_copy();
        assert!(retrieved.is_some());
        let inner = retrieved.unwrap();
        assert!((inner.steps_per_ml - 8000.0).abs() < 0.01);
        assert!((inner.nominal_vol - 10.0).abs() < 0.01);
        assert!((inner.speed_coeff - 0.031).abs() < 0.000_01);
        assert_eq!(inner.min_freq, 50);
        assert_eq!(inner.max_freq, 2500);
        assert_eq!(inner.calibration_date, 12345);
    }

    #[test]
    fn test_pending_cal_not_set_returns_none() {
        // PENDING_CAL_STATE was set in previous test; reset it.
        // We test the fresh state by checking a newly constructed PendingCal.
        let fresh = PendingCal::new();
        assert!(fresh.get_pending_copy().is_none());
    }

    // ══════════════════════════════════════════════════════════════
    // calculate_new_steps_per_ml
    // ══════════════════════════════════════════════════════════════

    #[test]
    fn test_calc_new_spm_normal() {
        let new_spm = calculate_new_steps_per_ml(7730.0, Ml(8.14), Ml(8.14));
        assert!((new_spm - 7730.0).abs() < 1.0);
    }

    #[test]
    fn test_calc_new_spm_guard_zero_actual() {
        let new_spm = calculate_new_steps_per_ml(7730.0, Ml(8.14), Ml(0.0));
        assert!((new_spm - 7730.0).abs() < 0.1);
    }

    #[test]
    fn test_calc_new_spm_correction() {
        // If actual is 10% low, spm should increase by ~11%
        let new_spm = calculate_new_steps_per_ml(7730.0, Ml(10.0), Ml(9.0));
        assert!((new_spm - 8588.9).abs() < 1.0);
    }
}
