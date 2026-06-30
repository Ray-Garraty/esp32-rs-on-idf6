//! Persistent storage (NVS) wrappers.
//!
//! Uses raw `esp_idf_sys::nvs` FFI for calibration data, WiFi credentials,
//! and other persistent configuration.

pub mod nvs;
