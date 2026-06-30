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
pub const STA_CONNECT_TIMEOUT_MS: u32 = 15_000;
pub const STA_RECONNECT_INTERVAL_MS: u32 = 30_000;
pub const STA_POLL_MS: u32 = 500;
pub const STA_POST_CONNECT_DELAY_MS: u32 = 500;

// ── GPIO Pin Assignments ──────────────────────────────────────
pub const PIN_STEP: u8 = 25;
pub const PIN_DIR: u8 = 26;
pub const PIN_EN: u8 = 27;
pub const PIN_LIMIT_FULL: u8 = 32;
pub const PIN_LIMIT_EMPTY: u8 = 35;
pub const PIN_ADC: u8 = 34;
pub const PIN_TEMP: u8 = 33;
pub const PIN_LED: u8 = 2;
pub const PIN_VALVE_OPEN: u8 = 12;
pub const PIN_VALVE_CLOSE: u8 = 13;

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

// ── RMT ───────────────────────────────────────────────────────
/// RMT resolution in Hz (1 MHz → 1 tick = 1 µs).
pub const RMT_RESOLUTION: u32 = 1_000_000;

// ── Main loop ────────────────────────────────────────────────
/// Main loop pacing tick in milliseconds. Must NOT exceed 10 ms
/// to keep the loop responsive (AGENTS.md golden rule).
pub const MAIN_LOOP_TICK_MS: u64 = 10;
