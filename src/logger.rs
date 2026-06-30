//! Xtensa-specific logging infrastructure: ring buffer, log impl, JSON export.
//!
//! This module is only available on ESP32 (xtensa) targets because it depends on
//! `esp_idf_sys::esp_timer_get_time()` and uses `println!` for UART output.

#![allow(clippy::module_name_repetitions)]

use core::fmt::Write as FmtWrite;
use log::{Level, LevelFilter, Log, Metadata, Record};
use std::sync::Mutex;

use heapless::Deque;

use crate::domain::logging::LogEntry;
use crate::domain::memory::{
    LOG_BUFFER_SIZE, MAX_LOG_MODULE_SIZE, MAX_LOG_MSG_SIZE, MAX_RESPONSE_SIZE,
};

/// Ring buffer of log entries with sequence counter.
struct RingBuffer {
    entries: Deque<LogEntry, LOG_BUFFER_SIZE>,
    seq: u64,
}

impl RingBuffer {
    const fn new() -> Self {
        Self {
            entries: Deque::new(),
            seq: 0,
        }
    }

    /// Push a log entry; evicts oldest entry if buffer is full.
    fn push(&mut self, entry: LogEntry) {
        if self.entries.is_full() {
            self.entries.pop_front();
        }
        // push_back only fails if full, but we already evicted
        self.entries.push_back(entry).ok();
        self.seq += 1;
    }
}

impl Default for RingBuffer {
    fn default() -> Self {
        Self::new()
    }
}

static LOGGER: Logger = Logger {
    inner: Mutex::new(RingBuffer::new()),
};

struct Logger {
    inner: Mutex<RingBuffer>,
}

impl Log for Logger {
    fn enabled(&self, _metadata: &Metadata) -> bool {
        true
    }

    fn log(&self, record: &Record) {
        let level = record.level();
        let target = record.target();

        // Suppress esp_idf_svc::http noise at WARN level
        if target.starts_with("esp_idf_svc::http") && level <= Level::Warn {
            return;
        }

        let mut msg: heapless::String<MAX_LOG_MSG_SIZE> = heapless::String::new();
        write!(msg, "{}", record.args()).ok();

        // UART console output — only available on the ESP32 target
        println!("[{}] {}", level, record.args());

        // Safety: esp_timer_get_time() is safe to call at any point after
        // the FreeRTOS scheduler has started (which it has by main()).
        // Return value is microseconds since boot, always non-negative.
        let ts_ms = unsafe { u64::try_from(esp_idf_sys::esp_timer_get_time()).unwrap_or(0) / 1000 };

        let mut module: heapless::String<MAX_LOG_MODULE_SIZE> = heapless::String::new();
        write!(module, "{target}").ok();

        if let Ok(mut buf) = self.inner.lock() {
            buf.push(LogEntry {
                ts_ms,
                level,
                module,
                msg,
            });
        }
    }

    fn flush(&self) {}
}

/// Initialise the global logger. Must be called exactly once at boot.
pub fn init() {
    log::set_logger(&LOGGER).ok();
    log::set_max_level(LevelFilter::Info);
}

/// Return the most recent log entries as a JSON string (bounded to 512 bytes).
///
/// Format: `{"total":<seq>,"entries":[{"ts":...,"l":"...","m":"..."},...]}`
pub fn get_entries_json(limit: usize) -> heapless::String<MAX_RESPONSE_SIZE> {
    let Ok(buf) = LOGGER.inner.lock() else {
        let mut fallback: heapless::String<MAX_RESPONSE_SIZE> = heapless::String::new();
        write!(fallback, r#"{{"total":0,"entries":[]}}"#).ok();
        return fallback;
    };

    let mut result: heapless::String<MAX_RESPONSE_SIZE> = heapless::String::new();
    write!(result, r#"{{"total":"#).ok();
    write!(result, "{}", buf.seq).ok();
    write!(result, r#","entries":["#).ok();

    let total = buf.entries.len();
    let start = total.saturating_sub(limit);
    for (i, entry) in buf.entries.iter().skip(start).enumerate() {
        if i > 0 {
            write!(result, ",").ok();
        }
        let level_str = match entry.level {
            Level::Error => "ERROR",
            Level::Warn => "WARN",
            Level::Info => "INFO",
            Level::Debug => "DEBUG",
            Level::Trace => "TRACE",
        };
        write!(
            result,
            r#"{{"ts":{},"l":"{}","m":""#,
            entry.ts_ms, level_str,
        )
        .ok();
        json_escape(entry.msg.as_str(), &mut result);
        write!(result, r"}}").ok();
    }
    // Drop the lock before the final JSON close so we don't hold it during
    // the return (the MutexGuard is no longer needed after iteration).
    drop(buf);

    write!(result, r"]}}").ok();
    result
}

/// JSON-escape a string slice into a `heapless::String` output buffer.
///
/// Escapes `"`, `\`, `\n`, `\r`, `\t`, and control characters (< 0x20).
fn json_escape(s: &str, out: &mut heapless::String<MAX_RESPONSE_SIZE>) {
    for c in s.chars() {
        match c {
            '"' => {
                out.push_str("\\\"").ok();
            }
            '\\' => {
                out.push_str("\\\\").ok();
            }
            '\n' => {
                out.push_str("\\n").ok();
            }
            '\r' => {
                out.push_str("\\r").ok();
            }
            '\t' => {
                out.push_str("\\t").ok();
            }
            c if (c as u32) < 0x20 => {
                let _ = write!(out, "\\u{:04x}", c as u32);
            }
            c => {
                let mut buf = [0u8; 4];
                let s = c.encode_utf8(&mut buf);
                out.push_str(s).ok();
            }
        }
    }
}
