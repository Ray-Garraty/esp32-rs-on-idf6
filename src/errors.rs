//! Three-level error hierarchy with automatic `?` conversion.
//!
//! ```text
//! AppError
//!  +-- Hardware(HardwareError)
//!  |    +-- StepperMotor(StepperError)
//!  |    +-- Sensor(SensorError)
//!  |    +-- Network(NetworkError)
//!  +-- Protocol(ProtocolError)
//!  +-- State(StateError)
//!  +-- Resource(ResourceError)
//! ```

use crate::domain::types::LimitSwitchId;

// ── Top-level error ───────────────────────────────────────────

#[derive(Debug, Clone, thiserror::Error)]
pub enum AppError {
    #[error("Hardware error: {0}")]
    Hardware(#[from] HardwareError),
    #[error("Protocol error: {0}")]
    Protocol(#[from] ProtocolError),
    #[error("State error: {0}")]
    State(#[from] StateError),
    #[error("Resource error: {0}")]
    Resource(#[from] ResourceError),
}

// ── Second level: Hardware ────────────────────────────────────

#[derive(Debug, Clone, thiserror::Error)]
pub enum HardwareError {
    #[error("Stepper motor error: {0}")]
    StepperMotor(#[from] StepperError),
    #[error("Sensor error: {0}")]
    Sensor(#[from] SensorError),
    #[error("Network error: {0}")]
    Network(#[from] NetworkError),
}

// ── Third level: Leaf errors ──────────────────────────────────

#[derive(Debug, Clone, thiserror::Error)]
pub enum StepperError {
    #[error("Stepper init failed: {reason}")]
    InitFailed { reason: &'static str },
    #[error("RMT error: code={code}")]
    Rmt { code: i32 },
    /// FUTURE: used when merging limit switch into stepper state machine in Phase 5.
    /// Currently, the driver uses StepperError::LimitSwitchReached for stop_flag polling.
    #[allow(dead_code)]
    #[error("Limit switch triggered: {switch:?}")]
    LimitSwitchTriggered { switch: LimitSwitchId },
    #[error("Limit switch reached (stop flag)")]
    LimitSwitchReached,
    #[error("Operation timeout: {operation}")]
    Timeout { operation: &'static str },
}

#[derive(Debug, Clone, thiserror::Error)]
pub enum SensorError {
    #[error("ADC read failed")]
    AdcReadFailed,
    #[error("Temperature sensor not detected")]
    TempSensorNotDetected,
    #[error("Temperature read glitch")]
    TempReadGlitch,
}

#[derive(Debug, Clone, thiserror::Error)]
pub enum NetworkError {
    #[error("WiFi connection failed")]
    WifiConnectionFailed,
}

#[derive(Debug, Clone, thiserror::Error)]
pub enum ProtocolError {
    #[error("Invalid JSON")]
    InvalidJson,
    #[error("Unknown command")]
    UnknownCommand,
    #[error("Missing parameter")]
    MissingParam,
}

#[derive(Debug, Clone, thiserror::Error)]
pub enum StateError {
    #[error("Burette busy")]
    Busy,
    #[error("Invalid state transition")]
    InvalidTransition,
    #[error("Already running")]
    AlreadyRunning,
}

#[derive(Debug, Clone, thiserror::Error)]
pub enum ResourceError {
    #[error("NVS open failed")]
    NvsOpenFailed,
    #[error("Out of memory")]
    OutOfMemory,
}

// ── Recovery trait ────────────────────────────────────────────

/// Suggested recovery action for an error.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RecoveryAction {
    /// Retry the failed operation (e.g. WiFi reconnect, RMT chunk transmit).
    Retry,
    /// Reset the subsystem to a known state.
    Reset,
    /// Ignore the error and continue (e.g. malformed command).
    Ignore,
}

/// Trait for errors that may be recoverable at runtime.
pub trait Recoverable {
    /// Returns `true` if the system can attempt recovery from this error.
    fn can_recover(&self) -> bool;

    /// Returns the suggested recovery action, if any.
    fn recovery_action(&self) -> Option<RecoveryAction>;
}

impl Recoverable for AppError {
    fn can_recover(&self) -> bool {
        matches!(
            self,
            Self::Hardware(
                HardwareError::Network(_) | HardwareError::StepperMotor(StepperError::Rmt { .. })
            )
        )
    }

    fn recovery_action(&self) -> Option<RecoveryAction> {
        match self {
            Self::Hardware(HardwareError::Network(_)) => Some(RecoveryAction::Retry),
            Self::Hardware(HardwareError::StepperMotor(StepperError::Rmt { .. })) => {
                Some(RecoveryAction::Retry)
            }
            _ => None,
        }
    }
}

// ── Convenience `From` impls ──────────────────────────────────

/// Shortcut: allow `?` on `StepperError` in functions returning `Result<_, AppError>`.
impl From<StepperError> for AppError {
    fn from(e: StepperError) -> Self {
        Self::Hardware(HardwareError::StepperMotor(e))
    }
}

/// Convert ESP-IDF errors into StepperError (xtensa-only, since `esp_idf_sys`
/// does not exist on the host build target).
#[cfg(target_arch = "xtensa")]
impl From<esp_idf_sys::EspError> for StepperError {
    fn from(e: esp_idf_sys::EspError) -> Self {
        Self::Rmt { code: e.code() }
    }
}

/// Convert ESP-IDF errors into SensorError.
#[cfg(target_arch = "xtensa")]
impl From<esp_idf_sys::EspError> for SensorError {
    fn from(_e: esp_idf_sys::EspError) -> Self {
        Self::AdcReadFailed
    }
}

/// Convert ESP-IDF errors into ResourceError.
#[cfg(target_arch = "xtensa")]
impl From<esp_idf_sys::EspError> for ResourceError {
    fn from(_e: esp_idf_sys::EspError) -> Self {
        Self::NvsOpenFailed
    }
}
