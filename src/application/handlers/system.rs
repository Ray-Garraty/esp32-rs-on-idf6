//! Handlers for system commands (3 commands).
//!
//! Commands: system.getStatus, system.getFormattedLogs, system.readLog.
//!
//! Reads real device state from motor_state globals, valve globals,
//! and temperature sensor (xtensa-gated).

#![forbid(unsafe_code)]
use crate::application::command::{
    Command, CommandHandler, CommandResponse, CompactJson, HandlerContext,
};
use crate::domain::motor_state;
use crate::errors::AppError;
use core::fmt::Write as CoreWrite;

// Real temperature from onewire on xtensa; stub on host.
#[cfg(target_arch = "xtensa")]
use crate::infrastructure::drivers::onewire;

// Real valve position (host stub provided in valve handler module)
#[cfg(target_arch = "xtensa")]
use crate::infrastructure::drivers::valve as valve_drv;

#[cfg(not(target_arch = "xtensa"))]
mod valve_drv {
    use crate::domain::types::ValvePosition;
    use core::sync::atomic::{AtomicU8, Ordering};

    static GLOBAL_VALVE_POSITION: AtomicU8 = AtomicU8::new(0);

    pub fn get_global_valve_position() -> ValvePosition {
        let disc = GLOBAL_VALVE_POSITION.load(Ordering::Acquire);
        match disc {
            0 => ValvePosition::Input,
            _ => ValvePosition::Output,
        }
    }
}

/// Handler for system commands.
pub struct SystemHandler;

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

/// Handle `system.getStatus` — build a JSON snapshot of device state.
///
/// # Lint justification
///
/// `cast_precision_loss`: `current_pos.unsigned_abs() as f32 / 100.0` — the
/// position is in steps (i32, max ~2.1B). For f32 mantissa (24 bit, ~16.7 M),
/// positions above 16.7 million steps lose precision. At 200 steps/mL this
/// corresponds to ~83 kL, physically impossible for a burette.
#[allow(clippy::cast_precision_loss)]
fn handle_get_status(ctx: &HandlerContext<'_>, id: u64) -> CommandResponse {
    let sts = motor_state::get_broadcast_sts();
    let vol = motor_state::get_current_volume_ml();
    let current_pos = motor_state::CURRENT_POSITION.load(std::sync::atomic::Ordering::Acquire);
    let speed_val = current_pos.unsigned_abs() as f32 / 100.0;
    let valve_str = valve_drv::get_global_valve_position().as_str();

    let mut data: CompactJson = CompactJson::new();
    let _ = write!(
        data,
        r#"{{"brt":{{"sts":"{sts}","vol":{vol:.2},"spd":{speed_val:.1}}},"vlv":"{valve_str}","temp":{temp},"mv":0,"cal_steps_per_ml":{:.1}}}"#,
        ctx.cal_config.steps_per_ml,
        temp = display_temp(),
    );
    CommandResponse::Single {
        id,
        status: "ok",
        data,
    }
}

/// Get temperature string: "23.5" or "null".
fn display_temp() -> heapless::String<8> {
    let mut s: heapless::String<8> = heapless::String::new();
    #[cfg(target_arch = "xtensa")]
    if let Some(t) = onewire::temp_celsius() {
        let _ = core::fmt::Write::write_fmt(&mut s, format_args!("{t:.1}"));
        return s;
    }
    let _ = s.push_str("null");
    s
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
