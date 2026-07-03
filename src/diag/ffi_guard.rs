//! FFI boundary tracing macros and helpers (GR-5).
//!
//! Provides `ffi_enter!` / `ffi_exit!` macros that record FFI boundary
//! crossings in the black box and emit trace-level logs.
//!
//! # Usage
//!
//! ```ignore
//! ffi_guard::record_enter(FFI_WS_SEND);
//! let ret = unsafe { httpd_ws_send_frame_async(...) };
//! ffi_guard::record_exit(FFI_WS_SEND, if ret == 0 { 0 } else { -1 });
//! ```

use super::black_box;
use super::black_box::DiagEvent;

// ── Named FFI boundary IDs ──────────────────────────────────────
pub const FFI_WS_SEND: u8 = 1;
pub const FFI_NVS_OPEN: u8 = 2;
pub const FFI_NVS_GET_U32: u8 = 3;
pub const FFI_NVS_GET_I64: u8 = 4;
pub const FFI_NVS_GET_STR: u8 = 5;
pub const FFI_NVS_SET_U32: u8 = 6;
pub const FFI_NVS_SET_I64: u8 = 7;
pub const FFI_NVS_SET_STR: u8 = 8;
pub const FFI_NVS_ERASE: u8 = 9;
pub const FFI_NVS_COMMIT: u8 = 10;
pub const FFI_NVS_CLOSE: u8 = 11;
pub const FFI_MUTEX_LOCK: u8 = 12;
pub const FFI_MUTEX_TRYLOCK: u8 = 13;
pub const FFI_MUTEX_UNLOCK: u8 = 14;
pub const FFI_ESP_TIMER: u8 = 15;
pub const FFI_ESP_WDT: u8 = 16;
pub const FFI_ESP_HEAP: u8 = 17;
pub const FFI_ESP_RESTART: u8 = 18;
pub const FFI_ESP_COEX: u8 = 19;
pub const FFI_WATERMARK: u8 = 20;

/// Record an FFI entry event.
#[inline]
pub fn record_enter(boundary: u8) {
    black_box::record(DiagEvent::FfiEnter { boundary });
}

/// Record an FFI exit event with result code (0 = success, -1 = error).
#[inline]
pub fn record_exit(boundary: u8, result: i8) {
    black_box::record(DiagEvent::FfiExit { boundary, result });
}
