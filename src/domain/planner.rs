//! Command planning logic — ported from `legacy/src/burette_planner.cpp`.
//!
//! Pure domain logic: validates command parameters, computes multi-cycle plans,
//! and returns planning decisions. No ESP-IDF imports.

use crate::domain::types::{Ml, MlMin};

// ── Validation constants ───────────────────────────────────────

/// Minimum volume for a dose operation (ml).
pub const PLANNER_MIN_VOLUME_ML: f32 = 0.01;

/// Maximum volume for a dose operation (ml).
pub const PLANNER_MAX_VOLUME_ML: f32 = 50.0;

/// Minimum allowed speed (ml/min).
pub const PLANNER_MIN_SPEED_ML_MIN: f32 = 0.1;

/// Maximum allowed speed (ml/min).
pub const PLANNER_MAX_SPEED_ML_MIN: f32 = 20.0;

/// Epsilon for float comparisons in the planner.
pub const PLANNER_EPSILON_FLOAT: f32 = 0.001;

/// Threshold below which a residual volume is treated as a full cycle.
pub const PLANNER_RESIDUAL_THRESHOLD: f32 = 0.01;

// ── Action enums ───────────────────────────────────────────────

/// Decision for a dose volume plan.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DoseAction {
    /// Reject the plan — see `reject_reason`.
    Reject,
    /// Need to fill first before dosing.
    FillFirst,
    /// Can dose directly from current contents.
    Direct,
}

/// Decision for a simple action (fill, empty, or cal_speed_seq).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SimpleAction {
    /// Reject the plan — see `reject_reason`.
    Reject,
    /// Execute the action.
    Execute,
}

/// Decision for a calibration run.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CalAction {
    /// Reject the plan — see `reject_reason`.
    Reject,
    /// Calibrate dose volume.
    Dose,
    /// Calibrate speed.
    Speed,
}

// ── Plan structs ───────────────────────────────────────────────

/// Result of `plan_dose_volume`.
#[derive(Debug, Clone, PartialEq)]
pub struct DosePlan {
    pub action: DoseAction,
    pub reject_reason: Option<&'static str>,
    pub first_cycle_vol: Ml,
    pub total_cycles: u8,
    pub remaining_vol: Ml,
}

/// Result of `plan_fill` / `plan_empty`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SimplePlan {
    pub action: SimpleAction,
    pub reject_reason: Option<&'static str>,
}

/// Result of `plan_rinse`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RinsePlan {
    pub action: SimpleAction,
    pub reject_reason: Option<&'static str>,
    pub cycles: u8,
}

/// Result of `plan_cal_run`.
#[derive(Debug, Clone, PartialEq)]
pub struct CalRunPlan {
    pub action: CalAction,
    pub reject_reason: Option<&'static str>,
    pub freq_hz: u16,
    pub speed_ml_min: MlMin,
}

/// Result of `plan_cal_speed_seq`.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct CalSpeedSeqPlan {
    pub action: SimpleAction,
    pub reject_reason: Option<&'static str>,
    pub fill_speed_ml_min: MlMin,
}

// ── Helpers ────────────────────────────────────────────────────

/// Calculate the number of cycles needed for a given volume.
///
/// `ceil(volume / nominal)`, saturating cast to `u8`.
#[allow(clippy::cast_possible_truncation, clippy::cast_sign_loss)]
pub fn calc_total_cycles(volume: Ml, nominal: Ml) -> u8 {
    if nominal.0 <= 0.0 {
        return 0;
    }
    let cycles = (volume.0 / nominal.0).ceil();
    if cycles > 255.0 {
        255
    } else {
        cycles as u8
    }
}

/// Calculate the remaining volume after filling whole nominal cycles.
///
/// `fmod(volume, nominal)`. If the residual is below `PLANNER_RESIDUAL_THRESHOLD`,
/// returns `nominal` (i.e., the last cycle is a full one).
#[allow(clippy::cast_precision_loss)]
pub fn calc_remaining_vol(volume: Ml, nominal: Ml) -> Ml {
    if nominal.0 <= 0.0 {
        return Ml(0.0);
    }
    let rem = volume.0 % nominal.0;
    if rem < PLANNER_RESIDUAL_THRESHOLD {
        nominal
    } else {
        Ml(rem)
    }
}

// ── Dose Volume ────────────────────────────────────────────────

