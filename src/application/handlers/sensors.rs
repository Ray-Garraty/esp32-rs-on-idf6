//! Handlers for sensor and ADC calibration commands (8 commands).
//!
//! Commands: temperature.read, stallGuard.getThreshold, stallGuard.setThreshold,
//! adc.cal.get, adc.cal.measure, adc.cal.compute, adc.cal.save, adc.cal.reset.

#![forbid(unsafe_code)]
use crate::application::command::{
    Command, CommandHandler, CommandResponse, CompactJson, HandlerContext,
};
use crate::domain::adc_cal;
use crate::errors::AppError;
use core::fmt::Write as CoreWrite;

/// Handler for sensor and ADC calibration commands.
pub struct SensorsHandler;

// Handler trait requires Result<_, AppError>. All Phase 3 return Ok(stub);
// Phase 5 will wire real hardware that can return Err.
#[allow(clippy::unnecessary_wraps)]
impl CommandHandler for SensorsHandler {
    // Inner fn also returns Ok-only; kept wrapped for trait signature consistency.
    #[allow(clippy::unnecessary_wraps)]
    fn handle(
        &self,
        _ctx: &HandlerContext<'_>,
        cmd: &Command,
        id: u64,
    ) -> Result<CommandResponse, AppError> {
        match cmd {
            Command::TemperatureRead => Ok(handle_temp_read(id)),
            Command::StallGuardGetThreshold => Ok(handle_sg_get_threshold(id)),
            Command::StallGuardSetThreshold { value } => Ok(handle_sg_set_threshold(*value, id)),
            Command::AdcCalGet => Ok(handle_adc_cal_get(id)),
            Command::AdcCalMeasure { .. } => Ok(handle_adc_cal_measure(id)),
            Command::AdcCalCompute { raw_mv } => Ok(handle_adc_cal_compute(*raw_mv, id)),
            Command::AdcCalSave { a, b } => Ok(handle_adc_cal_save(*a, *b, id)),
            Command::AdcCalReset => Ok(handle_adc_cal_reset(id)),
            _ => Err(AppError::Protocol(
                crate::errors::ProtocolError::UnknownCommand,
            )),
        }
    }
}

