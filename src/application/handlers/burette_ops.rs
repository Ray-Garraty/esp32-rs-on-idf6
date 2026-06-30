//! Handlers for burette operation commands (10 commands).
//!
//! Commands: fill, empty, doseVolume, rinse, stop, emergencyStop,
//! getStatus, moveSteps, moveToStop, setDirection.

use crate::application::command::{
    Command, CommandHandler, CommandResponse, CompactJson, HandlerContext,
};
use crate::domain::planner;
use crate::domain::types::{Direction, Ml, MlMin};
use crate::errors::AppError;
use core::fmt::Write as CoreWrite;

/// Handler for burette operation commands.
pub struct BuretteOpsHandler;

// Handler trait requires Result<_, AppError>. All Phase 3 return Ok(stub);
// Phase 5 will wire real hardware that can return Err.
#[allow(clippy::unnecessary_wraps)]
impl CommandHandler for BuretteOpsHandler {
    fn handle(
        &self,
        ctx: &HandlerContext<'_>,
        cmd: &Command,
        id: u64,
    ) -> Result<CommandResponse, AppError> {
        match cmd {
            Command::BuretteFill { speed_ml_min } => Ok(handle_fill(*speed_ml_min, id)),
            Command::BuretteEmpty { speed_ml_min } => Ok(handle_empty(*speed_ml_min, id)),
            Command::BuretteDoseVolume { ml, speed_ml_min } => {
                Ok(handle_dose_volume(ctx, *ml, *speed_ml_min, id))
            }
            Command::BuretteRinse {
                cycles,
                speed_ml_min,
            } => Ok(handle_rinse(*cycles, *speed_ml_min, id)),
            Command::BuretteStop => Ok(handle_stop(id)),
            Command::BuretteEmergencyStop => Ok(handle_emergency_stop(id)),
            Command::BuretteGetStatus => Ok(handle_get_status(id)),
            Command::BuretteMoveSteps { .. } => Ok(handle_move_steps(id)),
            Command::BuretteMoveToStop { .. } => Ok(handle_move_to_stop(id)),
            Command::BuretteSetDirection { direction } => Ok(handle_set_direction(*direction, id)),
            _ => Err(AppError::Protocol(
                crate::errors::ProtocolError::UnknownCommand,
            )),
        }
    }
}

