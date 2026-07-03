//! NVS (Non-Volatile Storage) manager via raw `esp_idf_sys::nvs` FFI.
//!
//! Provides read/write for `f32` (stored as `u32` via bit-cast),
//! `i64`, `u32`, and strings. Each key-value pair lives in a named namespace.
//!
//! # NVS Layout
//!
//! | Namespace | Key | Type | Description |
//! |---|---|---|---|
//! | `cal` | `steps_per_ml` | f32 (→ u32) | Steps per millilitre |
//! | `cal` | `nominal_vol` | f32 (→ u32) | Nominal burette volume (ml) |
//! | `cal` | `speed_coeff` | f32 (→ u32) | Speed coefficient |
//! | `cal` | `min_freq` | u32 | Minimum step frequency (Hz) |
//! | `cal` | `max_freq` | u32 | Maximum step frequency (Hz) |
//! | `cal` | `cal_date` | i64 | Calibration date (Unix timestamp) |
//! | `adc` | `coeff_a` | f32 (→ u32) | ADC calibration slope |
//! | `adc` | `coeff_b` | f32 (→ u32) | ADC calibration offset |
//! | `wifi` | `ssid` | string | WiFi SSID (max 32 bytes) |
//! | `wifi` | `password` | string | WiFi password (max 64 bytes) |
//! | `stallguard` | `threshold` | u32 | StallGuard threshold (0-255) |
//!
//! # Error Handling
//!
//! - `ESP_ERR_NVS_NOT_FOUND`: return `None` / default value.
//! - `ESP_ERR_NVS_TYPE_MISMATCH`: erase key and re-write with correct type.
//!
//! Matches the NVS layout from `docs/refs/project.md`.
//!
//! # Clippy Notes
//!
//! FFI wrapper code necessarily uses patterns that trigger some clippy lints:
//! - `borrow_as_ptr`: passing `&mut val` to FFI functions that take raw pointers
//!   is idiomatic for ESP-IDF bindings.
//! - `ptr_as_ptr`: casting between raw pointer types is part of FFI.
//! - `items_after_statements`: constants defined mid-function for clarity.

#![allow(
    clippy::borrow_as_ptr,
    clippy::ptr_as_ptr,
    clippy::items_after_statements
)]

use core::ffi::c_char;

use esp_idf_svc::nvs::EspDefaultNvsPartition;
use esp_idf_sys::{
    nvs_close, nvs_commit, nvs_erase_key, nvs_get_i64, nvs_get_str, nvs_get_u32, nvs_handle_t,
    nvs_open, nvs_set_i64, nvs_set_str, nvs_set_u32,
};

use crate::diag;
use crate::errors::ResourceError;

/// NVS handle type alias.
type NvsHandle = nvs_handle_t;

/// NVS manager wrapping raw ESP-IDF FFI.
///
/// Each instance manages one open namespace. Drop calls `nvs_close()`.
pub struct NvsManager {
    handle: NvsHandle,
}

impl NvsManager {
    /// Open an NVS namespace.
    ///
    /// - `namespace`: null-terminated string (e.g., `c"cal"`).
    /// - `readwrite`: if `true`, open with `NVS_READWRITE`; else `NVS_READONLY`.
    ///
    /// # Errors
    ///
    /// Returns `ResourceError::NvsOpenFailed` if the namespace cannot be opened.
    ///
    /// # Safety
    ///
    /// `nvs_open` is an FFI call. The namespace pointer must be a valid
    /// null-terminated C string.
    pub fn open(namespace: &str, readwrite: bool) -> Result<Self, ResourceError> {
        let c_namespace =
            std::ffi::CString::new(namespace).map_err(|_| ResourceError::NvsOpenFailed)?;

        let open_mode = if readwrite {
            esp_idf_sys::nvs_open_mode_t_NVS_READWRITE
        } else {
            esp_idf_sys::nvs_open_mode_t_NVS_READONLY
        };

        let mut handle: NvsHandle = 0;

        diag::ffi_guard::record_enter(diag::ffi_guard::FFI_NVS_OPEN);
        // SAFETY: nvs_open is a standard ESP-IDF FFI call. The namespace
        // pointer is valid, null-terminated. The handle output pointer is
        // aligned and non-null. We check the return code to ensure the
        // handle is valid.
        let ret = unsafe { nvs_open(c_namespace.as_ptr(), open_mode, &mut handle) };
        diag::ffi_guard::record_exit(diag::ffi_guard::FFI_NVS_OPEN, if ret == 0 { 0 } else { -1 });

        if ret != esp_idf_sys::ESP_OK {
            return Err(ResourceError::NvsOpenFailed);
        }

        Ok(Self { handle })
    }

