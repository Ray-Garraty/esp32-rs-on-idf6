use std::net::{Ipv4Addr, UdpSocket};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};

use embedded_svc::wifi::*;
use esp_idf_svc::eventloop::EspSystemEventLoop;
use esp_idf_svc::hal::modem::Modem;
use esp_idf_svc::nvs::EspDefaultNvsPartition;
use esp_idf_svc::sys::EspError;
use esp_idf_svc::wifi::*;
use log::*;

use crate::config;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum WifiMode {
    Off,
    ApMode,
    StaConnecting,
    StaActive,
}

pub struct WifiManager {
    wifi: Option<BlockingWifi<EspWifi<'static>>>,
    mode: WifiMode,
    #[allow(dead_code)]
    ap_ip: Ipv4Addr,
    dns_socket: Option<UdpSocket>,
    last_reconnect: Instant,
    ble_active: Arc<AtomicBool>,
    saved_ssid: heapless::String<32>,
    saved_password: heapless::String<64>,
}

impl WifiManager {
    pub fn new(
        modem: Modem<'static>,
        sys_loop: EspSystemEventLoop,
        nvs: Option<EspDefaultNvsPartition>,
        ble_active: Arc<AtomicBool>,
    ) -> Result<Self, EspError> {
        let wifi = EspWifi::new(modem, sys_loop.clone(), nvs)?;
        let wifi = BlockingWifi::wrap(wifi, sys_loop)?;

        let (saved_ssid, saved_password) = Self::load_credentials_from_nvs();

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
            last_reconnect: Instant::now(),
            ble_active,
            saved_ssid,
            saved_password,
        })
    }

    fn load_credentials_from_nvs() -> (heapless::String<32>, heapless::String<64>) {
        let ssid = nvs_read_str("wifi", "ssid");
        let pass = nvs_read_str("wifi", "password");
        (ssid, pass)
    }

    pub fn save_credentials_to_nvs(ssid: &str, password: &str) {
        nvs_write_str("wifi", "ssid", ssid);
        nvs_write_str("wifi", "password", password);
        info!("WiFi credentials saved to NVS");
    }

    pub fn clear_credentials_from_nvs() {
        nvs_write_str("wifi", "ssid", "");
        nvs_write_str("wifi", "password", "");
        info!("WiFi credentials cleared from NVS");
    }

    pub fn init(&mut self) {
        info!("WiFi manager init");
        if self.saved_ssid.len() > 0 && self.saved_password.len() > 0 {
            info!(
                "Saved credentials found for SSID: {}",
                self.saved_ssid.as_str()
            );
            if self.try_sta_connect() {
                return; // Connected successfully — skip AP
            }
            // Failed — clear bad credentials (C++ behavior)
            info!("Failed to connect with saved credentials, clearing");
            Self::clear_credentials_from_nvs();
            self.saved_ssid.clear();
            self.saved_password.clear();
        } else {
            info!("No saved WiFi credentials");
        }

        info!("Starting AP mode (captive portal)");
        self.start_ap();
    }

    /// Try connecting to STA with given credentials (used from captive portal)
    pub fn try_connect_sta(&mut self, ssid: &str, password: &str) -> bool {
        use std::time::{Duration, Instant};

        let wifi = match self.wifi.as_mut() {
            Some(w) => w,
            None => return false,
        };
        self.mode = WifiMode::StaConnecting;

        let ssid_h: heapless::String<32> = match heapless::String::try_from(ssid) {
            Ok(s) => s,
            Err(_) => { self.mode = WifiMode::Off; return false; }
        };
        let pass_h: heapless::String<64> = match heapless::String::try_from(password) {
            Ok(p) => p,
            Err(_) => { self.mode = WifiMode::Off; return false; }
        };

        info!("Trying to connect to STA: {}", ssid);

        let client_config = ClientConfiguration {
            ssid: ssid_h,
            bssid: None,
            auth_method: AuthMethod::WPA2Personal,
            password: pass_h,
            channel: None,
            ..Default::default()
        };

        if wifi.set_configuration(&Configuration::Client(client_config)).is_err() {
            warn!("set_configuration failed");
            self.mode = WifiMode::Off;
            return false;
        }

        if wifi.start().is_err() {
            warn!("WiFi start failed");
            self.mode = WifiMode::Off;
            return false;
        }

        if wifi.connect().is_err() {
            warn!("WiFi connect failed for: {}", ssid);
            wifi.stop().ok();
            self.mode = WifiMode::Off;
            return false;
        }

        let deadline = Instant::now() + Duration::from_millis(config::STA_CONNECT_TIMEOUT_MS as u64);
        loop {
            if wifi.is_connected().unwrap_or(false) {
                break;
            }
            if Instant::now() >= deadline {
                warn!("STA connect timeout for: {}", ssid);
                wifi.disconnect().ok();
                wifi.stop().ok();
                self.mode = WifiMode::Off;
                return false;
            }
            std::thread::sleep(Duration::from_millis(config::STA_POLL_MS as u64));
        }

        std::thread::sleep(Duration::from_millis(config::STA_POST_CONNECT_DELAY_MS as u64));

        if wifi.is_connected().unwrap_or(false) {
            self.mode = WifiMode::StaActive;
            info!("WiFi connected! SSID: {}", ssid);
            if let Ok(ip) = wifi.wifi().sta_netif().get_ip_info() {
                info!("IP: {:?}", ip);
            }
            true
        } else {
            warn!("STA connect timeout for: {}", ssid);
            wifi.disconnect().ok();
            wifi.stop().ok();
            self.mode = WifiMode::Off;
            false
        }
    }

    fn try_sta_connect(&mut self) -> bool {
        let wifi = match self.wifi.as_mut() {
            Some(w) => w,
            None => return false,
        };
        self.mode = WifiMode::StaConnecting;

        let ssid_str = self.saved_ssid.as_str();

        info!("Connecting to STA: {}", ssid_str);

        let client_config = ClientConfiguration {
            ssid: self.saved_ssid.clone(),
            bssid: None,
            auth_method: AuthMethod::WPA2Personal,
            password: self.saved_password.clone(),
            channel: None,
            ..Default::default()
        };

        if wifi
            .set_configuration(&Configuration::Client(client_config))
            .is_err()
        {
            warn!("set_configuration failed");
            self.mode = WifiMode::Off;
            return false;
        }

        if wifi.start().is_err() {
            warn!("WiFi start failed");
            self.mode = WifiMode::Off;
            return false;
        }

        if wifi.connect().is_err() {
            warn!("WiFi connect failed for: {}", ssid_str);
            wifi.stop().ok();
            self.mode = WifiMode::Off;
            return false;
        }

        let deadline = Instant::now() + Duration::from_millis(config::STA_CONNECT_TIMEOUT_MS as u64);
        loop {
            if wifi.is_connected().unwrap_or(false) {
                break;
            }
            if Instant::now() >= deadline {
                warn!("STA connect timeout for: {}", ssid_str);
                wifi.disconnect().ok();
                wifi.stop().ok();
                self.mode = WifiMode::Off;
                return false;
            }
            std::thread::sleep(Duration::from_millis(config::STA_POLL_MS as u64));
        }

        // Give time for DHCP to complete
        std::thread::sleep(Duration::from_millis(
            config::STA_POST_CONNECT_DELAY_MS as u64,
        ));

        if wifi.is_connected().unwrap_or(false) {
            self.mode = WifiMode::StaActive;
            info!("WiFi connected! SSID: {}", ssid_str);
            if let Ok(ip) = wifi.wifi().sta_netif().get_ip_info() {
                info!("IP: {:?}", ip);
            }
            true
        } else {
            warn!("STA connect timeout for: {}", ssid_str);
            wifi.disconnect().ok();
            wifi.stop().ok();
            self.mode = WifiMode::Off;
            false
        }
    }

    fn start_ap(&mut self) {
        let wifi = match self.wifi.as_mut() {
            Some(w) => w,
            None => return,
        };

        info!("Starting AP: {} / ch {}", config::AP_SSID, config::AP_CHANNEL);

        // Create AP EspNetif with 192.168.4.1/24 before wifi.start(),
        // so DHCP server starts with the correct subnet from the beginning.
        {
            use esp_idf_svc::ipv4::{Configuration as IpConfig, RouterConfiguration, Subnet, Mask};
            use esp_idf_svc::netif::{EspNetif, NetifConfiguration, NetifStack};
            use std::net::Ipv4Addr;

            let ap_ip = Ipv4Addr::new(config::AP_IP[0], config::AP_IP[1], config::AP_IP[2], config::AP_IP[3]);

            let ap_netif_conf = NetifConfiguration {
                flags: 0,
                got_ip_event_id: None,
                lost_ip_event_id: None,
                key: "WIFI_AP_CUSTOM".try_into().unwrap(),
                description: "ap".try_into().unwrap(),
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

            let custom_ap = match EspNetif::new_with_conf(&ap_netif_conf) {
                Ok(n) => n,
                Err(e) => {
                    warn!("Failed to create custom AP netif: {}", e);
                    return;
                }
            };

            if let Err(e) = wifi.wifi_mut().swap_netif_ap(custom_ap) {
                warn!("Failed to swap AP netif: {}", e);
                return;
            }
        }

        let ap_config = AccessPointConfiguration {
            ssid: config::AP_SSID.try_into().unwrap_or_default(),
            ssid_hidden: false,
            channel: config::AP_CHANNEL,
            auth_method: AuthMethod::WPA2Personal,
            password: config::AP_PASSWORD.try_into().unwrap_or_default(),
            max_connections: config::AP_MAX_CONNECTIONS,
            ..Default::default()
        };

        if wifi
            .set_configuration(&Configuration::AccessPoint(ap_config))
            .is_err()
        {
            warn!("AP set_configuration failed");
            return;
        }

        if wifi.start().is_err() {
            warn!("AP start failed");
            return;
        }

        info!("AP ready at {}:{}", config::AP_SSID, config::AP_IP_ADDRESS);

        std::thread::sleep(Duration::from_millis(500));
        self.mode = WifiMode::ApMode;
        self.start_dns();
    }

    fn start_dns(&mut self) {
        // Bind to AP IP explicitly — LwIP needs a concrete interface IP,
        // not 0.0.0.0 wildcard, to receive DNS from AP clients
        let bind_addr = format!("{}.{}.{}.{}:53",
            config::AP_IP[0], config::AP_IP[1],
            config::AP_IP[2], config::AP_IP[3]);
        let socket = match UdpSocket::bind(&bind_addr) {
            Ok(s) => {
                s.set_read_timeout(Some(Duration::from_millis(5))).ok();
                s.set_nonblocking(true).ok();
                info!("DNS responder started on {}:53", config::AP_IP_ADDRESS);
                s
            }
            Err(e) => {
                // Fallback: try 0.0.0.0
                warn!("DNS bind {} failed: {}, trying 0.0.0.0:53", bind_addr, e);
                match UdpSocket::bind("0.0.0.0:53") {
                    Ok(s) => {
                        s.set_read_timeout(Some(Duration::from_millis(5))).ok();
                        s.set_nonblocking(true).ok();
                        info!("DNS responder started on 0.0.0.0:53");
                        s
                    }
                    Err(e2) => {
                        warn!("DNS bind 0.0.0.0:53 also failed: {}", e2);
                        return;
                    }
                }
            }
        };
        self.dns_socket = Some(socket);
    }

    fn process_dns(&mut self) {
        let socket = match self.dns_socket.as_ref() {
            Some(s) => s,
            None => return,
        };

        let mut buf = [0u8; 512];
        loop {
            match socket.recv_from(&mut buf) {
                Ok((len, src)) => {
                    if len < 12 {
                        continue;
                    }
                    let response = build_dns_response(&buf[..len], config::AP_IP);
                    if !response.is_empty() {
                        socket.send_to(&response, src).ok();
                    }
                }
                Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
                Err(ref e) if e.kind() == std::io::ErrorKind::TimedOut => break,
                Err(e) => {
                    warn!("DNS recv error: {}", e);
                    break;
                }
            }
        }
    }

    pub fn process(&mut self) {
        if self.dns_socket.is_some() {
            self.process_dns();
        }

        if self.mode == WifiMode::StaActive {
            let is_connected = self
                .wifi
                .as_ref()
                .and_then(|w| w.is_connected().ok())
                .unwrap_or(false);

            if !is_connected {
                let elapsed = self.last_reconnect.elapsed();
                if elapsed >= Duration::from_millis(config::STA_RECONNECT_INTERVAL_MS as u64) {
                    info!("WiFi disconnected, attempting reconnect");
                    self.wifi.as_mut().map(|w| w.connect().ok());
                    self.last_reconnect = Instant::now();
                }
            } else {
                self.last_reconnect = Instant::now();
            }
        }
    }

    pub fn stop(&mut self) {
        info!("Stopping WiFi");
        self.dns_socket = None;
        if let Some(wifi) = self.wifi.as_mut() {
            wifi.disconnect().ok();
            wifi.stop().ok();
        }
        self.mode = WifiMode::Off;
    }

    pub fn set_ble_active(&mut self, _active: bool) {
        self.ble_active.store(true, Ordering::Relaxed);
    }

    pub fn mode(&self) -> WifiMode {
        self.mode
    }

    pub fn is_connected(&self) -> bool {
        self.mode == WifiMode::StaActive
    }

    pub fn wifi_rssi(&self) -> Option<i32> {
        if self.mode == WifiMode::StaActive {
            self.wifi.as_ref().and_then(|w| w.wifi().get_rssi().ok())
        } else {
            None
        }
    }

    pub fn wifi_ssid(&self) -> Option<&str> {
        if self.mode == WifiMode::StaActive {
            Some(self.saved_ssid.as_str())
        } else {
            None
        }
    }

    pub fn has_saved_credentials(&self) -> bool {
        !self.saved_ssid.is_empty() && !self.saved_password.is_empty()
    }

    pub fn is_ap_mode(&self) -> bool {
        self.mode == WifiMode::ApMode
    }

    pub fn wifi_ip(&self) -> Option<String> {
        if self.mode == WifiMode::StaActive {
            if let Some(wifi) = self.wifi.as_ref() {
                if let Ok(ip) = wifi.wifi().sta_netif().get_ip_info() {
                    return Some(ip.ip.to_string());
                }
            }
        }
        None
    }
}

