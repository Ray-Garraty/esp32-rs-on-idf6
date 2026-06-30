//! Handlers for system commands (3 commands).
//!
//! Commands: system.getStatus, system.getFormattedLogs, system.readLog.

use crate::application::command::{
    Command, CommandHandler, CommandResponse, CompactJson, HandlerContext,
};
use crate::errors::AppError;
use core::fmt::Write as CoreWrite;

/// Handler for system commands.
pub struct SystemHandler;

// Handler trait requires Result<_, AppError>. All Phase 3 return Ok(stub);
// Phase 5 will wire real hardware that can return Err.
#[allow(clippy::unnecessary_wraps)]
impl CommandHandler for SystemHandler {
    fn handle(
        &self,
        ctx: &HandlerContext<'_>,
        cmd: &Command,
        id: u64,
    ) -> Result<CommandResponse, AppError> {
        match cmd {
            Command::SystemGetStatus => Ok(handle_get_status(ctx, id)),
            Command::SystemGetFormattedLogs => Ok(handle_get_formatted_logs(id)),
            Command::SystemReadLog => Ok(handle_read_log(id)),
            _ => Err(AppError::Protocol(
                crate::errors::ProtocolError::UnknownCommand,
            )),
        }
    }
}

fn handle_get_status(ctx: &HandlerContext<'_>, id: u64) -> CommandResponse {
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(
        data,
        r#"{{"burette":{{"sts":"idle","vol":0.0,"spd":0.0}},"valve":"input","temp":null,"mv":0,"cal_steps_per_ml":{:.1}}}"#,
        ctx.cal_config.steps_per_ml,
    );
    CommandResponse::Single {
        id,
        status: "ok",
        data,
    }
}

fn handle_get_formatted_logs(id: u64) -> CommandResponse {
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(data, r#"{{"logs":[]}}"#);
    CommandResponse::Single {
        id,
        status: "ok",
        data,
    }
}

fn handle_read_log(id: u64) -> CommandResponse {
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(data, r#"{{"entries":[]}}"#);
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
    fn test_get_status() {
        let ctx = test_ctx();
        let cmd = Command::SystemGetStatus;
        let result = SystemHandler.handle(&ctx, &cmd, 1).unwrap();
        assert!(result.serialize().contains(r#""status":"ok""#));
    }

    #[test]
    fn test_get_formatted_logs() {
        let ctx = test_ctx();
        let cmd = Command::SystemGetFormattedLogs;
        let result = SystemHandler.handle(&ctx, &cmd, 1).unwrap();
        assert!(result.serialize().contains(r#""status":"ok""#));
    }

    #[test]
    fn test_read_log() {
        let ctx = test_ctx();
        let cmd = Command::SystemReadLog;
        let result = SystemHandler.handle(&ctx, &cmd, 1).unwrap();
        assert!(result.serialize().contains(r#""status":"ok""#));
    }
}
