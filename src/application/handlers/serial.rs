//! Handler for the serial ping command (1 command).
//!
//! Command: serial.ping

#![forbid(unsafe_code)]
use crate::application::command::{
    Command, CommandHandler, CommandResponse, CompactJson, HandlerContext,
};
use crate::errors::AppError;
use core::fmt::Write as CoreWrite;

/// Handler for serial.ping.
pub struct SerialPingHandler;

// Handler trait requires Result<_, AppError>. All Phase 3 return Ok(stub);
// Phase 5 will wire real hardware that can return Err.
#[allow(clippy::unnecessary_wraps)]
impl CommandHandler for SerialPingHandler {
    fn handle(
        &self,
        _ctx: &HandlerContext<'_>,
        cmd: &Command,
        id: u64,
    ) -> Result<CommandResponse, AppError> {
        match cmd {
            Command::SerialPing => Ok(handle_ping(id)),
            _ => Err(AppError::Protocol(
                crate::errors::ProtocolError::UnknownCommand,
            )),
        }
    }
}

fn handle_ping(id: u64) -> CommandResponse {
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(data, r#"{{"pong":true}}"#);
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
    fn test_serial_ping() {
        let ctx = test_ctx();
        let cmd = Command::SerialPing;
        let result = SerialPingHandler.handle(&ctx, &cmd, 1).unwrap();
        assert!(result.serialize().contains(r#""status":"ok""#));
        assert!(result.serialize().contains(r#""pong":true"#));
    }
}