/// Plan a dose operation.
///
/// Validation pipeline:
/// 1. Zero volume or speed → reject
/// 2. Volume out of range → reject
/// 3. Speed out of range → reject
/// 4. Burette busy → reject
/// 5. Compute cycles, decide FillFirst vs Direct
#[allow(clippy::cast_possible_truncation, clippy::cast_sign_loss)]
pub fn plan_dose_volume(
    vol: Ml,
    speed: MlMin,
    current_vol: Ml,
    nominal: Ml,
    is_busy: bool,
) -> DosePlan {
    // Zero check
    if vol.0 <= 0.0 || speed.0 <= 0.0 {
        return DosePlan {
            action: DoseAction::Reject,
            reject_reason: Some("invalid_params"),
            first_cycle_vol: Ml(0.0),
            total_cycles: 0,
            remaining_vol: Ml(0.0),
        };
    }

    // Volume range check
    if vol.0 < PLANNER_MIN_VOLUME_ML || vol.0 > PLANNER_MAX_VOLUME_ML {
        return DosePlan {
            action: DoseAction::Reject,
            reject_reason: Some("volume_out_of_range"),
            first_cycle_vol: Ml(0.0),
            total_cycles: 0,
            remaining_vol: Ml(0.0),
        };
    }

    // Speed range check
    if speed.0 < PLANNER_MIN_SPEED_ML_MIN || speed.0 > PLANNER_MAX_SPEED_ML_MIN {
        return DosePlan {
            action: DoseAction::Reject,
            reject_reason: Some("speed_out_of_range"),
            first_cycle_vol: Ml(0.0),
            total_cycles: 0,
            remaining_vol: Ml(0.0),
        };
    }

    // Busy check
    if is_busy {
        return DosePlan {
            action: DoseAction::Reject,
            reject_reason: Some("burette_busy"),
            first_cycle_vol: Ml(0.0),
            total_cycles: 0,
            remaining_vol: Ml(0.0),
        };
    }

    // Compute plan
    let total_cycles: u8 = if vol.0 > nominal.0 + PLANNER_EPSILON_FLOAT {
        calc_total_cycles(vol, nominal)
    } else {
        1
    };

    let remaining_vol = if vol.0 > nominal.0 + PLANNER_EPSILON_FLOAT {
        calc_remaining_vol(vol, nominal)
    } else {
        vol
    };

    let first_cycle_vol = if total_cycles == 1 { vol } else { nominal };

    // Legacy C++ uses strict `<` without epsilon: `if (current_vol_ml < plan.first_cycle_vol)`
    let action = if current_vol.0 < first_cycle_vol.0 {
        DoseAction::FillFirst
    } else {
        DoseAction::Direct
    };

    DosePlan {
        action,
        reject_reason: None,
        first_cycle_vol,
        total_cycles,
        remaining_vol,
    }
}

// ── Fill / Empty ───────────────────────────────────────────────

/// Plan a fill operation.
pub fn plan_fill(speed: MlMin, is_busy: bool) -> SimplePlan {
    if speed.0 <= 0.0 || speed.0 < PLANNER_MIN_SPEED_ML_MIN || speed.0 > PLANNER_MAX_SPEED_ML_MIN {
        return SimplePlan {
            action: SimpleAction::Reject,
            reject_reason: Some("invalid_params"),
        };
    }
    if is_busy {
        return SimplePlan {
            action: SimpleAction::Reject,
            reject_reason: Some("burette_busy"),
        };
    }
    SimplePlan {
        action: SimpleAction::Execute,
        reject_reason: None,
    }
}

/// Plan an empty operation.
pub fn plan_empty(speed: MlMin, is_busy: bool) -> SimplePlan {
    if speed.0 <= 0.0 || speed.0 < PLANNER_MIN_SPEED_ML_MIN || speed.0 > PLANNER_MAX_SPEED_ML_MIN {
        return SimplePlan {
            action: SimpleAction::Reject,
            reject_reason: Some("invalid_params"),
        };
    }
    if is_busy {
        return SimplePlan {
            action: SimpleAction::Reject,
            reject_reason: Some("burette_busy"),
        };
    }
    SimplePlan {
        action: SimpleAction::Execute,
        reject_reason: None,
    }
}

// ── Rinse ──────────────────────────────────────────────────────

