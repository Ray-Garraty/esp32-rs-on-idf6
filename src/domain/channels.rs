//! System channels for thread-safe command dispatch, status broadcast, and log transport.
//!
//! Uses `std::sync::mpsc` Sender/Receiver pairs. Pure domain — no ESP-IDF imports.

#![forbid(unsafe_code)]
use crate::domain::burette::{BuretteCommand, BuretteOperation, BuretteState};
use crate::domain::logging::LogEntry;
use crate::domain::types::Ml;
use std::sync::mpsc;

/// A status update broadcast from the burette state machine to subscribers.
#[derive(Debug, Clone)]
pub struct StatusUpdate {
    pub state: BuretteState,
    pub volume_ml: Ml,
    pub operation: BuretteOperation,
    pub details: heapless::String<64>,
}

/// Channel pairs for inter-task communication.
///
/// - `cmd_tx` / `cmd_rx`:  Send `BuretteCommand` from main loop → motor task.
/// - `status_tx` / `status_rx`:  Broadcast `StatusUpdate` from motor task → BLE notify / HTTP WS.
/// - `log_tx` / `log_rx`:  Transport `LogEntry` from any task → HTTP log endpoint.
#[derive(Debug)]
pub struct SystemChannels {
    pub cmd_tx: mpsc::Sender<BuretteCommand>,
    pub cmd_rx: mpsc::Receiver<BuretteCommand>,
    pub status_tx: mpsc::Sender<StatusUpdate>,
    pub status_rx: mpsc::Receiver<StatusUpdate>,
    pub log_tx: mpsc::Sender<LogEntry>,
    pub log_rx: mpsc::Receiver<LogEntry>,
}

impl SystemChannels {
    /// Create a new set of channel pairs.
    pub fn new() -> Self {
        let (cmd_tx, cmd_rx) = mpsc::channel();
        let (status_tx, status_rx) = mpsc::channel();
        let (log_tx, log_rx) = mpsc::channel();
        Self {
            cmd_tx,
            cmd_rx,
            status_tx,
            status_rx,
            log_tx,
            log_rx,
        }
    }
}

impl Default for SystemChannels {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::domain::burette::BuretteOperation;
    use crate::domain::types::Ml;

    #[test]
    fn test_channels_send_command() {
        let channels = SystemChannels::new();
        let cmd = BuretteCommand::Fill {
            speed: crate::domain::types::MlMin(10.0),
        };
        channels.cmd_tx.send(cmd.clone()).unwrap();

        let received = channels.cmd_rx.recv().unwrap();
        assert_eq!(received, cmd);
    }

    #[test]
    fn test_channels_send_status() {
        let channels = SystemChannels::new();
        let status = StatusUpdate {
            state: BuretteState::Idle,
            volume_ml: Ml(5.0),
            operation: BuretteOperation::None,
            details: {
                let mut s: heapless::String<64> = heapless::String::new();
                s.push_str("test").ok();
                s
            },
        };
        channels.status_tx.send(status.clone()).unwrap();

        let received = channels.status_rx.recv().unwrap();
        assert_eq!(received.state, status.state);
        assert_eq!(received.volume_ml, status.volume_ml);
        assert_eq!(received.operation, status.operation);
    }

    #[test]
    fn test_channels_send_log() {
        let channels = SystemChannels::new();
        let mut module: heapless::String<64> = heapless::String::new();
        module.push_str("test").ok();
        let mut msg: heapless::String<256> = heapless::String::new();
        msg.push_str("hello").ok();
        let entry = LogEntry {
            ts_ms: 1000,
            level: log::Level::Info,
            module,
            msg,
        };
        channels.log_tx.send(entry.clone()).unwrap();

        let received = channels.log_rx.recv().unwrap();
        assert_eq!(received.ts_ms, entry.ts_ms);
        assert_eq!(received.level, entry.level);
    }
}
