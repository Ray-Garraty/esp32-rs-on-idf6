//! Command dispatch — routes each `Command` variant to the correct handler.
//!
//! Uses a single match expression that maps every variant to its handler
//! module. This is the central routing table for all 32 wire-protocol commands.

use crate::application::command::{Command, CommandHandler, CommandResponse, HandlerContext};
use crate::application::handlers::burette_cal::BuretteCalHandler;
use crate::application::handlers::burette_ops::BuretteOpsHandler;
use crate::application::handlers::sensors::SensorsHandler;
use crate::application::handlers::serial::SerialPingHandler;
use crate::application::handlers::system::SystemHandler;
use crate::application::handlers::valve::ValveHandler;
use crate::errors::AppError;

/// Route a command to the appropriate handler and return the response.
///
/// Each handler module is a zero-sized struct that implements `CommandHandler`.
/// The dispatch match maps every `Command` variant to its handler.
///
/// # Errors
///
/// Propagates errors from the handler.
// All 32 variants are deliberately enumerated for exhaustiveness checking
// at compile time. Groups route to the same handler module intentionally.
#[allow(clippy::match_same_arms)]
pub fn dispatch(
    ctx: &HandlerContext<'_>,
    cmd: &Command,
    id: u64,
) -> Result<CommandResponse, AppError> {
    // Singleton handler instances (zero-sized, no allocation).
    let burette_ops = BuretteOpsHandler;
    let burette_cal = BuretteCalHandler;
    let valve = ValveHandler;
    let sensors = SensorsHandler;
    let system = SystemHandler;
    let serial = SerialPingHandler;

    match cmd {
        // ── Serial ──
        Command::SerialPing => serial.handle(ctx, cmd, id),

        // ── System ──
        Command::SystemGetStatus | Command::SystemGetFormattedLogs | Command::SystemReadLog => {
            system.handle(ctx, cmd, id)
        }

        // ── Burette operations ──
        Command::BuretteFill { .. }
        | Command::BuretteEmpty { .. }
        | Command::BuretteDoseVolume { .. }
        | Command::BuretteRinse { .. }
        | Command::BuretteStop
        | Command::BuretteEmergencyStop
        | Command::BuretteGetStatus
        | Command::BuretteMoveSteps { .. }
        | Command::BuretteMoveToStop { .. }
        | Command::BuretteSetDirection { .. } => burette_ops.handle(ctx, cmd, id),

        // ── Burette calibration ──
        Command::BuretteCalGet
        | Command::BuretteCalCalcVolume { .. }
        | Command::BuretteCalCalcSpeed { .. }
        | Command::BuretteCalSave
        | Command::BuretteCalReset
        | Command::BuretteCalRun { .. }
        | Command::BuretteCalRunSpeedSeq { .. }
        | Command::BuretteCalGetResult => burette_cal.handle(ctx, cmd, id),

        // ── Valve ──
        Command::ValveSetPosition { .. } | Command::ValveGetState => valve.handle(ctx, cmd, id),

        // ── Sensors ──
        Command::TemperatureRead
        | Command::StallGuardGetThreshold
        | Command::StallGuardSetThreshold { .. }
        | Command::AdcCalGet
        | Command::AdcCalMeasure { .. }
        | Command::AdcCalCompute { .. }
        | Command::AdcCalSave { .. }
        | Command::AdcCalReset => sensors.handle(ctx, cmd, id),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::domain::calibration::CalibrationConfig;
    use crate::domain::channels::SystemChannels;

    /// Helper: create a minimal HandlerContext for testing.
    fn test_ctx() -> HandlerContext<'static> {
        let channels = Box::leak(Box::new(SystemChannels::new()));
        let cal_config = Box::leak(Box::new(CalibrationConfig::new()));
        HandlerContext {
            channels,
            cal_config,
        }
    }

    #[test]
    fn test_dispatch_serial_ping() {
        let ctx = test_ctx();
        let cmd = Command::SerialPing;
        let resp = dispatch(&ctx, &cmd, 1).unwrap();
        let json = resp.serialize();
        assert!(json.contains(r#""status":"ok""#));
    }

    #[test]
    fn test_dispatch_system_get_status() {
        let ctx = test_ctx();
        let cmd = Command::SystemGetStatus;
        let resp = dispatch(&ctx, &cmd, 2).unwrap();
        let json = resp.serialize();
        assert!(json.contains(r#""status":"ok""#));
    }

    #[test]
    fn test_dispatch_burette_stop() {
        let ctx = test_ctx();
        let cmd = Command::BuretteStop;
        let resp = dispatch(&ctx, &cmd, 3).unwrap();
        let json = resp.serialize();
        assert!(json.contains(r#""status":"ok""#));
    }

    #[test]
    fn test_dispatch_burette_emergency_stop() {
        let ctx = test_ctx();
        let cmd = Command::BuretteEmergencyStop;
        let resp = dispatch(&ctx, &cmd, 4).unwrap();
        let json = resp.serialize();
        assert!(json.contains(r#""status":"ok""#));
    }

    #[test]
    fn test_dispatch_valve_get_state() {
        let ctx = test_ctx();
        let cmd = Command::ValveGetState;
        let resp = dispatch(&ctx, &cmd, 5).unwrap();
        let json = resp.serialize();
        assert!(json.contains(r#""status":"ok""#));
    }
}
