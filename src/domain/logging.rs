//! Pure-domain log entry type.
//!
//! Defines `LogEntry` and the size constants for its string fields.
//! These constants are re-exported by `domain::memory` for use by
//! the infrastructure logger.

use log::Level;

/// Maximum length of a log message string (bytes).
pub const MAX_LOG_MSG_SIZE: usize = 256;

/// Maximum length of a module path string (bytes).
pub const MAX_LOG_MODULE_SIZE: usize = 64;

/// A single log entry stored in the ring buffer.
#[derive(Debug, Clone)]
pub struct LogEntry {
    pub ts_ms: u64,
    pub level: Level,
    pub module: heapless::String<MAX_LOG_MODULE_SIZE>,
    pub msg: heapless::String<MAX_LOG_MSG_SIZE>,
}