fn nvs_read_str<const N: usize>(namespace: &str, key: &str) -> heapless::String<N> {
    use std::ffi::CString;
    use esp_idf_sys::{nvs_handle_t, nvs_open_mode_t_NVS_READONLY};
    let mut handle: nvs_handle_t = 0;
    let ns = match CString::new(namespace) { Ok(c) => c, Err(_) => return heapless::String::new() };
    let k = match CString::new(key) { Ok(c) => c, Err(_) => return heapless::String::new() };

    let ret = unsafe { esp_idf_sys::nvs_open(ns.as_ptr(), nvs_open_mode_t_NVS_READONLY, &mut handle) };
    if ret != esp_idf_sys::ESP_OK {
        return heapless::String::new();
    }

    let mut len: usize = 0;
    unsafe {
        esp_idf_sys::nvs_get_str(handle, k.as_ptr(), core::ptr::null_mut(), &mut len);
    }
    if len == 0 {
        unsafe { esp_idf_sys::nvs_close(handle); }
        return heapless::String::new();
    }

    let mut buf: Vec<u8> = vec![0u8; len];
    unsafe {
        esp_idf_sys::nvs_get_str(handle, k.as_ptr(), buf.as_mut_ptr() as *mut ::core::ffi::c_char, &mut len);
        esp_idf_sys::nvs_close(handle);
    }

    let s = String::from_utf8_lossy(&buf[..len.saturating_sub(1)]);
    heapless::String::<N>::try_from(s.as_ref()).unwrap_or_default()
}