    /// Read a `f32` value stored as `u32` via bit-cast.
    ///
    /// Returns `None` if the key is not found (`ESP_ERR_NVS_NOT_FOUND`).
    /// On `ESP_ERR_NVS_TYPE_MISMATCH`, erases the key and re-writes with `u32` type.
    ///
    /// # Errors
    ///
    /// Returns `ResourceError::NvsOpenFailed` on unexpected NVS errors.
    pub fn read_f32(&self, key: &str) -> Result<Option<f32>, ResourceError> {
        let c_key = std::ffi::CString::new(key).map_err(|_| ResourceError::NvsOpenFailed)?;
        let mut val: u32 = 0;

        diag::ffi_guard::record_enter(diag::ffi_guard::FFI_NVS_GET_U32);
        // SAFETY: Standard NVS FFI call with valid handle and key.
        let ret = unsafe { nvs_get_u32(self.handle, c_key.as_ptr(), &mut val) };
        diag::ffi_guard::record_exit(
            diag::ffi_guard::FFI_NVS_GET_U32,
            if ret == 0 { 0 } else { -1 },
        );

        match ret {
            esp_idf_sys::ESP_OK => {
                let f = f32::from_bits(val);
                if f.is_finite() {
                    Ok(Some(f))
                } else {
                    // Treat NaN/inf as "not found" — corrupted data
                    Ok(None)
                }
            }
            esp_idf_sys::ESP_ERR_NVS_NOT_FOUND => Ok(None),
            esp_idf_sys::ESP_ERR_NVS_TYPE_MISMATCH => {
                // Key exists with wrong type — erase it
                self.erase(key)?;
                Ok(None)
            }
            _ => Err(ResourceError::NvsOpenFailed),
        }
    }

    /// Write a `f32` value stored as `u32` via `f32::to_bits()`.
    ///
    /// Automatically calls `commit()`.
    ///
    /// # Errors
    ///
    /// Returns `ResourceError::NvsOpenFailed` on write failure.
    pub fn write_f32(&self, key: &str, value: f32) -> Result<(), ResourceError> {
        let c_key = std::ffi::CString::new(key).map_err(|_| ResourceError::NvsOpenFailed)?;
        let bits = value.to_bits();

        diag::ffi_guard::record_enter(diag::ffi_guard::FFI_NVS_SET_U32);
        // SAFETY: Standard NVS FFI call.
        let ret = unsafe { nvs_set_u32(self.handle, c_key.as_ptr(), bits) };
        diag::ffi_guard::record_exit(
            diag::ffi_guard::FFI_NVS_SET_U32,
            if ret == 0 { 0 } else { -1 },
        );
        if ret != esp_idf_sys::ESP_OK {
            return Err(ResourceError::NvsOpenFailed);
        }
        self.commit()
    }

    /// Read an `i64` value from NVS.
    ///
    /// Returns `None` if the key is not found.
    ///
    /// # Errors
    ///
    /// Returns `ResourceError::NvsOpenFailed` on unexpected NVS errors.
    pub fn read_i64(&self, key: &str) -> Result<Option<i64>, ResourceError> {
        let c_key = std::ffi::CString::new(key).map_err(|_| ResourceError::NvsOpenFailed)?;
        let mut val: i64 = 0;

        diag::ffi_guard::record_enter(diag::ffi_guard::FFI_NVS_GET_I64);
        // SAFETY: Standard NVS FFI call with valid handle and key.
        let ret = unsafe { nvs_get_i64(self.handle, c_key.as_ptr(), &mut val) };
        diag::ffi_guard::record_exit(
            diag::ffi_guard::FFI_NVS_GET_I64,
            if ret == 0 { 0 } else { -1 },
        );

        match ret {
            esp_idf_sys::ESP_OK => Ok(Some(val)),
            esp_idf_sys::ESP_ERR_NVS_NOT_FOUND => Ok(None),
            _ => Err(ResourceError::NvsOpenFailed),
        }
    }

