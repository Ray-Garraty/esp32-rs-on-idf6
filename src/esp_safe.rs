//! Safe wrappers around ESP-IDF boot-time and utility FFI calls.
//!
//! Each function encapsulates a single `unsafe { }` block with documented
//! safety invariants, exposing a safe Rust API.
//!
//! This module is only available on xtensa (ESP32) targets because it
//! depends on `esp_idf_sys`.

use esp_idf_sys;

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
    // SAFETY:
    //   Invariant: esp_task_wdt_deinit requires FreeRTOS scheduler running.
    //   Context: called once at boot from main task.
    //   Risk: safe even if called multiple times (idempotent).
    unsafe {
        esp_idf_sys::esp_task_wdt_deinit();
    }
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
    // SAFETY:
    //   Invariant: heap_caps_check_integrity_all is a read-only diagnostic call.
    //   Context: safe after FreeRTOS scheduler init.
    //   Risk: none — read-only traversal, no side effects.
    let ok = unsafe { esp_idf_sys::heap_caps_check_integrity_all(true) };
    if ok {
        log::info!("Heap integrity OK");
    } else {
        log::error!("HEAP CORRUPTION DETECTED!");
    }
}

/// Read boot-time heap statistics.
///
/// Returns `(free_heap_bytes, largest_free_block_bytes)`.
///
/// Both values come from read-only hardware registers via the ESP-IDF
/// heap allocator. Safe to call after FreeRTOS scheduler init.
pub fn heap_stats() -> (u32, u32) {
    // SAFETY:
    //   Invariant: esp_get_free_heap_size and heap_caps_get_largest_free_block
    //   are read-only FFI calls that access hardware registers only.
    //   Context: safe after FreeRTOS scheduler init.
    //   Risk: stale values if called while heap is in use (always true).
    unsafe {
        let free = esp_idf_sys::esp_get_free_heap_size();
        let largest =
            esp_idf_sys::heap_caps_get_largest_free_block(esp_idf_sys::MALLOC_CAP_DEFAULT);
        (free, u32::try_from(largest).unwrap_or(0))
    }
}

/// Read the current task's stack watermark (minimum free stack bytes
/// since task creation).
///
/// A return value below 1024 indicates critical risk of stack overflow.
/// Safe to call from any FreeRTOS task after scheduler init.
pub fn stack_watermark() -> u32 {
    // SAFETY:
    //   Invariant: uxTaskGetStackHighWaterMark(NULL) queries the calling
    //   task's TCB (read-only field). Valid in any FreeRTOS task context.
    //   Context: safe after FreeRTOS scheduler init (main task).
    //   Risk: none — read-only TCB field access, idempotent.
    unsafe { esp_idf_sys::uxTaskGetStackHighWaterMark(core::ptr::null_mut()) }
}

/// Trigger a full ESP32 software restart.
///
/// Saves state to NVS before calling. This function does not return.
pub fn restart() -> ! {
    // SAFETY:
    //   Invariant: esp_restart resets the CPU immediately. All state must be
    //   persisted before calling. Safe to call from any task context.
    //   Risk: function does not return. UB if called without saving state.
    unsafe {
        esp_idf_sys::esp_restart();
    }
}

/// Set BT/WiFi coexistence priority to prefer BLE.
///
/// Safe to call once at init before any radio activity. Uses a simple
/// register write — no side effects on memory safety.
pub fn set_coex_ble_preferred() {
    // SAFETY:
    //   Invariant: esp_coex_preference_set is a register write, no memory effects.
    //   Context: called once at init before any radio activity.
    //   Risk: if called later, may cause brief radio renegotiation; no UB.
    unsafe {
        esp_idf_sys::esp_coex_preference_set(esp_idf_sys::esp_coex_prefer_t_ESP_COEX_PREFER_BT);
    }
}
