//! USB-Serial line reader with atomic silent mode flag.
//!
//! Accumulates bytes until a newline is received, then returns the
//! complete command line. Designed for non-blocking use in the main loop.

#![forbid(unsafe_code)]
use crate::application::scheduler;
use crate::domain::memory::MAX_COMMAND_SIZE;
use core::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use heapless::Vec;

/// Serial silent mode — when `true`, UART output is suppressed during
/// command processing (e.g., to avoid interleaving debug output with
/// JSON responses).
pub static G_SERIAL_SILENT: AtomicBool = AtomicBool::new(false);

/// Timestamp (ms since boot) of last USB serial data byte.
/// `0` means no data yet. Updated in `push_byte()` with `Release`.
/// Uses `AtomicU32` (xtensa lacks 64-bit atomics). Wraps at ~49.7 days.
pub static G_LAST_SERIAL_ACTIVITY: AtomicU32 = AtomicU32::new(0);

/// Accumulates UART bytes into a line buffer.
///
/// Call `push_byte()` for each received byte. When a complete line
/// (terminated by `\n`) is received, the contents are copied into
/// `out` without the newline, and the method returns `true`.
///
/// CR bytes (`\r`) are silently ignored.
/// If the buffer overflows, it is cleared (overflow is not an error).
pub struct SerialReader {
    buf: Vec<u8, MAX_COMMAND_SIZE>,
}

impl SerialReader {
    /// Create a new empty serial reader.
    pub const fn new() -> Self {
        Self { buf: Vec::new() }
    }
}

impl Default for SerialReader {
    fn default() -> Self {
        Self::new()
    }
}

impl SerialReader {
    /// Push a received byte.
    ///
    /// Returns `true` when a full line (terminated by `\n`) is available
    /// in `out`. The `out` buffer contains the line without the trailing `\n`.
    ///
    /// This method also updates `G_LAST_SERIAL_ACTIVITY` with the current
    /// elapsed milliseconds when a data byte (not `\n` or `\r`) is received.
    ///
    /// # Lint justification
    ///
    /// `cast_possible_truncation`: `elapsed_ms() as u32` truncates u64→u32,
    /// but the tick counter wraps at ~49.7 days which is acceptable for
    /// a lab instrument that is rebooted periodically. The wrapping
    /// arithmetic in `is_usb_alive()` handles the wrap correctly.
    ///
    /// # Safety
    ///
    /// The `out` buffer must be large enough to hold `MAX_COMMAND_SIZE` bytes.
    #[allow(clippy::cast_possible_truncation)]
    pub fn push_byte(&mut self, byte: u8, out: &mut Vec<u8, MAX_COMMAND_SIZE>) -> bool {
        if byte == b'\n' {
            // Complete line received — copy to out and clear internal buffer.
            out.clear();
            // `extend_from_slice` returns `Err` only if the slice exceeds capacity.
            // We clear `out` first, and `self.buf` is at most `MAX_COMMAND_SIZE`,
            // so it fits.
            let _ = out.extend_from_slice(&self.buf);
            self.buf.clear();
            return true;
        }
        // Ignore carriage return.
        if byte == b'\r' {
            return false;
        }
        // Update USB activity timestamp (data byte received)
        G_LAST_SERIAL_ACTIVITY.store(scheduler::elapsed_ms() as u32, Ordering::Release);
        // Append to buffer if there is space; silently drop on overflow.
        if self.buf.len() < MAX_COMMAND_SIZE {
            let _ = self.buf.push(byte);
        } else {
            // Buffer full — reset to avoid accumulating garbage.
            self.buf.clear();
        }
        false
    }

    /// Reset the internal buffer without emitting a line.
    pub fn reset(&mut self) {
        self.buf.clear();
    }
}

/// Returns `true` if a USB data byte was received within the last `timeout_ms` milliseconds.
///
/// Uses wrapping subtraction to handle the ~49.7 day u32 wrapping in the
/// scheduler tick counter. If no data has ever been received (timestamp is 0),
/// returns `false`.
pub fn is_usb_alive(timeout_ms: u64) -> bool {
    let now = scheduler::elapsed_ms();
    let last = u64::from(G_LAST_SERIAL_ACTIVITY.load(Ordering::Acquire));
    last != 0 && now.wrapping_sub(last) < timeout_ms
}

/// Force-update the serial activity timestamp.
///
/// # Lint justification
///
/// `cast_possible_truncation`: `u64→u32` wraps at ~49.7 days.
/// `is_usb_alive()` uses wrapping arithmetic
/// `(now.wrapping_sub(last)) < timeout`, which handles wraparound correctly
/// for intervals up to 49.7 days. Lab equipment is rebooted periodically,
/// making this acceptable.
pub fn serial_touch() {
    #[allow(clippy::cast_possible_truncation)]
    G_LAST_SERIAL_ACTIVITY.store(scheduler::elapsed_ms() as u32, Ordering::Release);
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::sync::atomic::Ordering;

    #[test]
    fn test_single_line() {
        let mut reader = SerialReader::new();
        let mut out: Vec<u8, MAX_COMMAND_SIZE> = Vec::new();

        for &b in b"hello\n" {
            let complete = reader.push_byte(b, &mut out);
            if complete {
                assert_eq!(out.as_slice(), b"hello");
                return;
            }
        }
        panic!("should have completed");
    }

    #[test]
    fn test_multiple_lines() {
        let mut reader = SerialReader::new();
        let mut out: Vec<u8, MAX_COMMAND_SIZE> = Vec::new();

        // First line
        for &b in b"line1\n" {
            reader.push_byte(b, &mut out);
        }
        assert_eq!(out.as_slice(), b"line1");

        // Second line
        for &b in b"line2\n" {
            reader.push_byte(b, &mut out);
        }
        assert_eq!(out.as_slice(), b"line2");
    }

    #[test]
    fn test_cr_is_ignored() {
        let mut reader = SerialReader::new();
        let mut out: Vec<u8, MAX_COMMAND_SIZE> = Vec::new();

        for &b in b"hello\r\n" {
            reader.push_byte(b, &mut out);
        }
        assert_eq!(out.as_slice(), b"hello");
    }

    #[test]
    fn test_overflow_resets() {
        let mut reader = SerialReader::new();
        let mut out: Vec<u8, MAX_COMMAND_SIZE> = Vec::new();

        // Push MAX_COMMAND_SIZE bytes without newline → overflow → buffer clears
        for i in 0..MAX_COMMAND_SIZE + 5 {
            let b = (i as u8 & 0x7F).max(32); // printable ASCII
            reader.push_byte(b, &mut out);
        }
        // Buffer should have been cleared on overflow
        // Now push a valid line
        for &b in b"ok\n" {
            reader.push_byte(b, &mut out);
        }
        assert_eq!(out.as_slice(), b"ok");
    }

    #[test]
    fn test_reset() {
        let mut reader = SerialReader::new();
        let mut out: Vec<u8, MAX_COMMAND_SIZE> = Vec::new();

        for &b in b"partial" {
            reader.push_byte(b, &mut out);
        }
        reader.reset();
        // After reset, the partial bytes are gone
        for &b in b"fresh\n" {
            reader.push_byte(b, &mut out);
        }
        assert_eq!(out.as_slice(), b"fresh");
    }

    #[test]
    fn test_serial_silent_default() {
        assert!(!G_SERIAL_SILENT.load(Ordering::Relaxed));
        G_SERIAL_SILENT.store(true, Ordering::Relaxed);
        assert!(G_SERIAL_SILENT.load(Ordering::Relaxed));
        G_SERIAL_SILENT.store(false, Ordering::Relaxed);
    }
}