/// Plan a rinse operation.
pub fn plan_rinse(cycles: u8, speed: MlMin, is_busy: bool) -> RinsePlan {
    if cycles == 0 || speed.0 <= 0.0 {
        return RinsePlan {
            action: SimpleAction::Reject,
            reject_reason: Some("invalid_params"),
            cycles: 0,
        };
    }
    if speed.0 < PLANNER_MIN_SPEED_ML_MIN || speed.0 > PLANNER_MAX_SPEED_ML_MIN {
        return RinsePlan {
            action: SimpleAction::Reject,
            reject_reason: Some("speed_out_of_range"),
            cycles: 0,
        };
    }
    if is_busy {
        return RinsePlan {
            action: SimpleAction::Reject,
            reject_reason: Some("burette_busy"),
            cycles: 0,
        };
    }
    RinsePlan {
        action: SimpleAction::Execute,
        reject_reason: None,
        cycles,
    }
}

// ── Cal run ────────────────────────────────────────────────────

/// Plan a calibration run.
///
/// `mode` must be `"dose"` or `"speed"`.
#[allow(clippy::cast_possible_truncation, clippy::cast_sign_loss)]
pub fn plan_cal_run(
    mode: &str,
    speed: MlMin,
    freq_hz: u16,
    max_freq: f32,
    speed_coeff: f32,
    is_busy: bool,
) -> CalRunPlan {
    if mode != "dose" && mode != "speed" {
        return CalRunPlan {
            action: CalAction::Reject,
            reject_reason: Some("invalid_params: mode must be 'dose' or 'speed'"),
            freq_hz: 0,
            speed_ml_min: MlMin(0.0),
        };
    }

    if is_busy {
        return CalRunPlan {
            action: CalAction::Reject,
            reject_reason: Some("burette_busy"),
            freq_hz: 0,
            speed_ml_min: MlMin(0.0),
        };
    }

    if mode == "dose" {
        let cal_freq = if freq_hz == 0 {
            // Default to half of max_freq
            (max_freq / 2.0).round() as u16
        } else {
            freq_hz
        };
        let cal_speed = MlMin(f32::from(cal_freq) * speed_coeff);
        // If the calculated speed is below min, use a default of 15 ml/min (matching legacy)
        let final_speed = if cal_speed.0 < PLANNER_MIN_SPEED_ML_MIN {
            MlMin(15.0)
        } else {
            cal_speed
        };

        CalRunPlan {
            action: CalAction::Dose,
            reject_reason: None,
            freq_hz: cal_freq,
            speed_ml_min: final_speed,
        }
    } else {
        // "speed" mode
        if freq_hz == 0 {
            return CalRunPlan {
                action: CalAction::Reject,
                reject_reason: Some("invalid_params: freq_hz required for speed mode"),
                freq_hz: 0,
                speed_ml_min: MlMin(0.0),
            };
        }

        let fill_speed = if speed.0 < PLANNER_MIN_SPEED_ML_MIN {
            MlMin((max_freq / 2.0) * speed_coeff)
        } else {
            speed
        };

        CalRunPlan {
            action: CalAction::Speed,
            reject_reason: None,
            freq_hz,
            speed_ml_min: fill_speed,
        }
    }
}

// ── Cal speed seq ──────────────────────────────────────────────

/// Plan a calibration speed sequence.
///
/// `freq_count` must be between 3 and 8 (inclusive).
#[allow(clippy::cast_precision_loss)]
pub fn plan_cal_speed_seq(
    freq_count: u8,
    fill_speed: MlMin,
    max_freq: f32,
    speed_coeff: f32,
    is_busy: bool,
) -> CalSpeedSeqPlan {
    if !(3..=8).contains(&freq_count) {
        return CalSpeedSeqPlan {
            action: SimpleAction::Reject,
            reject_reason: Some("invalid_params: freq_count must be 3..8"),
            fill_speed_ml_min: MlMin(0.0),
        };
    }

    if is_busy {
        return CalSpeedSeqPlan {
            action: SimpleAction::Reject,
            reject_reason: Some("burette_busy"),
            fill_speed_ml_min: MlMin(0.0),
        };
    }

    let final_speed = if fill_speed.0 < PLANNER_MIN_SPEED_ML_MIN {
        MlMin((max_freq / 2.0) * speed_coeff)
    } else {
        fill_speed
    };

    CalSpeedSeqPlan {
        action: SimpleAction::Execute,
        reject_reason: None,
        fill_speed_ml_min: final_speed,
    }
}

