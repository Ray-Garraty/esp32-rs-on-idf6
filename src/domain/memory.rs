// ── Fixed-size buffer constants ───────────────────────────────
pub const MAX_COMMAND_SIZE: usize = 256;
pub const MAX_RESPONSE_SIZE: usize = 512;
pub const LOG_BUFFER_SIZE: usize = 100;

pub use super::logging::{MAX_LOG_MODULE_SIZE, MAX_LOG_MSG_SIZE};
pub const ADC_BUF_SIZE: usize = 64;
pub const DNS_BUF_SIZE: usize = 512;
pub const BLE_CMD_QUEUE_SIZE: usize = 8;
pub const HTTP_POST_BUF_SIZE: usize = 512;

use crate::domain::logging::LogEntry;
use heapless::{Deque, Vec};

/// Command buffer for USB / BLE / HTTP command input.
pub type CommandBuffer = Vec<u8, MAX_COMMAND_SIZE>;

/// Response buffer for JSON serialization.
pub type ResponseBuffer = Vec<u8, MAX_RESPONSE_SIZE>;

/// Ring-buffer log type for HTTP log access.
pub type LogBuffer = Deque<LogEntry, LOG_BUFFER_SIZE>;
