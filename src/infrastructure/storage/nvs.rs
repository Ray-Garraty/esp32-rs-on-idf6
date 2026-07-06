//! NVS (Non-Volatile Storage) via `esp_idf_svc::nvs::EspNvs`.
//!
//! Provides safe access to NVS without raw FFI calls. Uses `EspNvs` from
//! esp-idf-svc which internally wraps the raw `nvs_*` FFI calls.
//!
//! # NVS Layout
//!
//! | Namespace | Key | Type | Description |
//! |---|---|---|---|
//! | `stallguard` | `threshold` | u32 | StallGuard threshold (0-255) |
//!
//! WiFi credentials (namespace `wifi`, keys `ssid`/`password`) are handled
//! directly by `WifiManager` via `EspNvs`. Calibration data uses in-memory
//! `CalibrationConfig` and is not stored in NVS in the current version.
//!
//! Matches the NVS layout from `docs/refs/project.md`.

use std::sync::OnceLock;

use esp_idf_svc::nvs::{EspDefaultNvsPartition, EspNvs, EspDefaultNvs};

use crate::errors::ResourceError;

static NVS_PARTITION: OnceLock<EspDefaultNvsPartition> = OnceLock::new();

/// Initialize the default NVS partition exactly once at boot.
///
/// Returns a clonable handle. Stores a clone globally for later use by
/// `stallguard_read_threshold()` / `stallguard_write_threshold()` and
/// WiFi credential helpers.
///
/// Must be called before any `EspNvs::new()` call.
///
/// # Errors
///
/// Returns `ResourceError::NvsOpenFailed` if the NVS partition cannot be initialized.
pub fn nvs_init() -> Result<EspDefaultNvsPartition, ResourceError> {
    let partition = EspDefaultNvsPartition::take().map_err(|_| ResourceError::NvsOpenFailed)?;
    let _ = NVS_PARTITION.set(partition.clone());
    Ok(partition)
}

/// Get a clone of the stored NVS partition for opening namespaces.
fn get_partition() -> Option<EspDefaultNvsPartition> {
    NVS_PARTITION.get().cloned()
}

// ── StallGuard threshold ────────────────────────────────────────

const NS_STALLGUARD: &str = "stallguard";
const KEY_SG_THRESHOLD: &str = "threshold";
const DEFAULT_SG_THRESHOLD: u32 = 0;

/// Open the stallguard namespace with EspNvs.
fn open_stallguard(readwrite: bool) -> Result<EspDefaultNvs, ResourceError> {
    let partition = get_partition().ok_or(ResourceError::NvsOpenFailed)?;
    EspNvs::new(partition, NS_STALLGUARD, readwrite).map_err(|_| ResourceError::NvsOpenFailed)
}

/// Read the StallGuard threshold from NVS.
///
/// Returns `DEFAULT_SG_THRESHOLD` (0) if not set.
pub fn stallguard_read_threshold() -> u8 {
    #[allow(clippy::cast_possible_truncation)]
    let threshold = open_stallguard(false)
        .ok()
        .and_then(|nvs| nvs.get_u32(KEY_SG_THRESHOLD).ok().flatten())
        .unwrap_or(DEFAULT_SG_THRESHOLD) as u8;
    threshold
}

/// Write the StallGuard threshold to NVS.
pub fn stallguard_write_threshold(value: u8) -> Result<(), ResourceError> {
    let nvs = open_stallguard(true)?;
    nvs.set_u32(KEY_SG_THRESHOLD, u32::from(value))
        .map_err(|_| ResourceError::NvsOpenFailed)
}

// ── WiFi credential helpers (used by WifiManager) ───────────────

const NS_WIFI: &str = "wifi";
/// Open the wifi namespace with EspNvs.
fn open_wifi(readwrite: bool) -> Result<EspDefaultNvs, ResourceError> {
    let partition = get_partition().ok_or(ResourceError::NvsOpenFailed)?;
    EspNvs::new(partition, NS_WIFI, readwrite).map_err(|_| ResourceError::NvsOpenFailed)
}

/// Read a string value from the wifi namespace into a heapless::String<N>.
pub fn wifi_read_str<const N: usize>(key: &str) -> Result<Option<heapless::String<N>>, ResourceError> {
    let nvs = open_wifi(true)?;
    let mut buf = [0u8; 128];
    let val = nvs.get_str(key, &mut buf).map_err(|_| ResourceError::NvsOpenFailed)?;
    match val {
        Some(s) => {
            let mut result: heapless::String<N> = heapless::String::new();
            result.push_str(s).map_err(|_| ResourceError::NvsOpenFailed)?;
            Ok(Some(result))
        }
        None => Ok(None),
    }
}

/// Write a string value to the wifi namespace.
pub fn wifi_write_str(key: &str, value: &str) -> Result<(), ResourceError> {
    let nvs = open_wifi(true)?;
    nvs.set_str(key, value).map_err(|_| ResourceError::NvsOpenFailed)
}

/// Erase a key from the wifi namespace.
pub fn wifi_erase(key: &str) -> Result<(), ResourceError> {
    let nvs = open_wifi(true)?;
    nvs.remove(key).map_err(|_| ResourceError::NvsOpenFailed)?;
    Ok(())
}

// ── Tests ──────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    /// f32 ↔ u32 bit-cast round-trip via to_bits / from_bits.
    #[test]
    fn test_f32_bitcast_roundtrip() {
        let values = [0.0_f32, 1.0, -1.0, 3.14159, 7730.0, 0.03052, -55.0, 125.0];
        for &v in &values {
            let bits = v.to_bits();
            let recovered = f32::from_bits(bits);
            assert!(
                (v - recovered).abs() < f32::EPSILON,
                "bitcast failed for {v}"
            );
        }
    }

    /// Non-finite values (NaN) should be detectable.
    #[test]
    fn test_f32_non_finite_detection() {
        let nan = f32::NAN;
        assert!(!nan.is_finite());
        let inf = f32::INFINITY;
        assert!(!inf.is_finite());
    }

    /// heapless::String creation from byte slice.
    #[test]
    fn test_heapless_string_from_slice() {
        let bytes = b"hello";
        let v: heapless::Vec<u8, 32> = heapless::Vec::from_slice(bytes).unwrap();
        let s: heapless::String<32> = heapless::String::from_utf8(v).unwrap();
        assert_eq!(s.as_str(), "hello");
    }
}
