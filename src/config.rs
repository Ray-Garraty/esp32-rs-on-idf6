#![forbid(unsafe_code)]
// ── WiFi Access Point ─────────────────────────────────────────
pub const AP_SSID: &str = "EcoTiter-AP";
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

// ── GPIO Pin Assignments ──────────────────────────────────────
pub const PIN_STEP: u8 = 25;
pub const PIN_DIR: u8 = 26;
pub const PIN_EN: u8 = 27;
pub const PIN_LIMIT_FULL: u8 = 32;
pub const PIN_LIMIT_EMPTY: u8 = 35;
pub const PIN_ADC: u8 = 34;
pub const PIN_TEMP: u8 = 33;
pub const PIN_LED: u8 = 2;
pub const PIN_VALVE: u8 = 14;

// ── ADC (pH electrode, GPIO34, ADC1_CH6) ──────────────────────
pub const ADC_SAMPLES: u32 = 64;
pub const ADC_ATTENUATION_DB: u8 = 12;

// ── Temperature (DS18B20, GPIO33) ─────────────────────────────
pub const TEMP_READ_INTERVAL_MS: u64 = 1000;
pub const TEMP_CONVERSION_WAIT_MS: u64 = 800;

// ── NTP ───────────────────────────────────────────────────────
pub const NTP_MIN_VALID_TIMESTAMP: i64 = 1_000_000_000;

// ── mDNS ──────────────────────────────────────────────────────
pub const MDNS_HOSTNAME: &str = "ecotiter";

// ── HTTP Server ───────────────────────────────────────────────
pub const HTTP_PORT: u16 = 80;

// ── BLE GATT (NUS Service UUIDs) ──────────────────────────────
/// Nordic UART Service UUID (16-bit): 0x1818 — exception: we use a custom 128-bit.
pub const NUS_SERVICE_UUID: &str = "6e400001-b5a3-f393-e0a9-e50e24dc0000";
pub const NUS_RX_UUID: &str = "6e400002-b5a3-f393-e0a9-e50e24dc0000";
pub const NUS_TX_UUID: &str = "6e400003-b5a3-f393-e0a9-e50e24dc0000";

/// BLE advertising name prefix (mac address appended by NimBLE).
pub const BLE_ADV_NAME_PREFIX: &str = "EcoTiter-";

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
pub const MOTOR_THREAD_STACK: usize = 4096;
/// DS18B20 bitbang temperature thread.
pub const TEMP_THREAD_STACK: usize = 16384;
/// BLE GATT notify thread.
pub const BLE_NOTIFY_THREAD_STACK: usize = 8192;
/// HTTP server task.
pub const HTTP_SERVER_STACK: usize = 12288;

/// Owner thread for !Send EspHttpServer.
///
/// 32 KB guarantees safe C-struct init (URI matchers, httpd_start). Holds the
/// server via RAII, never exits. (Was 16 KB in main — insufficient, caused
/// SW_CPU_RESET during Debug format. Now isolated to dedicated 32 KB stack.)
pub const NET_OWNER_STACK: usize = 32768;

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
