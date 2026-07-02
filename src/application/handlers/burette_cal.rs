//! Handlers for burette calibration commands (8 commands).
//!
//! Commands: get, calcVolume, calcSpeed, save, reset, run, runSpeedSeq, getResult.

#![forbid(unsafe_code)]
use crate::application::command::{
    Command, CommandHandler, CommandResponse, CompactJson, HandlerContext,
};
use crate::domain::calibration;
use crate::domain::planner;
use crate::domain::types::{Ml, MlMin, Steps};
use crate::errors::AppError;
use core::fmt::Write as CoreWrite;

/// Handler for burette calibration commands.
pub struct BuretteCalHandler;

impl CommandHandler for BuretteCalHandler {
    fn handle(
        &self,
        ctx: &HandlerContext<'_>,
        cmd: &Command,
        id: u64,
    ) -> Result<CommandResponse, AppError> {
        match cmd {
            Command::BuretteCalGet => Ok(handle_cal_get(ctx, id)),
            Command::BuretteCalCalcVolume { steps } => Ok(handle_cal_calc_volume(*steps, id)),
            Command::BuretteCalCalcSpeed { steps_per_sec } => {
                Ok(handle_cal_calc_speed(*steps_per_sec, id))
            }
            Command::BuretteCalSave => Ok(handle_cal_save(ctx, id)),
            Command::BuretteCalReset => Ok(handle_cal_reset(id)),
            Command::BuretteCalRun {
                mode,
                speed_ml_min,
                freq_hz,
            } => Ok(handle_cal_run(*speed_ml_min, *freq_hz, mode.as_deref(), id)),
            Command::BuretteCalRunSpeedSeq {
                freq_count,
                speed_ml_min,
            } => Ok(handle_cal_run_speed_seq(*freq_count, *speed_ml_min, id)),
            Command::BuretteCalGetResult => Ok(handle_cal_get_result(id)),
            _ => Err(AppError::Protocol(
                crate::errors::ProtocolError::UnknownCommand,
            )),
        }
    }
}

fn handle_cal_get(ctx: &HandlerContext<'_>, id: u64) -> CommandResponse {
    let cfg = ctx.cal_config;
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(
        data,
        r#"{{"steps_per_ml":{:.1},"nominal_vol":{:.2},"speed_coeff":{:.5},"min_freq":{},"max_freq":{},"cal_date":{}}}"#,
        cfg.steps_per_ml,
        cfg.nominal_vol,
        cfg.speed_coeff,
        cfg.min_freq,
        cfg.max_freq,
        cfg.calibration_date,
    );
    CommandResponse::Single {
        id,
        status: "ok",
        data,
    }
}

