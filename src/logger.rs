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

        // SAFETY:
        //   Invariant: esp_timer_get_time is a read-only FFI call, safe after
        //   FreeRTOS scheduler init (which completed before main()).
        //   Return value is microseconds since boot, always non-negative.
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
/// Format: `{"total":<seq>,"entries":[{"ts":...,"level":"...","msg":"..."},...]}`
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
        let prev_len = result.len();
        if i > 0 && write!(result, ",").is_err() {
            result.truncate(prev_len);
            break;
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
            r#"{{"ts":{},"level":"{}","msg":""#,
            entry.ts_ms, level_str,
        )
        .ok();
        if !json_escape(entry.msg.as_str(), &mut result) || write!(result, "\"}}").is_err() {
            result.truncate(prev_len);
            break;
        }
    }
    // Drop the lock before the final JSON close so we don't hold it during
    // the return (the MutexGuard is no longer needed after iteration).
    drop(buf);

    let before_close = result.len();
    if write!(result, r"]}}").is_err() {
        result.truncate(before_close);
    }
    result
}

/// JSON-escape a string slice into a `heapless::String` output buffer.
///
/// Escapes `"`, `\`, `\n`, `\r`, `\t`, and control characters (< 0x20).
#[cfg(test)]
mod tests {
    use super::*;
    use crate::domain::logging::LogEntry;
    use log::Level;

    fn push_entry(ts_ms: u64, level: Level, msg: &str) {
        if let Ok(mut buf) = LOGGER.inner.lock() {
            let mut hmsg: heapless::String<MAX_LOG_MSG_SIZE> = heapless::String::new();
            let _ = write!(hmsg, "{msg}");
            let mut module: heapless::String<MAX_LOG_MODULE_SIZE> = heapless::String::new();
            let _ = write!(module, "test");
            buf.push(LogEntry {
                ts_ms,
                level,
                module,
                msg: hmsg,
            });
        }
    }

    fn clear_entries() {
        if let Ok(mut buf) = LOGGER.inner.lock() {
            while buf.entries.pop_front().is_some() {}
        }
    }

    #[test]
    fn json_output_is_valid_empty() {
        clear_entries();
        let json = get_entries_json(10);
        let val: serde_json::Value =
            serde_json::from_str(json.as_str()).expect("get_entries_json() must return valid JSON");
        assert_eq!(val["total"], 0);
        assert!(val["entries"].as_array().unwrap().is_empty());
    }

    #[test]
    fn json_output_single_entry() {
        clear_entries();
        push_entry(1000, Level::Info, "hello world");
        let json = get_entries_json(10);
        let val: serde_json::Value =
            serde_json::from_str(json.as_str()).expect("get_entries_json() must return valid JSON");
        let entries = val["entries"].as_array().unwrap();
        assert_eq!(entries.len(), 1);
        assert_eq!(entries[0]["level"], "INFO");
        assert_eq!(entries[0]["msg"], "hello world");
        assert_eq!(entries[0]["ts"], 1000);
    }

    #[test]
    fn json_output_multiple_entries() {
        clear_entries();
        push_entry(1, Level::Error, "err msg");
        push_entry(2, Level::Warn, "warn msg");
        push_entry(3, Level::Info, "info msg");
        let json = get_entries_json(10);
        let val: serde_json::Value =
            serde_json::from_str(json.as_str()).expect("get_entries_json() must return valid JSON");
        let entries = val["entries"].as_array().unwrap();
        assert_eq!(entries.len(), 3);
        assert_eq!(entries[0]["level"], "ERROR");
        assert_eq!(entries[0]["msg"], "err msg");
        assert_eq!(entries[1]["level"], "WARN");
        assert_eq!(entries[1]["msg"], "warn msg");
        assert_eq!(entries[2]["level"], "INFO");
        assert_eq!(entries[2]["msg"], "info msg");
    }

    #[test]
    fn json_output_limit() {
        clear_entries();
        push_entry(1, Level::Info, "first");
        push_entry(2, Level::Info, "second");
        push_entry(3, Level::Info, "third");
        let json = get_entries_json(2);
        let val: serde_json::Value =
            serde_json::from_str(json.as_str()).expect("get_entries_json() must return valid JSON");
        let entries = val["entries"].as_array().unwrap();
        assert_eq!(entries.len(), 2);
        assert_eq!(entries[0]["msg"], "second");
        assert_eq!(entries[1]["msg"], "third");
    }