    /// Write an `i64` value to NVS.
    ///
    /// Automatically calls `commit()`.
    ///
    /// # Errors
    ///
    /// Returns `ResourceError::NvsOpenFailed` on write failure.
    pub fn write_i64(&self, key: &str, value: i64) -> Result<(), ResourceError> {
        let c_key = std::ffi::CString::new(key).map_err(|_| ResourceError::NvsOpenFailed)?;

        diag::ffi_guard::record_enter(diag::ffi_guard::FFI_NVS_SET_I64);
        // SAFETY: Standard NVS FFI call.
        let ret = unsafe { nvs_set_i64(self.handle, c_key.as_ptr(), value) };
        diag::ffi_guard::record_exit(
            diag::ffi_guard::FFI_NVS_SET_I64,
            if ret == 0 { 0 } else { -1 },
        );
        if ret != esp_idf_sys::ESP_OK {
            return Err(ResourceError::NvsOpenFailed);
        }
        self.commit()
    }

    /// Read a `u32` value from NVS.
    ///
    /// Returns `None` if the key is not found.
    ///
    /// # Errors
    ///
    /// Returns `ResourceError::NvsOpenFailed` on unexpected NVS errors.
    pub fn read_u32(&self, key: &str) -> Result<Option<u32>, ResourceError> {
        let c_key = std::ffi::CString::new(key).map_err(|_| ResourceError::NvsOpenFailed)?;
        let mut val: u32 = 0;

        diag::ffi_guard::record_enter(diag::ffi_guard::FFI_NVS_GET_U32);
        // SAFETY: Standard NVS FFI call.
        let ret = unsafe { nvs_get_u32(self.handle, c_key.as_ptr(), &mut val) };
        diag::ffi_guard::record_exit(
            diag::ffi_guard::FFI_NVS_GET_U32,
            if ret == 0 { 0 } else { -1 },
        );

        match ret {
            esp_idf_sys::ESP_OK => Ok(Some(val)),
            esp_idf_sys::ESP_ERR_NVS_NOT_FOUND => Ok(None),
            _ => Err(ResourceError::NvsOpenFailed),
        }
    }

    /// Write a `u32` value to NVS.
    ///
    /// Automatically calls `commit()`.
    ///
    /// # Errors
    ///
    /// Returns `ResourceError::NvsOpenFailed` on write failure.
    pub fn write_u32(&self, key: &str, value: u32) -> Result<(), ResourceError> {
        let c_key = std::ffi::CString::new(key).map_err(|_| ResourceError::NvsOpenFailed)?;

        diag::ffi_guard::record_enter(diag::ffi_guard::FFI_NVS_SET_U32);
        // SAFETY: Standard NVS FFI call.
        let ret = unsafe { nvs_set_u32(self.handle, c_key.as_ptr(), value) };
        diag::ffi_guard::record_exit(
            diag::ffi_guard::FFI_NVS_SET_U32,
            if ret == 0 { 0 } else { -1 },
        );
        if ret != esp_idf_sys::ESP_OK {
            return Err(ResourceError::NvsOpenFailed);
        }
        self.commit()
    }