fn handle_cal_calc_volume(steps: i32, id: u64) -> CommandResponse {
    let vol = calibration::steps_to_volume(
        Steps(steps),
        Steps(0),
        calibration::DEFAULT_STEPS_PER_ML,
        Ml(calibration::DEFAULT_NOMINAL_VOL),
    );
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(data, r#"{{"volume_ml":{:.3},"steps":{steps}}}"#, vol.0);
    CommandResponse::Single {
        id,
        status: "ok",
        data,
    }
}

fn handle_cal_calc_speed(steps_per_sec: u16, id: u64) -> CommandResponse {
    let speed = calibration::frequency_to_speed(steps_per_sec, calibration::DEFAULT_SPEED_COEFF);
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(
        data,
        r#"{{"speed_ml_min":{:.3},"freq_hz":{steps_per_sec}}}"#,
        speed.0
    );
    CommandResponse::Single {
        id,
        status: "ok",
        data,
    }
}

fn handle_cal_save(ctx: &HandlerContext<'_>, id: u64) -> CommandResponse {
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(
        data,
        r#"{{"saved":true,"steps_per_ml":{:.1}}}"#,
        ctx.cal_config.steps_per_ml
    );
    CommandResponse::Single {
        id,
        status: "ok",
        data,
    }
}

fn handle_cal_reset(id: u64) -> CommandResponse {
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(
        data,
        r#"{{"reset":true,"steps_per_ml":{}}}"#,
        calibration::DEFAULT_STEPS_PER_ML
    );
    CommandResponse::Single {
        id,
        status: "ok",
        data,
    }
}

fn handle_cal_run(speed_ml_min: f32, freq_hz: u16, mode: Option<&str>, id: u64) -> CommandResponse {
    let plan = planner::plan_cal_run(
        mode.unwrap_or("dose"),
        MlMin(speed_ml_min),
        freq_hz,
        f32::from(calibration::DEFAULT_MAX_FREQ),
        calibration::DEFAULT_SPEED_COEFF,
        false,
    );
    if plan.action == planner::CalAction::Reject {
        let reason = plan.reject_reason.unwrap_or("invalid_params");
        return CommandResponse::Error {
            id,
            message: reason,
        };
    }

    let action_label = if plan.action == planner::CalAction::Dose {
        "dose"
    } else {
        "speed"
    };
    let mut ack: CompactJson = CompactJson::new();
    let _ = write!(
        ack,
        r#"{{"action":"{action_label}","freq_hz":{},"speed_ml_min":{:.2}}}"#,
        plan.freq_hz, plan.speed_ml_min.0,
    );
    CommandResponse::AckThen { id, ack }
}

fn handle_cal_run_speed_seq(freq_count: u8, speed_ml_min: f32, id: u64) -> CommandResponse {
    let plan = planner::plan_cal_speed_seq(
        freq_count,
        MlMin(speed_ml_min),
        f32::from(calibration::DEFAULT_MAX_FREQ),
        calibration::DEFAULT_SPEED_COEFF,
        false,
    );
    if plan.action == planner::SimpleAction::Reject {
        let reason = plan.reject_reason.unwrap_or("invalid_params");
        return CommandResponse::Error {
            id,
            message: reason,
        };
    }

    let mut ack: CompactJson = CompactJson::new();
    let _ = write!(
        ack,
        r#"{{"action":"speed_seq","freq_count":{freq_count},"fill_speed_ml_min":{:.2}}}"#,
        plan.fill_speed_ml_min.0,
    );
    CommandResponse::AckThen { id, ack }
}

// Both branches build different CompactJson data payloads. The
// `if let` / `else` is more readable than map_or_else with closures
// that would each need to own the CompactJson construction.
#[allow(clippy::option_if_let_else)]
fn handle_cal_get_result(id: u64) -> CommandResponse {
    if let Some(cfg) = calibration::burette_cal_get_pending_copy() {
        let mut data: CompactJson = CompactJson::new();
        let _ = write!(
            data,
            r#"{{"has_pending":true,"steps_per_ml":{:.1},"nominal_vol":{:.2}}}"#,
            cfg.steps_per_ml, cfg.nominal_vol,
        );
        CommandResponse::Single {
            id,
            status: "ok",
            data,
        }
    } else {
        let mut data: CompactJson = CompactJson::new();
        let _ = write!(data, r#"{{"has_pending":false}}"#);
        CommandResponse::Single {
            id,
            status: "ok",
            data,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::domain::calibration::CalibrationConfig;
    use crate::domain::channels::SystemChannels;
    use heapless::String as HString;
    use std::sync::mpsc;

    fn test_ctx() -> HandlerContext<'static> {
        let channels = Box::leak(Box::new(SystemChannels::new()));
        let cal_config = Box::leak(Box::new(CalibrationConfig::new()));
        let (tx, _rx) = mpsc::sync_channel(1);
        let response_tx: &'static mpsc::SyncSender<(u64, CommandResponse)> =
            Box::leak(Box::new(tx));
        HandlerContext {
            channels,
            cal_config,
            response_tx,
        }
    }

    #[test]
    fn test_cal_get() {
        let ctx = test_ctx();
        let cmd = Command::BuretteCalGet;
        let result = BuretteCalHandler.handle(&ctx, &cmd, 1).unwrap();
        let json = result.serialize();
        assert!(json.contains(r#""status":"ok""#));
        assert!(json.contains("steps_per_ml"));
    }

    #[test]
    fn test_cal_calc_volume() {
        let ctx = test_ctx();
        let cmd = Command::BuretteCalCalcVolume { steps: 7730 };
        let result = BuretteCalHandler.handle(&ctx, &cmd, 1).unwrap();
        let json = result.serialize();
        assert!(json.contains(r#""status":"ok""#));
        assert!(json.contains("volume_ml"));
    }

    #[test]
    fn test_cal_calc_speed() {
        let ctx = test_ctx();
        let cmd = Command::BuretteCalCalcSpeed {
            steps_per_sec: 1000,
        };
        let result = BuretteCalHandler.handle(&ctx, &cmd, 1).unwrap();
        let json = result.serialize();
        assert!(json.contains(r#""status":"ok""#));
    }

    #[test]
    fn test_cal_save() {
        let ctx = test_ctx();
        let cmd = Command::BuretteCalSave;
        let result = BuretteCalHandler.handle(&ctx, &cmd, 1).unwrap();
        let json = result.serialize();
        assert!(json.contains(r#""status":"ok""#));
    }

    #[test]
    fn test_cal_reset() {
        let ctx = test_ctx();
        let cmd = Command::BuretteCalReset;
        let result = BuretteCalHandler.handle(&ctx, &cmd, 1).unwrap();
        let json = result.serialize();
        assert!(json.contains(r#""status":"ok""#));
    }

    #[test]
    fn test_cal_run_dose() {
        let ctx = test_ctx();
        let mut m = HString::<16>::new();
        let _ = m.push_str("dose");
        let cmd = Command::BuretteCalRun {
            mode: Some(m),
            speed_ml_min: 15.0,
            freq_hz: 1500,
        };
        let result = BuretteCalHandler.handle(&ctx, &cmd, 1).unwrap();
        let json = result.serialize();
        assert!(json.contains(r#""status":"ok""#));
    }

    #[test]
    fn test_cal_run_invalid_mode() {
        let ctx = test_ctx();
        let mut m = HString::<16>::new();
        let _ = m.push_str("invalid");
        let cmd = Command::BuretteCalRun {
            mode: Some(m),
            speed_ml_min: 0.0,
            freq_hz: 0,
        };
        let result = BuretteCalHandler.handle(&ctx, &cmd, 1).unwrap();
        let json = result.serialize();
        assert!(json.contains(r#""status":"error""#));
    }

    #[test]
    fn test_cal_get_result_no_pending() {
        let ctx = test_ctx();
        let cmd = Command::BuretteCalGetResult;
        let result = BuretteCalHandler.handle(&ctx, &cmd, 1).unwrap();
        let json = result.serialize();
        assert!(json.contains("has_pending"));
    }
}
