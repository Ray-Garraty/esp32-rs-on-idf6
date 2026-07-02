//! Command enum (32 wire-protocol variants), envelope, response types, and handler trait.
//!
//! All 32 commands match the exact wire protocol names from the legacy C++ codebase.
//! See `docs/refs/project.md` for the full command set.
//!
//! Serde derive macros internally use `std::vec::Vec` for deserialization buffers.
//! This is outside our control — the lint is suppressed at module level.
#![allow(clippy::disallowed_types)]
#![forbid(unsafe_code)]
use crate::domain::calibration::CalibrationConfig;
use crate::domain::channels::SystemChannels;
use crate::domain::memory::MAX_RESPONSE_SIZE;
use crate::errors::AppError;
use core::fmt::Write as CoreWrite;
use heapless::String as HString;
use serde::Deserialize;
use std::sync::mpsc;

// ── Compact JSON buffer ───────────────────────────────────────

/// Fixed-size JSON string for command responses (no heap allocation).
pub type CompactJson = HString<MAX_RESPONSE_SIZE>;

// ── Command enum (32 variants) ────────────────────────────────

/// All wire-protocol commands with their parameters.
///
/// Serde `tag = "cmd"` enables flat JSON like `{"cmd":"burette.fill","speed_ml_min":10.0}`.
#[derive(Debug, Clone, PartialEq, Deserialize)]
#[serde(tag = "cmd")]
pub enum Command {
    // ── Burette operations (10) ────────────────────────────────
    #[serde(rename = "burette.fill")]
    BuretteFill {
        #[serde(default)]
        speed_ml_min: f32,
    },
    #[serde(rename = "burette.empty")]
    BuretteEmpty {
        #[serde(default)]
        speed_ml_min: f32,
    },
    #[serde(rename = "burette.doseVolume")]
    BuretteDoseVolume {
        ml: f32,
        #[serde(default)]
        speed_ml_min: f32,
    },
    #[serde(rename = "burette.rinse")]
    BuretteRinse {
        #[serde(default)]
        cycles: u8,
        #[serde(default)]
        speed_ml_min: f32,
    },
    #[serde(rename = "burette.stop")]
    BuretteStop,
    #[serde(rename = "burette.emergencyStop")]
    BuretteEmergencyStop,
    #[serde(rename = "burette.getStatus")]
    BuretteGetStatus,
    #[serde(rename = "burette.moveSteps")]
    BuretteMoveSteps {
        #[serde(default)]
        steps: i32,
        #[serde(default)]
        speed_hz: u32,
    },
    #[serde(rename = "burette.moveToStop")]
    BuretteMoveToStop {
        #[serde(default)]
        speed_hz: u32,
    },
    #[serde(rename = "burette.setDirection")]
    BuretteSetDirection {
        direction: Option<crate::domain::types::Direction>,
    },

    // ── Burette calibration (8) ────────────────────────────────
    #[serde(rename = "burette.cal.get")]
    BuretteCalGet,
    #[serde(rename = "burette.cal.calcVolume")]
    BuretteCalCalcVolume {
        #[serde(default)]
        steps: i32,
    },
    #[serde(rename = "burette.cal.calcSpeed")]
    BuretteCalCalcSpeed {
        #[serde(default)]
        steps_per_sec: u16,
    },
    #[serde(rename = "burette.cal.save")]
    BuretteCalSave,
    #[serde(rename = "burette.cal.reset")]
    BuretteCalReset,
    #[serde(rename = "burette.cal.run")]
    BuretteCalRun {
        mode: Option<HString<16>>,
        #[serde(default)]
        speed_ml_min: f32,
        #[serde(default)]
        freq_hz: u16,
    },
    #[serde(rename = "burette.cal.runSpeedSeq")]
    BuretteCalRunSpeedSeq {
        #[serde(default)]
        freq_count: u8,
        #[serde(default)]
        speed_ml_min: f32,
    },
    #[serde(rename = "burette.cal.getResult")]
    BuretteCalGetResult,