    /// Read a string from NVS into a `heapless::String<{N}>`.
    ///
    /// Returns `None` if the key is not found.
    ///
    /// # Errors
    ///
    /// Returns `ResourceError::NvsOpenFailed` on unexpected NVS errors
    /// or if the string exceeds the buffer capacity.
    pub fn read_str<const N: usize>(
        &self,
        key: &str,
    ) -> Result<Option<heapless::String<N>>, ResourceError> {
        let c_key = std::ffi::CString::new(key).map_err(|_| ResourceError::NvsOpenFailed)?;

        // First call with null buffer to get required length
        let mut required_len: usize = 0;

        // Maximum supported string length (including null terminator).
        // NVS strings for WiFi credentials are at most 64 bytes.
        const MAX_STR_LEN: usize = 256;

        diag::ffi_guard::record_enter(diag::ffi_guard::FFI_NVS_GET_STR);
        // SAFETY: Passing null pointer for buffer to query required length.
        let ret = unsafe {
            nvs_get_str(
                self.handle,
                c_key.as_ptr(),
                core::ptr::null_mut(),
                &mut required_len,
            )
        };
        diag::ffi_guard::record_exit(
            diag::ffi_guard::FFI_NVS_GET_STR,
            if ret == 0 || ret == esp_idf_sys::ESP_ERR_NVS_INVALID_LENGTH {
                0
            } else {
                -1
            },
        );

        if ret == esp_idf_sys::ESP_ERR_NVS_NOT_FOUND {
            return Ok(None);
        }
        if ret != esp_idf_sys::ESP_OK && ret != esp_idf_sys::ESP_ERR_NVS_INVALID_LENGTH {
            return Err(ResourceError::NvsOpenFailed);
        }
        if required_len == 0 {
            return Err(ResourceError::NvsOpenFailed);
        }

        if required_len > MAX_STR_LEN || required_len > N + 1 {
            return Err(ResourceError::NvsOpenFailed);
        }

        let mut buf = [0u8; MAX_STR_LEN];

        diag::ffi_guard::record_enter(diag::ffi_guard::FFI_NVS_GET_STR);
        // SAFETY: The buffer is sized to `MAX_STR_LEN` bytes, which is
        // larger than `required_len` (checked above), so the NVS read
        // will not overflow. The buffer pointer is valid and aligned.
        let ret = unsafe {
            nvs_get_str(
                self.handle,
                c_key.as_ptr(),
                buf.as_mut_ptr().cast::<c_char>(),
                &mut required_len,
            )
        };
        diag::ffi_guard::record_exit(
            diag::ffi_guard::FFI_NVS_GET_STR,
            if ret == 0 { 0 } else { -1 },
        );

        if ret != esp_idf_sys::ESP_OK {
            return Err(ResourceError::NvsOpenFailed);
        }

        // Find the null terminator and copy up to it
        let actual_len = buf
            .iter()
            .position(|&b| b == 0)
            .unwrap_or_else(|| required_len.saturating_sub(1));
        let slice = &buf[..actual_len.min(N)];

        // Convert bytes to heapless::String via from_utf8.
        let hv: heapless::Vec<u8, N> =
            heapless::Vec::from_slice(slice).map_err(|_| ResourceError::NvsOpenFailed)?;
        let s = heapless::String::from_utf8(hv).map_err(|_| ResourceError::NvsOpenFailed)?;

        Ok(Some(s))
    }

    /// Write a string to NVS.
    ///
    /// Automatically calls `commit()`.
    ///
    /// # Errors
    ///
    /// Returns `ResourceError::NvsOpenFailed` on write failure.
    pub fn write_str(&self, key: &str, value: &str) -> Result<(), ResourceError> {
        let c_key = std::ffi::CString::new(key).map_err(|_| ResourceError::NvsOpenFailed)?;
        let c_value = std::ffi::CString::new(value).map_err(|_| ResourceError::NvsOpenFailed)?;

        diag::ffi_guard::record_enter(diag::ffi_guard::FFI_NVS_SET_STR);
        // SAFETY: Standard NVS FFI call.
        let ret = unsafe { nvs_set_str(self.handle, c_key.as_ptr(), c_value.as_ptr()) };
        diag::ffi_guard::record_exit(
            diag::ffi_guard::FFI_NVS_SET_STR,
            if ret == 0 { 0 } else { -1 },
        );
        if ret != esp_idf_sys::ESP_OK {
            return Err(ResourceError::NvsOpenFailed);
        }
        self.commit()
    }

