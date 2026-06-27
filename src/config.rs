pub const AP_SSID: &str = "EcoTiter-AP";
pub const AP_PASSWORD: &str = "12345678";
pub const AP_CHANNEL: u8 = 1;
pub const AP_MAX_CONNECTIONS: u16 = 4;
pub const AP_IP: [u8; 4] = [192, 168, 4, 1];
pub const AP_NETMASK: [u8; 4] = [255, 255, 255, 0];
pub const AP_NETMASK_BITS: u8 = 24;
pub const AP_IP_ADDRESS: &str = "192.168.4.1";

pub const STA_CONNECT_TIMEOUT_MS: u32 = 15_000;
pub const STA_RECONNECT_INTERVAL_MS: u32 = 30_000;
pub const STA_POLL_MS: u32 = 500;
pub const STA_POST_CONNECT_DELAY_MS: u32 = 500;

pub const NTP_MIN_VALID_TIMESTAMP: i64 = 1_000_000_000;
pub const MDNS_HOSTNAME: &str = "ecotiter";
pub const HTTP_PORT: u16 = 80;
