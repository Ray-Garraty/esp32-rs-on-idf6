//! WiFi manager — AP mode (captive portal + DNS) and STA mode (NVS-backed credentials).
//!
//! # Architecture
//!
//! - `new()` + `init()`: blocking, called once at boot.
//! - `process()`: non-blocking, called from main loop every tick (~10 ms).
//! - All STA methods are blocking — only called from init or HTTP handler task.
//! - DNS responder runs from `process()` — non-blocking UDP poll.
//!
//! # Error Handling
//!
//! WiFi init errors map to [`NetworkError::WifiConnectionFailed`] via the
//! `From<EspError>` impl. BLE and HTTP init construct their respective
//! variants manually.

#![forbid(unsafe_code)]
use std::net::{Ipv4Addr, UdpSocket};
use std::sync::atomic::AtomicBool;
use std::sync::Arc;
use std::time::{Duration, Instant};

use esp_idf_hal::modem::WifiModemPeripheral;
use esp_idf_svc::eventloop::EspSystemEventLoop;
use esp_idf_svc::nvs::EspDefaultNvsPartition;
use esp_idf_svc::wifi::{BlockingWifi, EspWifi};

use embedded_svc::wifi::{
    AccessPointConfiguration, AuthMethod, ClientConfiguration, Configuration, Wifi,
};

use esp_idf_svc::mdns::EspMdns;

use crate::config;
use crate::errors::NetworkError;

/// Helper: convert &str to heapless::String<N>.
fn str_to_heapless<const N: usize>(s: &str) -> heapless::String<N> {
    let mut r = heapless::String::new();
    r.push_str(s).ok();
    r
}

/// WiFi operating mode.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WifiMode {
    Off,
    ApMode,
    StaConnecting,
    StaActive,
}

/// WiFi manager — AP + STA + DNS responder.
pub struct WifiManager<'d> {
    /// Blocking WiFi handle. `None` while switching modes.
    wifi: Option<BlockingWifi<EspWifi<'d>>>,
    /// Current operating mode.
    mode: WifiMode,
    /// AP IP address (192.168.4.1).
    ap_ip: Ipv4Addr,
    /// UDP socket for DNS responder (port 53 on AP_IP).
    dns_socket: Option<UdpSocket>,
    /// Timestamp of last reconnect attempt.
    last_reconnect: Instant,
    /// BLE active flag (shared with BLE manager).
    #[expect(dead_code)]
    ble_active: Arc<AtomicBool>,
    /// mDNS responder (ecotiter.local).
    mdns: Option<EspMdns>,
    /// Saved SSID from NVS (empty if none).
    saved_ssid: heapless::String<32>,
    /// Saved password from NVS (empty if none).
    saved_password: heapless::String<64>,
}