fn handle_fill(speed_ml_min: f32, id: u64) -> CommandResponse {
    let plan = planner::plan_fill(MlMin(speed_ml_min), false);
    if plan.action == planner::SimpleAction::Reject {
        let reason = plan.reject_reason.unwrap_or("invalid_params");
        return CommandResponse::Error {
            id,
            message: reason,
        };
    }
    let mut ack: CompactJson = CompactJson::new();
    let _ = write!(ack, r#"{{"action":"fill"}}"#);
    CommandResponse::AckThen { id, ack }
}

fn handle_empty(speed_ml_min: f32, id: u64) -> CommandResponse {
    let plan = planner::plan_empty(MlMin(speed_ml_min), false);
    if plan.action == planner::SimpleAction::Reject {
        let reason = plan.reject_reason.unwrap_or("invalid_params");
        return CommandResponse::Error {
            id,
            message: reason,
        };
    }
    let mut ack: CompactJson = CompactJson::new();
    let _ = write!(ack, r#"{{"action":"empty"}}"#);
    CommandResponse::AckThen { id, ack }
}

fn handle_dose_volume(
    ctx: &HandlerContext<'_>,
    ml: f32,
    speed_ml_min: f32,
    id: u64,
) -> CommandResponse {
    let plan = planner::plan_dose_volume(
        Ml(ml),
        MlMin(speed_ml_min),
        Ml(0.0),
        Ml(ctx.cal_config.nominal_vol),
        false,
    );
    if plan.action == planner::DoseAction::Reject {
        let reason = plan.reject_reason.unwrap_or("invalid_params");
        return CommandResponse::Error {
            id,
            message: reason,
        };
    }
    let action_label = if plan.action == planner::DoseAction::FillFirst {
        "fill_first"
    } else {
        "direct"
    };
    let mut ack: CompactJson = CompactJson::new();
    let _ = write!(
        ack,
        r#"{{"action":"{action_label}","cycles":{},"first_cycle_vol":{:.2}}}"#,
        plan.total_cycles, plan.first_cycle_vol.0,
    );
    CommandResponse::AckThen { id, ack }
}

fn handle_rinse(cycles: u8, speed_ml_min: f32, id: u64) -> CommandResponse {
    let plan = planner::plan_rinse(cycles, MlMin(speed_ml_min), false);
    if plan.action == planner::SimpleAction::Reject {
        let reason = plan.reject_reason.unwrap_or("invalid_params");
        return CommandResponse::Error {
            id,
            message: reason,
        };
    }
    let mut ack: CompactJson = CompactJson::new();
    let _ = write!(ack, r#"{{"action":"rinse","cycles":{}}}"#, plan.cycles);
    CommandResponse::AckThen { id, ack }
}

fn handle_stop(id: u64) -> CommandResponse {
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(data, r#"{{"action":"stopping"}}"#);
    CommandResponse::Single {
        id,
        status: "ok",
        data,
    }
}

fn handle_emergency_stop(id: u64) -> CommandResponse {
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(data, r#"{{"action":"emergency_stopped"}}"#);
    CommandResponse::Single {
        id,
        status: "ok",
        data,
    }
}

fn handle_get_status(id: u64) -> CommandResponse {
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(
        data,
        r#"{{"state":"idle","vol_ml":0.0,"operation":"none"}}"#
    );
    CommandResponse::Single {
        id,
        status: "ok",
        data,
    }
}

fn handle_move_steps(id: u64) -> CommandResponse {
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(data, r#"{{"action":"move_started"}}"#);
    CommandResponse::Single {
        id,
        status: "ok",
        data,
    }
}

fn handle_move_to_stop(id: u64) -> CommandResponse {
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(data, r#"{{"action":"moving_to_stop"}}"#);
    CommandResponse::Single {
        id,
        status: "ok",
        data,
    }
}

fn handle_set_direction(direction: Option<Direction>, id: u64) -> CommandResponse {
    let dir_str = match direction {
        Some(Direction::LiqIn) => "LiqIn",
        Some(Direction::LiqOut) => "LiqOut",
        None => {
            return CommandResponse::Error {
                id,
                message: "missing direction",
            }
        }
    };
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(data, r#"{{"direction":"{dir_str}"}}"#);
    CommandResponse::Single {
        id,
        status: "ok",
        data,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::domain::calibration::CalibrationConfig;
    use crate::domain::channels::SystemChannels;

    fn test_ctx() -> HandlerContext<'static> {
        let channels = Box::leak(Box::new(SystemChannels::new()));
        let cal_config = Box::leak(Box::new(CalibrationConfig::new()));
        HandlerContext {
            channels,
            cal_config,
        }
    }

    #[test]
    fn test_fill_ok() {
        let ctx = test_ctx();
        let cmd = Command::BuretteFill { speed_ml_min: 10.0 };
        let result = BuretteOpsHandler.handle(&ctx, &cmd, 1).unwrap();
        assert!(result.serialize().contains(r#""status":"ok""#));
    }

    #[test]
    fn test_fill_invalid_speed() {
        let ctx = test_ctx();
        let cmd = Command::BuretteFill { speed_ml_min: 0.0 };
        let result = BuretteOpsHandler.handle(&ctx, &cmd, 1).unwrap();
        assert!(result.serialize().contains(r#""status":"error""#));
    }

    #[test]
    fn test_dose_volume_ok() {
        let ctx = test_ctx();
        let cmd = Command::BuretteDoseVolume {
            ml: 5.0,
            speed_ml_min: 10.0,
        };
        let result = BuretteOpsHandler.handle(&ctx, &cmd, 1).unwrap();
        assert!(result.serialize().contains(r#""status":"ok""#));
    }

    #[test]
    fn test_stop() {
        let ctx = test_ctx();
        let cmd = Command::BuretteStop;
        let result = BuretteOpsHandler.handle(&ctx, &cmd, 1).unwrap();
        assert!(result.serialize().contains(r#""status":"ok""#));
    }

    #[test]
    fn test_emergency_stop() {
        let ctx = test_ctx();
        let cmd = Command::BuretteEmergencyStop;
        let result = BuretteOpsHandler.handle(&ctx, &cmd, 1).unwrap();
        assert!(result.serialize().contains(r#""status":"ok""#));
    }

    #[test]
    fn test_get_status() {
        let ctx = test_ctx();
        let cmd = Command::BuretteGetStatus;
        let result = BuretteOpsHandler.handle(&ctx, &cmd, 1).unwrap();
        assert!(result.serialize().contains(r#""status":"ok""#));
    }

    #[test]
    fn test_set_direction_missing() {
        let ctx = test_ctx();
        let cmd = Command::BuretteSetDirection { direction: None };
        let result = BuretteOpsHandler.handle(&ctx, &cmd, 1).unwrap();
        assert!(result.serialize().contains(r#""status":"error""#));
    }

    #[test]
    fn test_set_direction_liq_in() {
        let ctx = test_ctx();
        let cmd = Command::BuretteSetDirection {
            direction: Some(Direction::LiqIn),
        };
        let result = BuretteOpsHandler.handle(&ctx, &cmd, 1).unwrap();
        assert!(result.serialize().contains(r#""status":"ok""#));
    }
}