    /// Erase a key from NVS.
    ///
    /// # Errors
    ///
    /// Returns `ResourceError::NvsOpenFailed` if the key cannot be erased.
    pub fn erase(&self, key: &str) -> Result<(), ResourceError> {
        let c_key = std::ffi::CString::new(key).map_err(|_| ResourceError::NvsOpenFailed)?;

        diag::ffi_guard::record_enter(diag::ffi_guard::FFI_NVS_ERASE);
        // SAFETY: Standard NVS FFI call.
        let ret = unsafe { nvs_erase_key(self.handle, c_key.as_ptr()) };
        diag::ffi_guard::record_exit(
            diag::ffi_guard::FFI_NVS_ERASE,
            if ret == 0 { 0 } else { -1 },
        );
        if ret != esp_idf_sys::ESP_OK {
            return Err(ResourceError::NvsOpenFailed);
        }
        self.commit()
    }

    /// Commit pending NVS writes.
    ///
    /// # Errors
    ///
    /// Returns `ResourceError::NvsOpenFailed` on commit failure.
    fn commit(&self) -> Result<(), ResourceError> {
        diag::ffi_guard::record_enter(diag::ffi_guard::FFI_NVS_COMMIT);
        // SAFETY: Standard NVS FFI call with valid handle.
        let ret = unsafe { nvs_commit(self.handle) };
        diag::ffi_guard::record_exit(
            diag::ffi_guard::FFI_NVS_COMMIT,
            if ret == 0 { 0 } else { -1 },
        );
        if ret != esp_idf_sys::ESP_OK {
            return Err(ResourceError::NvsOpenFailed);
        }
        Ok(())
    }
}

impl Drop for NvsManager {
    fn drop(&mut self) {
        diag::ffi_guard::record_enter(diag::ffi_guard::FFI_NVS_CLOSE);
        // SAFETY: nvs_close is safe to call with a valid handle.
        // The handle is guaranteed valid because we only create NvsManager
        // via `open()` which returns Ok only if nvs_open succeeded.
        unsafe {
            nvs_close(self.handle);
        }
        diag::ffi_guard::record_exit(diag::ffi_guard::FFI_NVS_CLOSE, 0);
    }
}

// ── Namespace convenience functions ──────────────────────────────

const NS_STALLGUARD: &str = "stallguard";
const KEY_SG_THRESHOLD: &str = "threshold";
const DEFAULT_SG_THRESHOLD: u32 = 0;

/// Read the StallGuard threshold from NVS.
///
/// Returns `DEFAULT_SG_THRESHOLD` (0) if not set.
pub fn stallguard_read_threshold() -> u8 {
    // Threshold is always 0-255, safe to truncate from u32
    #[allow(clippy::cast_possible_truncation)]
    let threshold = NvsManager::open(NS_STALLGUARD, false)
        .ok()
        .and_then(|mgr| mgr.read_u32(KEY_SG_THRESHOLD).ok().flatten())
        .unwrap_or(DEFAULT_SG_THRESHOLD) as u8;
    threshold
}

/// Write the StallGuard threshold to NVS.
pub fn stallguard_write_threshold(value: u8) -> Result<(), ResourceError> {
    let mgr = NvsManager::open(NS_STALLGUARD, true)?;
    mgr.write_u32(KEY_SG_THRESHOLD, u32::from(value))
}

/// Initialize the default NVS partition exactly once at boot.
///
/// Returns a clonable handle. After this call, `NvsManager::open()` (which uses
/// raw FFI `nvs_open()`) is safe to use because `nvs_flash_init()` was called.
///
/// Must be called before any `NvsManager::open()` or `EspWifi::new()` call.
///
/// # Errors
///
/// Returns `ResourceError::NvsOpenFailed` if the NVS partition cannot be initialized.
pub fn nvs_init() -> Result<EspDefaultNvsPartition, ResourceError> {
    EspDefaultNvsPartition::take().map_err(|_| ResourceError::NvsOpenFailed)
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
