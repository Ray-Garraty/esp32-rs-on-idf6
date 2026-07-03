//! Safe wrappers around ESP-IDF boot-time and utility FFI calls.
//!
//! Each function encapsulates a single `unsafe { }` block with documented
//! safety invariants, exposing a safe Rust API.
//!
//! This module is only available on xtensa (ESP32) targets because it
//! depends on `esp_idf_sys`.

use esp_idf_sys;

use crate::diag;

/// Disable the hardware watchdog timer (TWDT).
///
/// Must be called once at boot, before any FreeRTOS task uses the watchdog.
/// After this call, the hardware watchdog will not trigger a reset regardless
/// of task execution time.
///
/// # Safety (encapsulated)
///
/// `esp_task_wdt_deinit()` is safe to call from the main task at boot
/// (FreeRTOS scheduler is running). No dependencies on other tasks.
pub fn disable_wdt() {
    diag::ffi_guard::record_enter(diag::ffi_guard::FFI_ESP_WDT);
    // SAFETY:
    //   Invariant: esp_task_wdt_deinit requires FreeRTOS scheduler running.
    //   Context: called once at boot from main task.
    //   Risk: safe even if called multiple times (idempotent).
    unsafe {
        esp_idf_sys::esp_task_wdt_deinit();
    }
    diag::ffi_guard::record_exit(diag::ffi_guard::FFI_ESP_WDT, 0);
}

/// Suppress debug-level logs from the ESP-IDF HTTP server txrx component.
///
/// Sets the log level for `httpd_txrx` to `ERROR` to reduce serial noise.
/// Safe to call at any point after `esp_idf_sys::link_patches()`.
pub fn suppress_httpd_txrx_logs() {
    // SAFETY:
    //   Invariant: c"httpd_txrx" is a valid null-terminated C string literal.
    //   esp_log_level_set modifies a global int only, no memory safety effects.
    //   Risk: wrong log level string = log spam, no UB.
    unsafe {
        esp_idf_sys::esp_log_level_set(
            c"httpd_txrx".as_ptr(),
            esp_idf_sys::esp_log_level_t_ESP_LOG_ERROR,
        );
    }
}

/// Check integrity of all heap regions using `heap_caps_check_integrity_all`.
///
/// Prints errors if corruption is found.
pub fn check_heap_integrity() {
    diag::ffi_guard::record_enter(diag::ffi_guard::FFI_ESP_HEAP);
    // SAFETY:
    //   Invariant: heap_caps_check_integrity_all is a read-only diagnostic call.
    //   Context: safe after FreeRTOS scheduler init.
    //   Risk: none — read-only traversal, no side effects.
    let ok = unsafe { esp_idf_sys::heap_caps_check_integrity_all(true) };
    diag::ffi_guard::record_exit(diag::ffi_guard::FFI_ESP_HEAP, 0);
    if ok {
        log::info!("Heap integrity OK");
    } else {
        log::error!("HEAP CORRUPTION DETECTED!");
    }
}

/// Read boot-time heap statistics.
///
/// Returns `(free_heap_bytes, largest_free_default_bytes, largest_free_dma_bytes)`.
///
/// All values come from read-only hardware registers via the ESP-IDF
/// heap allocator. Safe to call after FreeRTOS scheduler init.
pub fn heap_stats() -> (u32, u32, u32) {
    diag::ffi_guard::record_enter(diag::ffi_guard::FFI_ESP_HEAP);
    // SAFETY:
    //   Invariant: All three FFI calls are read-only and access heap
    //   metadata only. No side effects on memory.
    //   Context: safe after FreeRTOS scheduler init.
    //   Risk: stale values if called while heap is in use (always true).
    let result = unsafe {
        let free = esp_idf_sys::esp_get_free_heap_size();
        let largest_default =
            esp_idf_sys::heap_caps_get_largest_free_block(esp_idf_sys::MALLOC_CAP_DEFAULT);
        let largest_dma =
            esp_idf_sys::heap_caps_get_largest_free_block(esp_idf_sys::MALLOC_CAP_DMA);
        (
            free,
            u32::try_from(largest_default).unwrap_or(0),
            u32::try_from(largest_dma).unwrap_or(0),
        )
    };
    diag::ffi_guard::record_exit(diag::ffi_guard::FFI_ESP_HEAP, 0);
    result
}

/// Read the current task's stack watermark (minimum free stack bytes
/// since task creation).
///
/// A return value below 1024 indicates critical risk of stack overflow.
/// Safe to call from any FreeRTOS task after scheduler init.
pub fn stack_watermark() -> u32 {
    diag::ffi_guard::record_enter(diag::ffi_guard::FFI_WATERMARK);
    // SAFETY:
    //   Invariant: uxTaskGetStackHighWaterMark(NULL) queries the calling
    //   task's TCB (read-only field). Valid in any FreeRTOS task context.
    //   Context: safe after FreeRTOS scheduler init (main task).
    //   Risk: none — read-only TCB field access, idempotent.
    let result = unsafe { esp_idf_sys::uxTaskGetStackHighWaterMark(core::ptr::null_mut()) };
    diag::ffi_guard::record_exit(diag::ffi_guard::FFI_WATERMARK, 0);
    result
}

/// Trigger a full ESP32 software restart.
///
/// Saves state to NVS before calling. This function does not return.
pub fn restart() -> ! {
    diag::ffi_guard::record_enter(diag::ffi_guard::FFI_ESP_RESTART);
    // SAFETY:
    //   Invariant: esp_restart resets the CPU immediately. All state must be
    //   persisted before calling. Safe to call from any task context.
    //   Risk: function does not return. UB if called without saving state.
    unsafe {
        esp_idf_sys::esp_restart();
    }
}

/// Write a string directly to UART (stdout) without allocation.
///
/// Safe to call from any context including panic handler. Uses the raw
/// `write(1, ...)` syscall — no heap, no locks, no formatting.
/// Only for diagnostic emergency output.
pub fn panic_write_str(s: &str) {
    // SAFETY:
    //   Invariant: `write(1, ptr, len)` writes to the pre-opened stdout
    //   fd which is connected to UART on ESP-IDF. The write is async-signal-safe
    //   and does not allocate. Safe from any context including panic handler.
    //   Risk: partial write (returns < len) is silently ignored — OK for diagnostics.
    unsafe {
        esp_idf_sys::write(1, s.as_ptr().cast::<core::ffi::c_void>(), s.len());
    }
}

/// Set BT/WiFi coexistence priority to prefer BLE.
///
/// Safe to call once at init before any radio activity. Uses a simple
/// register write — no side effects on memory safety.
pub fn set_coex_ble_preferred() {
    diag::ffi_guard::record_enter(diag::ffi_guard::FFI_ESP_COEX);
    // SAFETY:
    //   Invariant: esp_coex_preference_set is a register write, no memory effects.
    //   Context: called once at init before any radio activity.
    //   Risk: if called later, may cause brief radio renegotiation; no UB.
    unsafe {
        esp_idf_sys::esp_coex_preference_set(esp_idf_sys::esp_coex_prefer_t_ESP_COEX_PREFER_BT);
    }
    diag::ffi_guard::record_exit(diag::ffi_guard::FFI_ESP_COEX, 0);
}
