use core::fmt::Write;
use log::{Level, LevelFilter, Log, Metadata, Record};
use std::sync::Mutex;

struct LogEntry {
    ts_ms: u64,
    level: Level,
    msg: heapless::String<256>,
}

struct RingBuffer {
    entries: heapless::Deque<LogEntry, 100>,
    seq: u64,
}

impl RingBuffer {
    const fn new() -> Self {
        Self {
            entries: heapless::Deque::new(),
            seq: 0,
        }
    }

    fn push(&mut self, entry: LogEntry) {
        if self.entries.is_full() {
            self.entries.pop_front();
        }
        self.entries.push_back(entry).ok();
        self.seq += 1;
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
        let args = record.args();

        let mut msg: heapless::String<256> = heapless::String::new();
        write!(msg, "{}", args).ok();

        let ts_ms = unsafe { esp_idf_sys::esp_timer_get_time() as u64 / 1000 };

        if let Ok(mut buf) = self.inner.lock() {
            buf.push(LogEntry { ts_ms, level, msg });
        }
    }

    fn flush(&self) {}
}

pub fn init() {
    log::set_logger(&LOGGER).ok();
    log::set_max_level(LevelFilter::Info);
}

pub fn get_entries_json(limit: usize) -> String {
    let buf = match LOGGER.inner.lock() {
        Ok(b) => b,
        Err(_) => return r#"{"total":0,"entries":[]}"#.to_string(),
    };

    let mut result = String::new();
    result.push_str(r#"{"total":"#);
    let _ = write!(result, "{}", buf.seq);
    result.push_str(r#","entries":["#);

    let total = buf.entries.len();
    let start = total.saturating_sub(limit);
    for (i, entry) in buf.entries.iter().skip(start).enumerate() {
        if i > 0 {
            result.push(',');
        }
        let level_str = match entry.level {
            Level::Error => "ERROR",
            Level::Warn => "WARN",
            Level::Info => "INFO",
            Level::Debug => "DEBUG",
            Level::Trace => "TRACE",
        };
        let _ = write!(
            result,
            r#"{{"ts":{},"l":"{}","m":"{}"}}"#,
            entry.ts_ms,
            level_str,
            json_escape(entry.msg.as_str()),
        );
    }

    result.push_str("]}");
    result
}

fn json_escape(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for c in s.chars() {
        match c {
            '"' => out.push_str(r#"\""#),
            '\\' => out.push_str(r#"\\"#),
            '\n' => out.push_str(r#"\n"#),
            '\r' => out.push_str(r#"\r"#),
            '\t' => out.push_str(r#"\t"#),
            c if (c as u32) < 0x20 => {
                let _ = write!(out, "\\u{:04x}", c as u32);
            }
            c => out.push(c),
        }
    }
    out
}
