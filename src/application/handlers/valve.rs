//! Handlers for valve commands (2 commands).
//!
//! Commands: valve.setPosition, valve.getState.
//!
//! Uses the global valve state from `infrastructure::drivers::valve` for
//! lock-free reads and GPIO writes (xtensa-only). On host, uses local
//! stubs for testing.

#![forbid(unsafe_code)]
use crate::application::command::{
    Command, CommandHandler, CommandResponse, CompactJson, HandlerContext,
};
use crate::domain::types::ValvePosition;
use crate::errors::AppError;
use core::fmt::Write as CoreWrite;

// Import real valve globals on xtensa; use local stubs on host.
#[cfg(target_arch = "xtensa")]
use crate::infrastructure::drivers::valve as valve_drv;

#[cfg(not(target_arch = "xtensa"))]
mod valve_drv {
    use crate::domain::types::ValvePosition;
    use core::sync::atomic::{AtomicU8, Ordering};

    static GLOBAL_VALVE_POSITION: AtomicU8 = AtomicU8::new(0);

    pub fn set_global_valve_position(pos: ValvePosition) {
        let disc: u8 = match pos {
            ValvePosition::Input => 0,
            ValvePosition::Output => 1,
        };
        GLOBAL_VALVE_POSITION.store(disc, Ordering::Release);
    }

    pub fn get_global_valve_position() -> ValvePosition {
        let disc = GLOBAL_VALVE_POSITION.load(Ordering::Acquire);
        match disc {
            0 => ValvePosition::Input,
            _ => ValvePosition::Output,
        }
    }
}

/// Handler for valve commands.
pub struct ValveHandler;

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
    // Update global valve state (atomic + GPIO via try_lock)
    valve_drv::set_global_valve_position(pos);
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
    let pos = valve_drv::get_global_valve_position();
    let pos_str = pos.as_str();
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(data, r#"{{"position":"{pos_str}"}}"#);
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
