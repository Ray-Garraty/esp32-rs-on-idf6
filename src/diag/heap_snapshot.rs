//! DRAM heap snapshot and fragmentation tracker (GR-3).
//!
//! Captures free/largest DRAM blocks at key init phases and periodically
//! during main loop. Also provides `assert_can_allocate()` which warns
//! if contiguous allocation of N bytes might fail due to fragmentation.

use super::black_box;
use super::black_box::DiagEvent;

/// Take a heap snapshot.
///
/// `phase` is a short string identifying the init phase
/// (e.g., "boot", "wifi_init", "http_started", "ble_init").
/// Uses `esp_safe::heap_stats()` which wraps `esp_get_free_heap_size()` and
/// `heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)`. On ESP32 without
/// PSRAM these are equivalent to MALLOC_CAP_INTERNAL values.
#[allow(clippy::cast_possible_truncation)]
pub fn snapshot(phase: &'static str) {
    let (free, largest, _dma) = crate::esp_safe::heap_stats();

    black_box::record(DiagEvent::HeapSnapshot {
        free_kb: (free / 1024).min(255) as u8,
        largest_kb: (largest / 1024).min(255) as u8,
        phase: phase_id(phase),
    });

    log::info!(
        "[HEAP] {} : free={}KB largest={}KB",
        phase,
        free / 1024,
        largest / 1024,
    );
}

/// Warn if the largest free DRAM block is smaller than `bytes`.
/// Call before allocating a large contiguous buffer.
#[allow(clippy::cast_possible_truncation)]
pub fn assert_can_allocate(bytes: usize, context: &'static str) {
    let (_free, largest, _dma) = crate::esp_safe::heap_stats();
    let largest = largest as usize;
    if largest < bytes {
        black_box::record(DiagEvent::DramFragmented {
            largest_block: (largest / 1024).min(u16::MAX as usize) as u16,
            requested: (bytes / 1024).min(u16::MAX as usize) as u16,
        });
        log::error!(
            "DRAM FRAGMENTATION: {context} needs {}KB but largest block is {}KB",
            bytes / 1024,
            largest / 1024,
        );
    }
}

/// Map a phase string to a u8 ID for storage in DiagEvent.
fn phase_id(phase: &str) -> u8 {
    match phase {
        "boot" => 0,
        "wifi_init" => 1,
        "wifi_ip" => 2,
        "http_started" => 3,
        "ble_init" => 4,
        "main_loop" => 5,
        _ => 0xFF,
    }
}
