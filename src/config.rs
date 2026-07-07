#![forbid(unsafe_code)]
// ── WiFi Access Point ─────────────────────────────────────────
pub const AP_SSID: &str = "esp32-rs-on-idf6";
pub const AP_PASSWORD: &str = "12345678";
pub const AP_CHANNEL: u8 = 1;
pub const AP_MAX_CONNECTIONS: u16 = 4;
pub const AP_IP: [u8; 4] = [192, 168, 4, 1];
pub const AP_NETMASK: [u8; 4] = [255, 255, 255, 0];
pub const AP_NETMASK_BITS: u8 = 24;
pub const AP_IP_ADDRESS: &str = "192.168.4.1";

// ── WiFi Station ──────────────────────────────────────────────
pub const STA_CONNECT_TIMEOUT_MS: u64 = 15_000;
pub const STA_RECONNECT_INTERVAL_MS: u64 = 30_000;
pub const STA_POLL_MS: u64 = 500;
pub const STA_POST_CONNECT_DELAY_MS: u64 = 500;
pub const STA_DHCP_TIMEOUT_MS: u64 = 5000;

// ── GPIO Pin Assignments ──────────────────────────────────────
pub const PIN_STEP: u8 = 21;
pub const PIN_DIR: u8 = 26;
pub const PIN_EN: u8 = 27;
pub const PIN_LIMIT_FULL: u8 = 32;
pub const PIN_LIMIT_EMPTY: u8 = 35;
pub const PIN_ADC: u8 = 4;
pub const PIN_TEMP: u8 = 33;
pub const PIN_LED: u8 = 2;
pub const PIN_VALVE: u8 = 14;

// ── ADC (pH electrode, GPIO4, ADC1_CH3 on S3) ─────────────────
pub const ADC_SAMPLES: u32 = 64;
pub const ADC_ATTENUATION_DB: u8 = 12;

// ── Temperature (DS18B20, GPIO33) ─────────────────────────────
pub const TEMP_READ_INTERVAL_MS: u64 = 1000;
pub const TEMP_CONVERSION_WAIT_MS: u64 = 800;

// ── NTP ───────────────────────────────────────────────────────
pub const NTP_MIN_VALID_TIMESTAMP: i64 = 1_000_000_000;

// ── mDNS ──────────────────────────────────────────────────────
pub const MDNS_HOSTNAME: &str = "esp32-rs-idf6";

// ── HTTP Server ───────────────────────────────────────────────
pub const HTTP_PORT: u16 = 80;

// ── BLE GATT (NUS Service UUIDs) ──────────────────────────────
/// Nordic UART Service UUID (16-bit): 0x1818 — exception: we use a custom 128-bit.
pub const NUS_SERVICE_UUID: &str = "6e400001-b5a3-f393-e0a9-e50e24dc0000";
pub const NUS_RX_UUID: &str = "6e400002-b5a3-f393-e0a9-e50e24dc0000";
pub const NUS_TX_UUID: &str = "6e400003-b5a3-f393-e0a9-e50e24dc0000";

/// BLE advertising name prefix (mac address appended by NimBLE).
pub const BLE_ADV_NAME_PREFIX: &str = "esp32-idf6-";

/// BLE connection parameters (in milliseconds).
pub const BLE_CONN_MIN_INTERVAL_MS: u32 = 30;
pub const BLE_CONN_MAX_INTERVAL_MS: u32 = 50;
pub const BLE_CONN_LATENCY: u16 = 4;
pub const BLE_CONN_SUPERVISION_TIMEOUT_S: u32 = 30;

/// BLE zombie defence: number of consecutive notify failures before
/// disconnecting and restarting advertising.
pub const ZOMBIE_NOTIFY_FAIL_LIMIT: u32 = 5;

// ── Thread Stack Sizes (bytes) ────────────────────────────────
/// Main loop task (FreeRTOS `main`). Set via sdkconfig CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384.
pub const MAIN_TASK_STACK: usize = 16384;
/// Dedicated motor thread (RMT stepper control).
pub const MOTOR_THREAD_STACK: usize = 16384;
/// DS18B20 bitbang temperature thread.
pub const TEMP_THREAD_STACK: usize = 16384;
/// UART reader thread (stdin polling, 8 KB).
///
/// `std::io::stdin().read()` goes through a deep FFI call chain: Rust std →
/// libc → ESP-IDF VFS → UART driver. Was 4 KB — caused pthread stack overflow
/// (LL-008 pattern: watermark was 540 bytes, crash during `log::warn!()` from
/// the `check_watermark` diagnostic routine).
pub const UART_THREAD_STACK: usize = 8192;
/// BLE GATT notify thread (recv + conditionally notify — simple loop).
pub const BLE_NOTIFY_THREAD_STACK: usize = 8192;
/// HTTP server task.
pub const HTTP_SERVER_STACK: usize = 12288;