    // ── Valve (2) ──────────────────────────────────────────────
    #[serde(rename = "valve.setPosition")]
    ValveSetPosition {
        position: Option<crate::domain::types::ValvePosition>,
    },
    #[serde(rename = "valve.getState")]
    ValveGetState,

    // ── Sensors (8) ────────────────────────────────────────────
    #[serde(rename = "temperature.read")]
    TemperatureRead,
    #[serde(rename = "stallGuard.getThreshold")]
    StallGuardGetThreshold,
    #[serde(rename = "stallGuard.setThreshold")]
    StallGuardSetThreshold { value: Option<u8> },
    #[serde(rename = "adc.cal.get")]
    AdcCalGet,
    #[serde(rename = "adc.cal.measure")]
    AdcCalMeasure { samples: Option<u32> },
    #[serde(rename = "adc.cal.compute")]
    AdcCalCompute { raw_mv: Option<i16> },
    #[serde(rename = "adc.cal.save")]
    AdcCalSave { a: Option<f32>, b: Option<f32> },
    #[serde(rename = "adc.cal.reset")]
    AdcCalReset,

    // ── System (3) ─────────────────────────────────────────────
    #[serde(rename = "system.getStatus")]
    SystemGetStatus,
    #[serde(rename = "system.getFormattedLogs")]
    SystemGetFormattedLogs,
    #[serde(rename = "system.readLog")]
    SystemReadLog,

    // ── Serial (1) ─────────────────────────────────────────────
    #[serde(rename = "serial.ping")]
    SerialPing,
}

// ── Command envelope ──────────────────────────────────────────

/// Outer wrapper: `{"id":...,"cmd":...}`.
#[derive(Debug, Clone, PartialEq, Deserialize)]
pub struct CommandEnvelope {
    pub id: u64,
    #[serde(flatten)]
    pub cmd: Command,
}

// ── Command response ──────────────────────────────────────────

/// Typed response that serialises to JSON without heap allocation.
///
/// - `Single`: Immediate response with status and data.
/// - `Error`: Error response with message.
/// - `AckThen`: Acknowledgment for two-phase commands.
/// - `NoResponse`: No response sent (e.g. broadcast-only).
pub enum CommandResponse {
    /// Immediate response: `{"id":<id>,"status":"<status>","data":<data>}`
    Single {
        id: u64,
        status: &'static str,
        data: CompactJson,
    },
    /// Error response: `{"id":<id>,"status":"error","message":"<message>"}`
    Error { id: u64, message: &'static str },
    /// Two-phase ack: `{"id":<id>,"status":"ok","data":<ack>}`
    AckThen { id: u64, ack: CompactJson },
    /// No response sent.
    NoResponse,
}

impl CommandResponse {
    /// Serialise the response into a fixed-size JSON buffer.
    ///
    /// Returns an empty `CompactJson` for `NoResponse`.
    pub fn serialize(&self) -> CompactJson {
        let mut buf: CompactJson = CompactJson::new();
        match self {
            Self::Single { id, status, data } => {
                let _ = write!(buf, r#"{{"id":{id},"status":"{status}","data":{data}}}"#);
            }
            Self::Error { id, message } => {
                let _ = write!(
                    buf,
                    r#"{{"id":{id},"status":"error","message":"{message}"}}"#,
                );
            }
            Self::AckThen { id, ack } => {
                let _ = write!(buf, r#"{{"id":{id},"status":"ok","data":{ack}}}"#);
            }
            Self::NoResponse => {}
        }
        buf
    }
}

// ── Handler context ───────────────────────────────────────────

/// Shared context passed to every command handler.
///
/// Provides access to system channels, calibration configuration,
/// and the response channel for two-phase command completion.
pub struct HandlerContext<'a> {
    /// Inter-task communication channels.
    pub channels: &'a SystemChannels,
    /// Current calibration configuration.
    pub cal_config: &'a CalibrationConfig,
    /// Response sender for two-phase command completion.
    /// The motor task sends `(cmd_id, CommandResponse)` here after execution.
    pub response_tx: &'a mpsc::SyncSender<(u64, CommandResponse)>,
}

// ── Command handler trait ─────────────────────────────────────

/// Trait implemented by each handler module.
///
/// A handler struct (zero-sized) implements this trait for the subset
/// of commands it manages. The `handle()` method matches on the
/// specific command variant and returns the appropriate response.
pub trait CommandHandler {
    /// Handle a single command.
    ///
    /// # Errors
    ///
    /// Returns `AppError` if the command cannot be processed.
    fn handle(
        &self,
        ctx: &HandlerContext<'_>,
        cmd: &Command,
        id: u64,
    ) -> Result<CommandResponse, AppError>;
}

// ── Tests ─────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    /// Helper to parse a command envelope from JSON.
    fn parse_envelope(json: &str) -> Result<CommandEnvelope, serde_json::Error> {
        serde_json::from_str(json)
    }