impl<'d> WifiManager<'d> {
    /// Create a new WiFi manager.
    ///
    /// # Arguments
    ///
    /// * `modem` - WiFi modem peripheral (consumed once).
    /// * `sys_loop` - System event loop.
    /// * `nvs` - Optional NVS partition for credential storage.
    /// * `ble_active` - Shared atomic flag for BLE active state.
    ///
    /// This is a blocking call (init only). Loads saved credentials from NVS.
    ///
    /// # Errors
    ///
    /// Returns `NetworkError::WifiConnectionFailed` if EspWifi initialisation fails.
    pub fn new<M: WifiModemPeripheral + 'd>(
        modem: M,
        sys_loop: EspSystemEventLoop,
        nvs: Option<EspDefaultNvsPartition>,
        ble_active: Arc<AtomicBool>,
    ) -> Result<Self, NetworkError> {
        let (ssid, password) = Self::load_credentials_from_nvs();
        let wifi = EspWifi::new(modem, sys_loop.clone(), nvs)?;
        let wifi = BlockingWifi::wrap(wifi, sys_loop)?;

        Ok(Self {
            wifi: Some(wifi),
            mode: WifiMode::Off,
            ap_ip: Ipv4Addr::new(
                config::AP_IP[0],
                config::AP_IP[1],
                config::AP_IP[2],
                config::AP_IP[3],
            ),
            dns_socket: None,
            mdns: None,
            last_reconnect: Instant::now(),
            ble_active,
            saved_ssid: ssid,
            saved_password: password,
        })
    }

    /// Create a WiFi manager in offline mode — no WiFi at all.
    ///
    /// Use when EspWifi init fails. HTTP can still serve WebUI,
    /// and process() will be a no-op (returns immediately).
    pub fn offline(ble_active: Arc<AtomicBool>) -> Self {
        Self {
            wifi: None,
            mode: WifiMode::Off,
            ap_ip: Ipv4Addr::new(
                config::AP_IP[0],
                config::AP_IP[1],
                config::AP_IP[2],
                config::AP_IP[3],
            ),
            dns_socket: None,
            mdns: None,
            last_reconnect: Instant::now(),
            ble_active,
            saved_ssid: heapless::String::new(),
            saved_password: heapless::String::new(),
        }
    }

    /// Initialise WiFi: try STA connect → if fail, clear creds → start AP.
    ///
    /// This is a blocking call (init only). Called once at boot.
    pub fn init(&mut self) {
        // Try STA mode first if we have saved credentials
        if !self.saved_ssid.is_empty() {
            log::info!("WiFi: trying STA connect to '{}'", self.saved_ssid.as_str());
            if self.try_sta_connect() {
                log::info!("WiFi: STA connected to '{}'", self.saved_ssid.as_str());
                self.start_mdns();
                return;
            }
            // STA failed — clear saved credentials and fall back to AP
            log::warn!("WiFi: STA connection failed, clearing credentials");
            Self::clear_credentials_from_nvs();
            self.saved_ssid.clear();
            self.saved_password.clear();
        }

        // Start AP mode
        log::info!("WiFi: starting AP '{}'", config::AP_SSID);
        self.start_ap();
        self.mode = WifiMode::ApMode;
        log::info!("WiFi: AP ready at {}", self.ap_ip);
    }

    /// Start AP mode with custom network interface at 192.168.4.1/24.
    ///
    /// This is a blocking call (init only).
    fn start_ap(&mut self) {
        use esp_idf_svc::ipv4::{Configuration as IpConfig, Mask, RouterConfiguration, Subnet};
        use esp_idf_svc::netif::{EspNetif, NetifConfiguration, NetifStack};

        let Some(wifi) = self.wifi.as_mut() else {
            return;
        };
        let _ = wifi.stop();

        // Create custom AP EspNetif with 192.168.4.1/24
        // This ensures DHCP assigns 192.168.4.x and DNS on 192.168.4.1 is reachable

        let ap_ip = self.ap_ip;
        let ap_netif_conf = NetifConfiguration {
            flags: 0,
            got_ip_event_id: None,
            lost_ip_event_id: None,
            key: str_to_heapless("WIFI_AP_CUSTOM"),
            description: str_to_heapless("ap"),
            route_priority: 10,
            ip_configuration: Some(IpConfig::Router(RouterConfiguration {
                subnet: Subnet {
                    gateway: ap_ip,
                    mask: Mask(config::AP_NETMASK_BITS),
                },
                dhcp_enabled: true,
                dns: Some(ap_ip),
                secondary_dns: None,
            })),
            stack: NetifStack::Ap,
            custom_mac: None,
        };

        match EspNetif::new_with_conf(&ap_netif_conf) {
            Ok(custom_ap) => {
                if let Err(e) = wifi.wifi_mut().swap_netif_ap(custom_ap) {
                    log::error!("WiFi: swap AP netif failed: {e}");
                    return;
                }
            }
            Err(e) => {
                log::error!("WiFi: create AP netif failed: {e}");
                return;
            }
        }

        // Configure and start AP
        let ap_config = AccessPointConfiguration {
            ssid: str_to_heapless(config::AP_SSID),
            password: str_to_heapless(config::AP_PASSWORD),
            channel: config::AP_CHANNEL,
            max_connections: config::AP_MAX_CONNECTIONS,
            auth_method: AuthMethod::WPA2Personal,
            ..Default::default()
        };

        if wifi
            .set_configuration(&Configuration::AccessPoint(ap_config))
            .is_err()
        {
            log::error!("WiFi: set_configuration (AP) failed");
            return;
        }

        if wifi.start().is_err() {
            log::error!("WiFi: start (AP) failed");
            return;
        }

        self.start_dns();
        self.start_mdns();
    }

    /// Start DNS responder on port 53 (AP IP address).
    ///
    /// Binds a UDP socket to AP_IP:53 in non-blocking mode.
    /// This is a blocking call (init only).
    fn start_dns(&mut self) {
        let bind_addr = (self.ap_ip, 53);
        match UdpSocket::bind(bind_addr) {
            Ok(socket) => {
                socket.set_nonblocking(true).ok();
                log::info!("DNS: responder bound to {bind_addr:?}");
                self.dns_socket = Some(socket);
            }
            Err(e) => {
                // Fallback: try binding to 0.0.0.0:53
                log::warn!("DNS: bind to {bind_addr:?} failed: {e}, trying 0.0.0.0:53");
                match UdpSocket::bind(("0.0.0.0", 53)) {
                    Ok(socket) => {
                        socket.set_nonblocking(true).ok();
                        log::info!("DNS: responder bound to 0.0.0.0:53 (fallback)");
                        self.dns_socket = Some(socket);
                    }
                    Err(e2) => {
                        log::error!("DNS: bind to 0.0.0.0:53 also failed: {e2}");
                    }
                }
            }
        }
    }

    /// Start mDNS responder for ecotiter.local.
    ///
    /// Registers hostname and HTTP service on both STA and AP interfaces.
    /// ESP-IDF mDNS handles .local resolution automatically via mDNS multicast.
    fn start_mdns(&mut self) {
        let mut mdns = match EspMdns::take() {
            Ok(m) => m,
            Err(e) => {
                log::warn!("mDNS: init failed: {e}");
                return;
            }
        };
        if let Err(e) = mdns.set_hostname(config::MDNS_HOSTNAME) {
            log::warn!("mDNS: set_hostname failed: {e}");
            return;
        }
        if let Err(e) = mdns.add_service(None, "_http", "_tcp", 80, &[]) {
            log::warn!("mDNS: add_service failed: {e}");
            return;
        }
        log::info!("mDNS: hostname '{}' registered", config::MDNS_HOSTNAME);
        self.mdns = Some(mdns);
    }

    /// Process a single DNS query from the UDP socket.
    ///
    /// Non-blocking: returns immediately if no data available.
    fn process_dns(&self) {
        let Some(socket) = self.dns_socket.as_ref() else {
            return;
        };

        let mut buf = [0u8; crate::domain::memory::DNS_BUF_SIZE];
        match socket.recv_from(&mut buf) {
            Ok((n, src)) => {
                let query = &buf[..n];
                log::info!("DNS: query from {src}: {n} bytes");

                let response = build_dns_response(query, self.ap_ip.octets());
                if !response.is_empty() {
                    if let Err(e) = socket.send_to(&response, src) {
                        log::warn!("DNS: send failed: {e}");
                    }
                }
            }
            Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                // No data available — expected for non-blocking socket
            }
            Err(e) => {
                log::warn!("DNS: recv error: {e}");
            }
        }
    }

    /// Process DNS queries and handle STA reconnect timer.
    ///
    /// This is the ONLY non-blocking method. Called from the main loop
    /// every tick (~10 ms) via `try_lock()`.
    pub fn process(&mut self) {
        // Process DNS queries (AP mode only)
        if self.mode == WifiMode::ApMode {
            self.process_dns();
        }

        // STA reconnect timer (every 30 seconds)
        if self.mode == WifiMode::StaActive
            && self.last_reconnect.elapsed()
                >= Duration::from_millis(config::STA_RECONNECT_INTERVAL_MS)
        {
            self.last_reconnect = Instant::now();
            if let Some(ref mut wifi) = self.wifi {
                if wifi.is_connected().unwrap_or(false) {
                    // Already connected
                } else {
                    log::info!("WiFi: STA reconnect check — disconnected, reconnecting...");
                    let _ = wifi.connect();
                }
            }
        }
    }

    /// Try to connect to the saved STA credentials.
    ///
    /// Blocking: called only from init or HTTP handler.
    /// Returns `true` if connected successfully.
    pub fn try_sta_connect(&mut self) -> bool {
        if self.saved_ssid.is_empty() {
            return false;
        }
        let ssid = self.saved_ssid.clone();
        let password = self.saved_password.clone();
        self.try_connect_sta(ssid.as_str(), password.as_str())
    }

    /// Try to connect to a specific STA network with the given credentials.
    ///
    /// Blocking: called only from init or HTTP handler.
    /// Returns `true` if connected successfully.
    pub fn try_connect_sta(&mut self, ssid: &str, password: &str) -> bool {
        let Some(wifi) = self.wifi.as_mut() else {
            return false;
        };

        // Stop current mode
        let _ = wifi.stop();
        // Wait for disconnect to complete
        std::thread::sleep(Duration::from_millis(100));

        self.mode = WifiMode::StaConnecting;

        let client_config = ClientConfiguration {
            ssid: str_to_heapless(ssid),
            password: str_to_heapless(password),
            ..Default::default()
        };

        if wifi
            .set_configuration(&Configuration::Client(client_config))
            .is_err()
        {
            self.mode = WifiMode::Off;
            return false;
        }

        if wifi.start().is_err() {
            self.mode = WifiMode::Off;
            return false;
        }

        log::info!("WiFi: connecting to STA '{ssid}'...");
        if wifi.connect().is_err() {
            log::warn!("WiFi: STA connect failed for '{ssid}'");
            self.mode = WifiMode::Off;
            return false;
        }

        // Wait for IP with polling + timeout
        let deadline = Instant::now() + Duration::from_millis(config::STA_CONNECT_TIMEOUT_MS);
        let mut connected = false;
        while Instant::now() < deadline {
            if wifi.is_connected().unwrap_or(false) {
                // Wait for DHCP IP assignment before continuing
                let ip_deadline =
                    Instant::now() + Duration::from_millis(config::STA_DHCP_TIMEOUT_MS);
                while Instant::now() < ip_deadline {
                    if let Ok(info) = wifi.wifi().sta_netif().get_ip_info() {
                        if info.ip != Ipv4Addr::UNSPECIFIED {
                            log::info!("WiFi: got IP {}", info.ip);
                            connected = true;
                            break;
                        }
                    }
                    std::thread::sleep(Duration::from_millis(100));
                }
                if connected {
                    break;
                }
                log::warn!("WiFi: DHCP timeout, continuing without IP");
                connected = true; // Still mark as connected for AP fallback
                break;
            }
            std::thread::sleep(Duration::from_millis(config::STA_POLL_MS));
        }

        if !connected {
            log::warn!("WiFi: STA connect timeout for '{ssid}'");
            let _ = wifi.disconnect();
            self.mode = WifiMode::Off;
            return false;
        }

        self.mode = WifiMode::StaActive;
        self.last_reconnect = Instant::now();
        log::info!("WiFi: STA connected to '{ssid}'");
        true
    }

    /// Stop WiFi and drop DNS socket.
    ///
    /// Blocking: called during mode switch or shutdown.
    pub fn stop(&mut self) {
        self.dns_socket = None;
        if let Some(ref mut wifi) = self.wifi {
            let _ = wifi.disconnect();
            let _ = wifi.stop();
        }
        self.mode = WifiMode::Off;
    }

    // ── Getters (non-blocking) ──────────────────────────────────

    /// Return the current WiFi mode.
    pub const fn mode(&self) -> WifiMode {
        self.mode
    }

    /// Returns `true` if STA is connected and active.
    pub fn is_connected(&self) -> bool {
        self.mode == WifiMode::StaActive
    }

    /// Returns `true` if currently in AP mode.
    pub fn is_ap_mode(&self) -> bool {
        self.mode == WifiMode::ApMode
    }

    /// Get current RSSI (STA mode only).
    pub fn wifi_rssi(&self) -> Option<i32> {
        self.wifi.as_ref().and_then(|w| w.wifi().get_rssi().ok())
    }

    /// Get current SSID (STA mode only).
    pub fn wifi_ssid(&self) -> Option<&str> {
        if self.mode == WifiMode::StaActive {
            Some(self.saved_ssid.as_str())
        } else {
            None
        }
    }

    /// Get current IP address.
    ///
    /// Returns AP_IP in AP mode, or the DHCP-assigned IP in STA mode.
    pub fn wifi_ip(&self) -> Option<Ipv4Addr> {
        match self.mode {
            WifiMode::ApMode => Some(self.ap_ip),
            WifiMode::StaActive => self
                .wifi
                .as_ref()
                .and_then(|w| w.wifi().sta_netif().get_ip_info().ok())
                .map(|info| info.ip),
            _ => None,
        }
    }

    // ── NVS credential helpers (static) ─────────────────────────

    /// Load saved WiFi credentials from NVS (namespace "wifi", keys "ssid"/"password").
    fn load_credentials_from_nvs() -> (heapless::String<32>, heapless::String<64>) {
        use crate::infrastructure::storage::nvs;

        let ssid = nvs::wifi_read_str::<32>("ssid")
            .ok()
            .flatten()
            .unwrap_or_default();
        let password = nvs::wifi_read_str::<64>("password")
            .ok()
            .flatten()
            .unwrap_or_default();
        (ssid, password)
    }

    /// Save WiFi credentials to NVS.
    ///
    /// Opens namespace "wifi" in read-write mode and writes "ssid"/"password".
    pub fn save_credentials_to_nvs(ssid: &str, password: &str) {
        use crate::infrastructure::storage::nvs;

        if nvs::wifi_write_str("ssid", ssid).is_ok() {
            log::info!("WiFi: saved SSID to NVS");
        }
        if nvs::wifi_write_str("password", password).is_ok() {
            log::info!("WiFi: saved password to NVS");
        }
    }

    /// Clear saved WiFi credentials from NVS.
    ///
    /// Opens namespace "wifi" in read-write mode and erases "ssid"/"password".
    pub fn clear_credentials_from_nvs() {
        use crate::infrastructure::storage::nvs;

        let _ = nvs::wifi_erase("ssid");
        let _ = nvs::wifi_erase("password");
        log::info!("WiFi: cleared credentials from NVS");
    }
}

// ── DNS Response Builder (re-export from domain) ───────────────
pub use crate::domain::dns::build_dns_response;