/// Owner thread for !Send EspHttpServer.
///
/// 16 KB is sufficient with PSRAM enabled — heap allocations go to SPIRAM via
/// CONFIG_SPIRAM_USE_MALLOC, so the stack only needs room for local variables
/// and FFI call frames (EspWifi::new, httpd_start, NimBLE init). The HTTP
/// server task itself has a dedicated 12288-byte stack.
pub const NET_OWNER_STACK: usize = 16384;

// ── RMT ───────────────────────────────────────────────────────
/// RMT resolution in Hz (1 MHz → 1 tick = 1 µs).
pub const RMT_RESOLUTION: u32 = 1_000_000;

/// Maximum number of RMT symbols per transmit chunk.
pub const RMT_CHUNK_MAX: usize = 128;

/// Width of the step pulse in RMT ticks (1 tick = 1 µs at 1 MHz).
pub const RMT_PULSE_WIDTH_TICKS: u16 = 1;

/// Number of steps in the acceleration phase of the trapezoidal ramp.
pub const RAMP_ACCEL_STEPS: u32 = 200;

/// Number of steps in the deceleration phase of the trapezoidal ramp.
pub const RAMP_DECEL_STEPS: u32 = 200;

/// Minimum step frequency in Hz (start/stop speed for ramp computation).
pub const STEPPER_MIN_HZ: u32 = 30;

// ── ADC Default Calibration ───────────────────────────────────
/// Default ADC calibration slope (a = 1.0 means raw = mV).
pub const ADC_DEFAULT_A: f32 = 1.0;
/// Default ADC calibration offset (b = 0.0).
pub const ADC_DEFAULT_B: f32 = 0.0;

// ── Main loop ────────────────────────────────────────────────
/// Main loop pacing tick in milliseconds. Must NOT exceed 10 ms
/// to keep the loop responsive (AGENTS.md golden rule).
pub const MAIN_LOOP_TICK_MS: u64 = 10;

/// Default entry limit for GET /api/logs (matched to legacy JS `?limit=20`).
pub const LOG_DEFAULT_LIMIT: usize = 20;

/// Interval in ticks for limitsw WS event pushes (100 ticks × 10 ms = 1 s).
pub const WS_LIMITSW_INTERVAL_TICKS: u64 = 100;

/// Interval in ticks for periodic log output (100 ticks × 10 ms = 1 s).
pub const LOG_INTERVAL_TICKS: u64 = 100;

// ── Motor task ────────────────────────────────────────────────
/// Homing timeout in milliseconds (120 seconds).
pub const HOMING_TIMEOUT_MS: u64 = 120_000;
/// Maximum steps for homing ramp. Homing only needs to reach the limit
/// switch, not traverse the full burette volume. This caps the `Vec`
/// allocation at ~40 KB (10 000 × 4 bytes) instead of ~250 KB.
pub const HOMING_MAX_STEPS: u32 = 10_000;
/// Command watchdog timeout in milliseconds (60 seconds).
pub const WATCHDOG_CMD_TIMEOUT_MS: u64 = 60_000;
/// USB alive timeout in milliseconds (10 seconds without data → fallback).
pub const USB_ALIVE_TIMEOUT_MS: u64 = 10_000;
/// Motor task idle sleep in milliseconds.
pub const MOTOR_IDLE_SLEEP_MS: u64 = 10;
/// Homing step frequency in Hz.
pub const HOMING_SPEED_HZ: u32 = 1500;
/// Maximum number of pending responses (backpressure limit).
pub const MAX_PENDING_RESPONSES: usize = 4;

#[cfg(test)]
mod tests {
    use super::*;

    /// Regression guard: STA_DHCP_TIMEOUT_MS must be reasonable —
    /// too low (< 1000 ms) would cause DHCP timeout on slow networks,
    /// too high (> 30000 ms) would make init unbearably slow.
    #[test]
    fn sta_dhcp_timeout_is_sane() {
        assert!(
            STA_DHCP_TIMEOUT_MS >= 1000,
            "DHCP timeout too low: got {STA_DHCP_TIMEOUT_MS}"
        );
        assert!(
            STA_DHCP_TIMEOUT_MS <= 30000,
            "DHCP timeout too high: got {STA_DHCP_TIMEOUT_MS}"
        );
    }

    /// Regression guard: ensure DHCP timeout is less than the total connect timeout.
    #[test]
    fn sta_dhcp_timeout_does_not_exceed_connect_timeout() {
        assert!(
            STA_DHCP_TIMEOUT_MS < STA_CONNECT_TIMEOUT_MS,
            "DHCP timeout ({STA_DHCP_TIMEOUT_MS}) must be less than connect timeout ({STA_CONNECT_TIMEOUT_MS})"
        );
    }
}