    // ════════════════════════════════════════════════════════════
    // Serde round-trip: all 32 command variants
    // ════════════════════════════════════════════════════════════

    #[test]
    fn test_cmd_serial_ping() {
        let env = parse_envelope(r#"{"id":1,"cmd":"serial.ping"}"#).unwrap();
        assert_eq!(env.id, 1);
        assert!(matches!(env.cmd, Command::SerialPing));
    }

    #[test]
    fn test_cmd_burette_fill() {
        let env = parse_envelope(r#"{"id":2,"cmd":"burette.fill","speed_ml_min":10.0}"#).unwrap();
        assert_eq!(env.id, 2);
        match env.cmd {
            Command::BuretteFill { speed_ml_min } => assert!((speed_ml_min - 10.0).abs() < 0.001),
            _ => panic!("wrong variant"),
        }
    }

    #[test]
    fn test_cmd_burette_empty() {
        let env = parse_envelope(r#"{"id":3,"cmd":"burette.empty"}"#).unwrap();
        assert!(matches!(env.cmd, Command::BuretteEmpty { .. }));
    }

    #[test]
    fn test_cmd_burette_dose_volume() {
        let env =
            parse_envelope(r#"{"id":4,"cmd":"burette.doseVolume","ml":5.0,"speed_ml_min":10.0}"#)
                .unwrap();
        match env.cmd {
            Command::BuretteDoseVolume { ml, speed_ml_min } => {
                assert!((ml - 5.0).abs() < 0.001);
                assert!((speed_ml_min - 10.0).abs() < 0.001);
            }
            _ => panic!("wrong variant"),
        }
    }

    #[test]
    fn test_cmd_burette_rinse() {
        let env =
            parse_envelope(r#"{"id":5,"cmd":"burette.rinse","cycles":3,"speed_ml_min":15.0}"#)
                .unwrap();
        match env.cmd {
            Command::BuretteRinse {
                cycles,
                speed_ml_min,
            } => {
                assert_eq!(cycles, 3);
                assert!((speed_ml_min - 15.0).abs() < 0.001);
            }
            _ => panic!("wrong variant"),
        }
    }

    #[test]
    fn test_cmd_burette_stop() {
        let env = parse_envelope(r#"{"id":6,"cmd":"burette.stop"}"#).unwrap();
        assert!(matches!(env.cmd, Command::BuretteStop));
    }

    #[test]
    fn test_cmd_burette_emergency_stop() {
        let env = parse_envelope(r#"{"id":7,"cmd":"burette.emergencyStop"}"#).unwrap();
        assert!(matches!(env.cmd, Command::BuretteEmergencyStop));
    }

    #[test]
    fn test_cmd_burette_get_status() {
        let env = parse_envelope(r#"{"id":8,"cmd":"burette.getStatus"}"#).unwrap();
        assert!(matches!(env.cmd, Command::BuretteGetStatus));
    }

    #[test]
    fn test_cmd_burette_move_steps() {
        let env =
            parse_envelope(r#"{"id":9,"cmd":"burette.moveSteps","steps":1000,"speed_hz":500}"#)
                .unwrap();
        match env.cmd {
            Command::BuretteMoveSteps { steps, speed_hz } => {
                assert_eq!(steps, 1000);
                assert_eq!(speed_hz, 500);
            }
            _ => panic!("wrong variant"),
        }
    }

    #[test]
    fn test_cmd_burette_move_to_stop() {
        let env = parse_envelope(r#"{"id":10,"cmd":"burette.moveToStop","speed_hz":300}"#).unwrap();
        assert!(matches!(env.cmd, Command::BuretteMoveToStop { .. }));
    }

    #[test]
    fn test_cmd_burette_set_direction() {
        let env = parse_envelope(r#"{"id":11,"cmd":"burette.setDirection","direction":"LiqIn"}"#)
            .unwrap();
        assert!(matches!(env.cmd, Command::BuretteSetDirection { .. }));
    }

    #[test]
    fn test_cmd_burette_cal_get() {
        let env = parse_envelope(r#"{"id":12,"cmd":"burette.cal.get"}"#).unwrap();
        assert!(matches!(env.cmd, Command::BuretteCalGet));
    }

    #[test]
    fn test_cmd_burette_cal_calc_volume() {
        let env =
            parse_envelope(r#"{"id":13,"cmd":"burette.cal.calcVolume","steps":10000}"#).unwrap();
        assert!(matches!(env.cmd, Command::BuretteCalCalcVolume { .. }));
    }

    #[test]
    fn test_cmd_burette_cal_calc_speed() {
        let env = parse_envelope(r#"{"id":14,"cmd":"burette.cal.calcSpeed","steps_per_sec":1000}"#)
            .unwrap();
        assert!(matches!(env.cmd, Command::BuretteCalCalcSpeed { .. }));
    }

    #[test]
    fn test_cmd_burette_cal_save() {
        let env = parse_envelope(r#"{"id":15,"cmd":"burette.cal.save"}"#).unwrap();
        assert!(matches!(env.cmd, Command::BuretteCalSave));
    }

    #[test]
    fn test_cmd_burette_cal_reset() {
        let env = parse_envelope(r#"{"id":16,"cmd":"burette.cal.reset"}"#).unwrap();
        assert!(matches!(env.cmd, Command::BuretteCalReset));
    }

    #[test]
    fn test_cmd_burette_cal_run() {
        let env = parse_envelope(
            r#"{"id":17,"cmd":"burette.cal.run","mode":"dose","speed_ml_min":15.0,"freq_hz":1500}"#,
        )
        .unwrap();
        assert!(matches!(env.cmd, Command::BuretteCalRun { .. }));
    }

    #[test]
    fn test_cmd_burette_cal_run_speed_seq() {
        let env = parse_envelope(
            r#"{"id":18,"cmd":"burette.cal.runSpeedSeq","freq_count":5,"speed_ml_min":10.0}"#,
        )
        .unwrap();
        assert!(matches!(env.cmd, Command::BuretteCalRunSpeedSeq { .. }));
    }

    #[test]
    fn test_cmd_burette_cal_get_result() {
        let env = parse_envelope(r#"{"id":19,"cmd":"burette.cal.getResult"}"#).unwrap();
        assert!(matches!(env.cmd, Command::BuretteCalGetResult));
    }

    #[test]
    fn test_cmd_valve_set_position() {
        let env =
            parse_envelope(r#"{"id":20,"cmd":"valve.setPosition","position":"Input"}"#).unwrap();
        assert!(matches!(env.cmd, Command::ValveSetPosition { .. }));
    }

    #[test]
    fn test_cmd_valve_get_state() {
        let env = parse_envelope(r#"{"id":21,"cmd":"valve.getState"}"#).unwrap();
        assert!(matches!(env.cmd, Command::ValveGetState));
    }

    #[test]
    fn test_cmd_temperature_read() {
        let env = parse_envelope(r#"{"id":22,"cmd":"temperature.read"}"#).unwrap();
        assert!(matches!(env.cmd, Command::TemperatureRead));
    }

    #[test]
    fn test_cmd_stall_guard_get_threshold() {
        let env = parse_envelope(r#"{"id":23,"cmd":"stallGuard.getThreshold"}"#).unwrap();
        assert!(matches!(env.cmd, Command::StallGuardGetThreshold));
    }

    #[test]
    fn test_cmd_stall_guard_set_threshold() {
        let env =
            parse_envelope(r#"{"id":24,"cmd":"stallGuard.setThreshold","value":128}"#).unwrap();
        assert!(matches!(env.cmd, Command::StallGuardSetThreshold { .. }));
    }

    #[test]
    fn test_cmd_adc_cal_get() {
        let env = parse_envelope(r#"{"id":25,"cmd":"adc.cal.get"}"#).unwrap();
        assert!(matches!(env.cmd, Command::AdcCalGet));
    }

    #[test]
    fn test_cmd_adc_cal_measure() {
        let env = parse_envelope(r#"{"id":26,"cmd":"adc.cal.measure"}"#).unwrap();
        assert!(matches!(env.cmd, Command::AdcCalMeasure { .. }));
    }

    #[test]
    fn test_cmd_adc_cal_compute() {
        let env = parse_envelope(r#"{"id":27,"cmd":"adc.cal.compute","raw_mv":1500}"#).unwrap();
        assert!(matches!(env.cmd, Command::AdcCalCompute { .. }));
    }

    #[test]
    fn test_cmd_adc_cal_save() {
        let env = parse_envelope(r#"{"id":28,"cmd":"adc.cal.save","a":1.5,"b":2.0}"#).unwrap();
        assert!(matches!(env.cmd, Command::AdcCalSave { .. }));
    }

    #[test]
    fn test_cmd_adc_cal_reset() {
        let env = parse_envelope(r#"{"id":29,"cmd":"adc.cal.reset"}"#).unwrap();
        assert!(matches!(env.cmd, Command::AdcCalReset));
    }

    #[test]
    fn test_cmd_system_get_status() {
        let env = parse_envelope(r#"{"id":30,"cmd":"system.getStatus"}"#).unwrap();
        assert!(matches!(env.cmd, Command::SystemGetStatus));
    }

    #[test]
    fn test_cmd_system_get_formatted_logs() {
        let env = parse_envelope(r#"{"id":31,"cmd":"system.getFormattedLogs"}"#).unwrap();
        assert!(matches!(env.cmd, Command::SystemGetFormattedLogs));
    }

    #[test]
    fn test_cmd_system_read_log() {
        let env = parse_envelope(r#"{"id":32,"cmd":"system.readLog"}"#).unwrap();
        assert!(matches!(env.cmd, Command::SystemReadLog));
    }

    // ════════════════════════════════════════════════════════════
    // CommandResponse serialization tests
    // ════════════════════════════════════════════════════════════

    #[test]
    fn test_response_serialize_single() {
        let mut data: CompactJson = CompactJson::new();
        write!(data, r#"{{"sts":"idle"}}"#).ok();
        let resp = CommandResponse::Single {
            id: 1,
            status: "ok",
            data,
        };
        let json = resp.serialize();
        assert_eq!(
            json.as_str(),
            r#"{"id":1,"status":"ok","data":{"sts":"idle"}}"#
        );
    }

    #[test]
    fn test_response_serialize_error() {
        let resp = CommandResponse::Error {
            id: 2,
            message: "test error",
        };
        let json = resp.serialize();
        assert_eq!(
            json.as_str(),
            r#"{"id":2,"status":"error","message":"test error"}"#
        );
    }

    #[test]
    fn test_response_serialize_ack_then() {
        let mut ack: CompactJson = CompactJson::new();
        write!(ack, r#"{{"ack":true}}"#).ok();
        let resp = CommandResponse::AckThen { id: 3, ack };
        let json = resp.serialize();
        assert_eq!(
            json.as_str(),
            r#"{"id":3,"status":"ok","data":{"ack":true}}"#
        );
    }

    #[test]
    fn test_response_serialize_no_response() {
        let resp = CommandResponse::NoResponse;
        let json = resp.serialize();
        assert_eq!(json.as_str(), "");
    }
}