// ── Tests ──────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    const NOMINAL: f32 = 8.14_f32;
    const SPEED: f32 = 10.0_f32;

    // ══════════════════════════════════════════════════════════════
    // plan_dose_volume — validation (7 cases)
    // ══════════════════════════════════════════════════════════════

    #[test]
    fn test_dose_reject_vol_zero() {
        let p = plan_dose_volume(Ml(0.0), MlMin(10.0), Ml(3.0), Ml(NOMINAL), false);
        assert_eq!(p.action, DoseAction::Reject);
    }

    #[test]
    fn test_dose_reject_speed_zero() {
        let p = plan_dose_volume(Ml(5.0), MlMin(0.0), Ml(3.0), Ml(NOMINAL), false);
        assert_eq!(p.action, DoseAction::Reject);
    }

    #[test]
    fn test_dose_reject_vol_too_small() {
        let p = plan_dose_volume(Ml(0.001), MlMin(10.0), Ml(3.0), Ml(NOMINAL), false);
        assert_eq!(p.action, DoseAction::Reject);
    }

    #[test]
    fn test_dose_reject_vol_too_large() {
        let p = plan_dose_volume(Ml(51.0), MlMin(10.0), Ml(3.0), Ml(NOMINAL), false);
        assert_eq!(p.action, DoseAction::Reject);
    }

    #[test]
    fn test_dose_reject_speed_too_small() {
        let p = plan_dose_volume(Ml(5.0), MlMin(0.05), Ml(3.0), Ml(NOMINAL), false);
        assert_eq!(p.action, DoseAction::Reject);
    }

    #[test]
    fn test_dose_reject_speed_too_large() {
        let p = plan_dose_volume(Ml(5.0), MlMin(25.0), Ml(3.0), Ml(NOMINAL), false);
        assert_eq!(p.action, DoseAction::Reject);
    }

    #[test]
    fn test_dose_reject_busy() {
        let p = plan_dose_volume(Ml(5.0), MlMin(10.0), Ml(3.0), Ml(NOMINAL), true);
        assert_eq!(p.action, DoseAction::Reject);
    }

    // ══════════════════════════════════════════════════════════════
    // plan_dose_volume — fill/direct decision (5 cases)
    // ══════════════════════════════════════════════════════════════

    #[test]
    fn test_dose_prefill_needed() {
        let p = plan_dose_volume(Ml(5.0), MlMin(SPEED), Ml(3.0), Ml(NOMINAL), false);
        assert_eq!(p.action, DoseAction::FillFirst);
        assert_eq!(p.total_cycles, 1);
        assert!((p.first_cycle_vol.0 - 5.0).abs() < 0.01);
    }

    #[test]
    fn test_dose_direct_enough() {
        let p = plan_dose_volume(Ml(5.0), MlMin(SPEED), Ml(7.0), Ml(NOMINAL), false);
        assert_eq!(p.action, DoseAction::Direct);
        assert_eq!(p.total_cycles, 1);
        assert!((p.first_cycle_vol.0 - 5.0).abs() < 0.01);
    }

    #[test]
    fn test_dose_exact_match() {
        let p = plan_dose_volume(Ml(NOMINAL), MlMin(SPEED), Ml(NOMINAL), Ml(NOMINAL), false);
        assert_eq!(p.action, DoseAction::Direct);
        assert_eq!(p.total_cycles, 1);
    }

    #[test]
    fn test_dose_empty_burette() {
        let p = plan_dose_volume(Ml(5.0), MlMin(SPEED), Ml(0.0), Ml(NOMINAL), false);
        assert_eq!(p.action, DoseAction::FillFirst);
    }

    #[test]
    fn test_dose_small_deficit() {
        let p = plan_dose_volume(Ml(5.0), MlMin(SPEED), Ml(4.999), Ml(NOMINAL), false);
        assert_eq!(p.action, DoseAction::FillFirst);
    }

    // ══════════════════════════════════════════════════════════════
    // plan_dose_volume — multi-cycle (5 cases)
    // ══════════════════════════════════════════════════════════════

    #[test]
    fn test_dose_oversize_2_cycles() {
        let p = plan_dose_volume(Ml(12.0), MlMin(SPEED), Ml(NOMINAL), Ml(NOMINAL), false);
        assert_eq!(p.action, DoseAction::Direct);
        assert_eq!(p.total_cycles, 2);
        assert!((p.first_cycle_vol.0 - NOMINAL).abs() < 0.01);
        assert!((p.remaining_vol.0 - 3.86).abs() < 0.01);
    }

    #[test]
    fn test_dose_oversize_3_cycles() {
        let p = plan_dose_volume(Ml(20.0), MlMin(SPEED), Ml(NOMINAL), Ml(NOMINAL), false);
        assert_eq!(p.action, DoseAction::Direct);
        assert_eq!(p.total_cycles, 3);
        assert!((p.first_cycle_vol.0 - NOMINAL).abs() < 0.01);
        assert!((p.remaining_vol.0 - 3.72).abs() < 0.01);
    }

    #[test]
    fn test_dose_oversize_exact_multiple() {
        let p = plan_dose_volume(Ml(16.28), MlMin(SPEED), Ml(NOMINAL), Ml(NOMINAL), false);
        assert_eq!(p.total_cycles, 2);
        assert!((p.remaining_vol.0 - NOMINAL).abs() < 0.01);
    }

    #[test]
    fn test_dose_direct_if_just_full() {
        let p = plan_dose_volume(Ml(NOMINAL), MlMin(SPEED), Ml(NOMINAL), Ml(NOMINAL), false);
        assert_eq!(p.action, DoseAction::Direct);
        assert_eq!(p.total_cycles, 1);
    }

    #[test]
    fn test_dose_multi_cycle_prefill_needed() {
        let p = plan_dose_volume(Ml(12.0), MlMin(SPEED), Ml(4.0), Ml(NOMINAL), false);
        assert_eq!(p.action, DoseAction::FillFirst);
        assert_eq!(p.total_cycles, 2);
        assert!((p.first_cycle_vol.0 - NOMINAL).abs() < 0.01);
        assert!((p.remaining_vol.0 - 3.86).abs() < 0.01);
    }

    // ══════════════════════════════════════════════════════════════
    // plan_fill / plan_empty (8 cases)
    // ══════════════════════════════════════════════════════════════

    #[test]
    fn test_fill_ok() {
        let p = plan_fill(MlMin(10.0), false);
        assert_eq!(p.action, SimpleAction::Execute);
    }

    #[test]
    fn test_fill_speed_zero() {
        let p = plan_fill(MlMin(0.0), false);
        assert_eq!(p.action, SimpleAction::Reject);
    }

    #[test]
    fn test_fill_speed_too_low() {
        let p = plan_fill(MlMin(0.05), false);
        assert_eq!(p.action, SimpleAction::Reject);
    }

    #[test]
    fn test_fill_speed_too_high() {
        let p = plan_fill(MlMin(25.0), false);
        assert_eq!(p.action, SimpleAction::Reject);
    }

    #[test]
    fn test_fill_busy() {
        let p = plan_fill(MlMin(10.0), true);
        assert_eq!(p.action, SimpleAction::Reject);
    }

    #[test]
    fn test_empty_ok() {
        let p = plan_empty(MlMin(10.0), false);
        assert_eq!(p.action, SimpleAction::Execute);
    }

    #[test]
    fn test_empty_speed_zero() {
        let p = plan_empty(MlMin(0.0), false);
        assert_eq!(p.action, SimpleAction::Reject);
    }

    #[test]
    fn test_empty_busy() {
        let p = plan_empty(MlMin(10.0), true);
        assert_eq!(p.action, SimpleAction::Reject);
    }

    // ══════════════════════════════════════════════════════════════
    // plan_rinse (4 cases)
    // ══════════════════════════════════════════════════════════════

    #[test]
    fn test_rinse_ok() {
        let p = plan_rinse(3, MlMin(10.0), false);
        assert_eq!(p.action, SimpleAction::Execute);
        assert_eq!(p.cycles, 3);
    }

    #[test]
    fn test_rinse_cycles_zero() {
        let p = plan_rinse(0, MlMin(10.0), false);
        assert_eq!(p.action, SimpleAction::Reject);
    }

    #[test]
    fn test_rinse_speed_zero() {
        let p = plan_rinse(3, MlMin(0.0), false);
        assert_eq!(p.action, SimpleAction::Reject);
    }

    #[test]
    fn test_rinse_busy() {
        let p = plan_rinse(3, MlMin(10.0), true);
        assert_eq!(p.action, SimpleAction::Reject);
    }

    // ══════════════════════════════════════════════════════════════
    // plan_cal_run (7 cases)
    // ══════════════════════════════════════════════════════════════

    #[test]
    fn test_cal_run_dose_default_freq() {
        let p = plan_cal_run("dose", MlMin(0.0), 0, 3000.0, 0.03052, false);
        assert_eq!(p.action, CalAction::Dose);
        assert_eq!(p.freq_hz, 1500);
    }

    #[test]
    fn test_cal_run_dose_with_freq() {
        let p = plan_cal_run("dose", MlMin(0.0), 2000, 3000.0, 0.03052, false);
        assert_eq!(p.action, CalAction::Dose);
        assert_eq!(p.freq_hz, 2000);
        assert!((p.speed_ml_min.0 - 2000.0 * 0.03052).abs() < 0.01);
    }

    #[test]
    fn test_cal_run_speed_no_freq() {
        let p = plan_cal_run("speed", MlMin(0.0), 0, 3000.0, 0.03052, false);
        assert_eq!(p.action, CalAction::Reject);
    }

    #[test]
    fn test_cal_run_speed_ok() {
        let p = plan_cal_run("speed", MlMin(0.0), 2000, 3000.0, 0.03052, false);
        assert_eq!(p.action, CalAction::Speed);
        assert_eq!(p.freq_hz, 2000);
        assert!((p.speed_ml_min.0 - 1500.0 * 0.03052).abs() < 0.01);
    }

    #[test]
    fn test_cal_run_speed_with_fill_speed() {
        let p = plan_cal_run("speed", MlMin(25.0), 2000, 3000.0, 0.03052, false);
        assert_eq!(p.action, CalAction::Speed);
        assert!((p.speed_ml_min.0 - 25.0).abs() < 0.01);
    }

    #[test]
    fn test_cal_run_invalid_mode() {
        let p = plan_cal_run("invalid", MlMin(0.0), 0, 3000.0, 0.03052, false);
        assert_eq!(p.action, CalAction::Reject);
    }

    #[test]
    fn test_cal_run_busy() {
        let p = plan_cal_run("dose", MlMin(0.0), 0, 3000.0, 0.03052, true);
        assert_eq!(p.action, CalAction::Reject);
    }

    // ══════════════════════════════════════════════════════════════
    // plan_cal_speed_seq (5 cases)
    // ══════════════════════════════════════════════════════════════

    #[test]
    fn test_cal_speed_seq_ok() {
        let p = plan_cal_speed_seq(3, MlMin(0.0), 3000.0, 0.03052, false);
        assert_eq!(p.action, SimpleAction::Execute);
        assert!((p.fill_speed_ml_min.0 - 1500.0 * 0.03052).abs() < 0.01);
    }

    #[test]
    fn test_cal_speed_seq_wrong_count() {
        let p = plan_cal_speed_seq(0, MlMin(0.0), 3000.0, 0.03052, false);
        assert_eq!(p.action, SimpleAction::Reject);
    }

    #[test]
    fn test_cal_speed_seq_count_2() {
        let p = plan_cal_speed_seq(2, MlMin(0.0), 3000.0, 0.03052, false);
        assert_eq!(p.action, SimpleAction::Reject);
    }

    #[test]
    fn test_cal_speed_seq_busy() {
        let p = plan_cal_speed_seq(3, MlMin(0.0), 3000.0, 0.03052, true);
        assert_eq!(p.action, SimpleAction::Reject);
    }

    #[test]
    fn test_cal_speed_seq_with_fill_speed() {
        let p = plan_cal_speed_seq(3, MlMin(20.0), 3000.0, 0.03052, false);
        assert_eq!(p.action, SimpleAction::Execute);
        assert!((p.fill_speed_ml_min.0 - 20.0).abs() < 0.01);
    }

    // ══════════════════════════════════════════════════════════════
    // calc_total_cycles / calc_remaining_vol (6 cases)
    // ══════════════════════════════════════════════════════════════

    #[test]
    fn test_calc_total_cycles_single() {
        assert_eq!(calc_total_cycles(Ml(5.0), Ml(NOMINAL)), 1);
    }

    #[test]
    fn test_calc_total_cycles_double() {
        assert_eq!(calc_total_cycles(Ml(12.0), Ml(NOMINAL)), 2);
    }

    #[test]
    fn test_calc_total_cycles_triple() {
        assert_eq!(calc_total_cycles(Ml(20.0), Ml(NOMINAL)), 3);
    }

    #[test]
    fn test_calc_remaining_vol_normal() {
        let r = calc_remaining_vol(Ml(12.0), Ml(NOMINAL));
        assert!((r.0 - 3.86).abs() < 0.01);
    }

    #[test]
    fn test_calc_remaining_vol_exact() {
        let r = calc_remaining_vol(Ml(16.28), Ml(NOMINAL));
        assert!((r.0 - NOMINAL).abs() < 0.01);
    }

    #[test]
    fn test_calc_remaining_vol_small_residual() {
        let r = calc_remaining_vol(Ml(16.285), Ml(NOMINAL));
        assert!((r.0 - NOMINAL).abs() < 0.01);
    }
}