    #[test]
    fn json_output_escapes_special_chars() {
        clear_entries();
        push_entry(1, Level::Info, r#"he said "hello" & 'world'"#);
        let json = get_entries_json(10);
        let val: serde_json::Value =
            serde_json::from_str(json.as_str()).expect("get_entries_json() must return valid JSON");
        assert_eq!(val["entries"][0]["msg"], r#"he said "hello" & 'world'"#);
    }

    #[test]
    fn json_output_fits_buffer() {
        clear_entries();
        let long = "x".repeat(200);
        push_entry(1, Level::Info, &long);
        let json = get_entries_json(10);
        assert!(json.len() <= MAX_RESPONSE_SIZE);
        let val: serde_json::Value =
            serde_json::from_str(json.as_str()).expect("get_entries_json() must return valid JSON");
        assert!(val["entries"][0]["msg"].as_str().unwrap().len() <= 200);
    }

    #[test]
    fn json_buffer_overflow_truncates_cleanly() {
        clear_entries();
        // Push 4 entries with long messages to force buffer overflow
        let long_msg = "A".repeat(200);
        for i in 0..4 {
            push_entry(i as u64, Level::Info, &long_msg);
        }
        let json = get_entries_json(10);
        // The result must be valid JSON, even if truncated
        let parsed: serde_json::Value =
            serde_json::from_str(json.as_str()).expect("overflow output must be valid JSON");
        let entries = parsed["entries"].as_array().unwrap();
        // At least some entries should have fit
        assert!(
            entries.len() > 0,
            "buffer overflow should preserve at least some entries"
        );
        // Each entry has valid fields
        for entry in entries {
            assert!(entry["ts"].as_u64().is_some());
            assert_eq!(entry["level"], "INFO");
            assert!(entry["msg"].as_str().is_some());
        }
    }

    #[test]
    fn json_output_traces_through_all_levels() {
        clear_entries();
        push_entry(1, Level::Trace, "trace");
        push_entry(2, Level::Debug, "debug");
        push_entry(3, Level::Info, "info");
        push_entry(4, Level::Warn, "warn");
        push_entry(5, Level::Error, "error");
        let json = get_entries_json(10);
        let val: serde_json::Value =
            serde_json::from_str(json.as_str()).expect("get_entries_json() must return valid JSON");
        let levels: Vec<&str> = val["entries"]
            .as_array()
            .unwrap()
            .iter()
            .map(|e| e["level"].as_str().unwrap())
            .collect();
        assert_eq!(levels, ["TRACE", "DEBUG", "INFO", "WARN", "ERROR"]);
    }

    #[test]
    fn json_full_output_is_valid() {
        clear_entries();
        push_entry(1, Level::Info, "Hello World");
        push_entry(2, Level::Error, "Test msg");
        let json = get_entries_json(10);
        let parsed: serde_json::Value =
            serde_json::from_str(json.as_str()).expect("Output must be valid JSON");
        assert!(parsed["entries"].is_array());
        assert_eq!(parsed["total"], 2);
        let entries = parsed["entries"].as_array().unwrap();
        assert_eq!(entries.len(), 2);
        assert_eq!(entries[0]["level"], "INFO");
        assert_eq!(entries[0]["msg"], "Hello World");
        assert_eq!(entries[0]["ts"], 1);
        assert_eq!(entries[1]["level"], "ERROR");
        assert_eq!(entries[1]["msg"], "Test msg");
        assert_eq!(entries[1]["ts"], 2);
    }
}

fn json_escape(s: &str, out: &mut heapless::String<MAX_RESPONSE_SIZE>) -> bool {
    for c in s.chars() {
        match c {
            '"' => {
                if out.push_str("\\\"").is_err() {
                    return false;
                }
            }
            '\\' => {
                if out.push_str("\\\\").is_err() {
                    return false;
                }
            }
            '\n' => {
                if out.push_str("\\n").is_err() {
                    return false;
                }
            }
            '\r' => {
                if out.push_str("\\r").is_err() {
                    return false;
                }
            }
            '\t' => {
                if out.push_str("\\t").is_err() {
                    return false;
                }
            }
            c if (c as u32) < 0x20 => {
                if write!(out, "\\u{:04x}", c as u32).is_err() {
                    return false;
                }
            }
            c => {
                let mut buf = [0u8; 4];
                let s = c.encode_utf8(&mut buf);
                if out.push_str(s).is_err() {
                    return false;
                }
            }
        }
    }
    true
}