fn nvs_write_str(namespace: &str, key: &str, value: &str) {
    use std::ffi::CString;
    use esp_idf_sys::{nvs_handle_t, nvs_open_mode_t_NVS_READWRITE};
    let mut handle: nvs_handle_t = 0;
    let ns = match CString::new(namespace) { Ok(c) => c, Err(_) => return };
    let k = match CString::new(key) { Ok(c) => c, Err(_) => return };
    let v = match CString::new(value) { Ok(c) => c, Err(_) => return };

    let ret = unsafe { esp_idf_sys::nvs_open(ns.as_ptr(), nvs_open_mode_t_NVS_READWRITE, &mut handle) };
    if ret != esp_idf_sys::ESP_OK {
        warn!("NVS open '{}' failed: {}", namespace, ret);
        return;
    }

    let ret = unsafe { esp_idf_sys::nvs_set_str(handle, k.as_ptr(), v.as_ptr()) };
    if ret != esp_idf_sys::ESP_OK {
        warn!("NVS set '{}' failed: {}", key, ret);
    }

    unsafe { esp_idf_sys::nvs_commit(handle); }
    unsafe { esp_idf_sys::nvs_close(handle); }
}

fn build_dns_response(query: &[u8], ip: [u8; 4]) -> Vec<u8> {
    if query.len() < 12 {
        return Vec::new();
    }

    let mut resp = Vec::with_capacity(query.len() + 16);

    resp.extend_from_slice(&query[..12]);
    resp[2] |= 0x80;
    resp[3] |= 0x80;
    resp[6] = 0;
    resp[7] = 1;

    let mut qpos: usize = 12;
    loop {
        if qpos >= query.len() {
            return Vec::new();
        }
        let label_len = query[qpos] as usize;
        if label_len == 0 {
            qpos += 1;
            break;
        }
        qpos += 1 + label_len;
    }

    if qpos + 4 > query.len() {
        return Vec::new();
    }
    qpos += 4;

    resp.extend_from_slice(&query[12..qpos]);

    resp.push(0xC0);
    resp.push(0x0C);
    resp.extend_from_slice(&[0x00, 0x01]);
    resp.extend_from_slice(&[0x00, 0x01]);
    resp.extend_from_slice(&[0x00, 0x00, 0x00, 0x78]);
    resp.extend_from_slice(&[0x00, 0x04]);
    resp.extend_from_slice(&ip);

    resp
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_dns_response_has_expected_structure() {
        let query = build_test_query();
        let resp = build_dns_response(&query, [192, 168, 4, 1]);
        assert!(!resp.is_empty(), "response should not be empty");
        assert_eq!(resp[0], query[0], "ID low byte should match");
        assert_eq!(resp[1], query[1], "ID high byte should match");
        assert_eq!(resp[2] & 0x80, 0x80, "QR bit should be set");
        assert_eq!(resp[6], 0, "question count high should be 0");
        assert_eq!(resp[7], 1, "answer count should be 1");
    }

    #[test]
    fn test_dns_response_ip_bytes() {
        let query = build_test_query();
        let ip = [10, 0, 0, 1];
        let resp = build_dns_response(&query, ip);
        let tail = &resp[resp.len() - 4..];
        assert_eq!(tail, &ip, "last 4 bytes should be the IP");
    }

    #[test]
    fn test_dns_response_truncated_query() {
        let resp = build_dns_response(&[0u8; 5], [192, 168, 4, 1]);
        assert!(resp.is_empty(), "truncated query should return empty");
    }

    #[test]
    fn test_dns_response_multi_label_query() {
        let mut query = Vec::new();
        query.extend_from_slice(&[0x12, 0x34]);
        query.extend_from_slice(&[0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        query.push(3);
        query.extend_from_slice(b"www");
        query.push(7);
        query.extend_from_slice(b"example");
        query.push(3);
        query.extend_from_slice(b"com");
        query.push(0);
        query.extend_from_slice(&[0x00, 0x01, 0x00, 0x01]);

        let resp = build_dns_response(&query, [192, 168, 4, 1]);
        assert!(!resp.is_empty(), "multi-label query should produce response");
        assert_eq!(resp[7], 1, "answer count should be 1");
    }

    fn build_test_query() -> Vec<u8> {
        let mut q = Vec::new();
        q.extend_from_slice(&[0x12, 0x34]);
        q.extend_from_slice(&[0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        q.push(5);
        q.extend_from_slice(b"hello");
        q.push(0);
        q.extend_from_slice(&[0x00, 0x01, 0x00, 0x01]);
        q
    }
}
