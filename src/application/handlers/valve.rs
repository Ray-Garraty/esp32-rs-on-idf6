//! Handlers for valve commands (2 commands).
//!
//! Commands: valve.setPosition, valve.getState.

use crate::application::command::{
    Command, CommandHandler, CommandResponse, CompactJson, HandlerContext,
};
use crate::domain::types::ValvePosition;
use crate::errors::AppError;
use core::fmt::Write as CoreWrite;

/// Handler for valve commands.
pub struct ValveHandler;

// Handler trait requires Result<_, AppError>. All Phase 3 return Ok(stub);
// Phase 5 will wire real hardware that can return Err.
#[allow(clippy::unnecessary_wraps)]
impl CommandHandler for ValveHandler {
    fn handle(
        &self,
        _ctx: &HandlerContext<'_>,
        cmd: &Command,
        id: u64,
    ) -> Result<CommandResponse, AppError> {
        match cmd {
            Command::ValveSetPosition { position } => Ok(handle_set_position(*position, id)),
            Command::ValveGetState => Ok(handle_get_state(id)),
            _ => Err(AppError::Protocol(
                crate::errors::ProtocolError::UnknownCommand,
            )),
        }
    }
}

fn handle_set_position(position: Option<ValvePosition>, id: u64) -> CommandResponse {
    let Some(pos) = position else {
        return CommandResponse::Error {
            id,
            message: "missing position",
        };
    };
    let pos_str = pos.as_str();
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(data, r#"{{"position":"{pos_str}"}}"#);
    CommandResponse::Single {
        id,
        status: "ok",
        data,
    }
}

fn handle_get_state(id: u64) -> CommandResponse {
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(data, r#"{{"position":"input"}}"#);
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
    fn test_set_position_input() {
        let ctx = test_ctx();
        let cmd = Command::ValveSetPosition {
            position: Some(ValvePosition::Input),
        };
        let result = ValveHandler.handle(&ctx, &cmd, 1).unwrap();
        assert!(result.serialize().contains(r#""status":"ok""#));
    }

    #[test]
    fn test_set_position_output() {
        let ctx = test_ctx();
        let cmd = Command::ValveSetPosition {
            position: Some(ValvePosition::Output),
        };
        let result = ValveHandler.handle(&ctx, &cmd, 1).unwrap();
        assert!(result.serialize().contains(r#""status":"ok""#));
    }

    #[test]
    fn test_set_position_missing() {
        let ctx = test_ctx();
        let cmd = Command::ValveSetPosition { position: None };
        let result = ValveHandler.handle(&ctx, &cmd, 1).unwrap();
        assert!(result.serialize().contains(r#""status":"error""#));
    }

    #[test]
    fn test_get_state() {
        let ctx = test_ctx();
        let cmd = Command::ValveGetState;
        let result = ValveHandler.handle(&ctx, &cmd, 1).unwrap();
        assert!(result.serialize().contains(r#""status":"ok""#));
    }
}