fn handle_temp_read(id: u64) -> CommandResponse {
    let mut data: CompactJson = CompactJson::new();
    #[cfg(target_arch = "xtensa")]
    if let Some(t) = crate::infrastructure::drivers::onewire::temp_celsius() {
        let _ = write!(data, r#"{{"celsius":{t:.1}}}"#);
    } else {
        let _ = write!(data, r#"{{"celsius":null}}"#);
    }
    #[cfg(not(target_arch = "xtensa"))]
    let _ = write!(data, r#"{{"celsius":null}}"#);
    CommandResponse::Single {
        id,
        status: "ok",
        data,
    }
}

fn handle_sg_get_threshold(id: u64) -> CommandResponse {
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(data, r#"{{"threshold":0}}"#);
    CommandResponse::Single {
        id,
        status: "ok",
        data,
    }
}

fn handle_sg_set_threshold(value: Option<u8>, id: u64) -> CommandResponse {
    let Some(v) = value else {
        return CommandResponse::Error {
            id,
            message: "missing value",
        };
    };
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(data, r#"{{"threshold":{v}}}"#);
    CommandResponse::Single {
        id,
        status: "ok",
        data,
    }
}

fn handle_adc_cal_get(id: u64) -> CommandResponse {
    let (a, b_val) = adc_cal::get_calibration();
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(data, r#"{{"a":{a:.3},"b":{b_val:.1}}}"#);
    CommandResponse::Single {
        id,
        status: "ok",
        data,
    }
}

fn handle_adc_cal_measure(id: u64) -> CommandResponse {
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(data, r#"{{"raw_mv":0,"calibrated_mv":0}}"#);
    CommandResponse::Single {
        id,
        status: "ok",
        data,
    }
}

fn handle_adc_cal_compute(raw_mv: Option<i16>, id: u64) -> CommandResponse {
    let Some(raw) = raw_mv else {
        let mut data: CompactJson = CompactJson::new();
        let _ = write!(data, r#"{{"raw_mv":0,"calibrated_mv":0}}"#);
        return CommandResponse::Single {
            id,
            status: "ok",
            data,
        };
    };
    // i16 → u16 cast: intentional — ADC raw values are always non-negative.
    #[allow(clippy::cast_sign_loss)]
    let calibrated = adc_cal::calibrated_from_raw(raw as u16);
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(data, r#"{{"raw_mv":{raw},"calibrated_mv":{calibrated}}}"#);
    CommandResponse::Single {
        id,
        status: "ok",
        data,
    }
}

fn handle_adc_cal_save(a: Option<f32>, b_val: Option<f32>, id: u64) -> CommandResponse {
    let (current_a, current_b) = adc_cal::get_calibration();
    let new_a = a.unwrap_or(current_a);
    let new_b = b_val.unwrap_or(current_b);
    adc_cal::set_calibration(new_a, new_b);
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(data, r#"{{"a":{new_a:.3},"b":{new_b:.1},"saved":true}}"#);
    CommandResponse::Single {
        id,
        status: "ok",
        data,
    }
}

fn handle_adc_cal_reset(id: u64) -> CommandResponse {
    adc_cal::reset_calibration();
    let mut data: CompactJson = CompactJson::new();
    let _ = write!(data, r#"{{"reset":true,"a":1.0,"b":0.0}}"#);
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
    fn test_temp_read() {
        let ctx = test_ctx();
        let cmd = Command::TemperatureRead;
        let result = SensorsHandler.handle(&ctx, &cmd, 1).unwrap();
        assert!(result.serialize().contains(r#""status":"ok""#));
    }

    #[test]
    fn test_adc_cal_get() {
        let ctx = test_ctx();
        let cmd = Command::AdcCalGet;
        let result = SensorsHandler.handle(&ctx, &cmd, 1).unwrap();
        assert!(result.serialize().contains(r#""status":"ok""#));
    }

    #[test]
    fn test_adc_cal_save() {
        let ctx = test_ctx();
        adc_cal::reset_calibration();
        let cmd = Command::AdcCalSave {
            a: Some(1.5),
            b: Some(2.0),
        };
        let result = SensorsHandler.handle(&ctx, &cmd, 1).unwrap();
        assert!(result.serialize().contains(r#""status":"ok""#));
        let (a, b_val) = adc_cal::get_calibration();
        assert!((a - 1.5).abs() < 0.001);
        assert!((b_val - 2.0).abs() < 0.001);
        adc_cal::reset_calibration();
    }

    #[test]
    fn test_adc_cal_reset() {
        let ctx = test_ctx();
        adc_cal::set_calibration(2.0, 3.0);
        let cmd = Command::AdcCalReset;
        let _ = SensorsHandler.handle(&ctx, &cmd, 1).unwrap();
        let (a, b_val) = adc_cal::get_calibration();
        assert!((a - 1.0).abs() < 0.001);
        assert!((b_val - 0.0).abs() < 0.001);
    }

    #[test]
    fn test_stall_guard_set_threshold_missing() {
        let ctx = test_ctx();
        let cmd = Command::StallGuardSetThreshold { value: None };
        let result = SensorsHandler.handle(&ctx, &cmd, 1).unwrap();
        assert!(result.serialize().contains(r#""status":"error""#));
    }

    #[test]
    fn test_stall_guard_set_threshold_ok() {
        let ctx = test_ctx();
        let cmd = Command::StallGuardSetThreshold { value: Some(128) };
        let result = SensorsHandler.handle(&ctx, &cmd, 1).unwrap();
        assert!(result.serialize().contains(r#""status":"ok""#));
    }
}
