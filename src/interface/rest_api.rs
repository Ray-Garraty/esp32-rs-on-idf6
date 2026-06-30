//! REST API route handler function signatures.
//!
//! These functions are called by the EspHttpServer route handlers (Phase 4).
//! They build JSON responses using `CompactJson` / `heapless::String` buffers.
//!
//! The actual EspHttpServer registration will be in `infrastructure/network/http_server.rs`.

use core::fmt::Write as CoreWrite;

use crate::domain::memory::MAX_RESPONSE_SIZE;
use heapless::String;

/// Handle GET /api/ping — health check.
pub fn handle_api_ping() -> String<MAX_RESPONSE_SIZE> {
    let mut buf: String<MAX_RESPONSE_SIZE> = String::new();
    let _ = write!(buf, r#"{{"status":"ok"}}"#);
    buf
}

/// Handle GET /api/status — full device status broadcast.
///
/// Builds a `BroadcastEvent`-style JSON with WiFi info and hardware state.
///
/// # Arguments
///
/// * `wifi_connected` - Whether STA is connected.
/// * `wifi_ssid` - Current SSID (if connected).
/// * `wifi_rssi` - Current RSSI in dBm (if connected).
/// * `wifi_ip` - Current IP address (if connected or in AP mode).
/// * `is_ap_mode` - Whether in AP mode.
/// * `temp` - Current temperature in Celsius.
/// * `mv` - Current ADC millivolts.
/// * `vlv` - Current valve position string ("in" or "out").
/// * `brt_status` - Burette status string ("idle", "working", "error").
/// * `brt_vol` - Burette volume in ml.
/// * `brt_speed` - Burette speed in ml/min.
/// * `ts` - Timestamp (ms since boot).
pub fn handle_api_status(
    wifi_connected: bool,
    wifi_ssid: Option<&str>,
    wifi_rssi: Option<i32>,
    wifi_ip: Option<&str>,
    is_ap_mode: bool,
    temp: Option<f32>,
    mv: i16,
    vlv: &str,
    brt_status: &str,
    brt_vol: f32,
    brt_speed: f32,
    ts: u64,
) -> String<MAX_RESPONSE_SIZE> {
    let mut buf: String<MAX_RESPONSE_SIZE> = String::new();

    // Build wifi sub-object
    let wifi_ssid_str = wifi_ssid.unwrap_or("");
    let wifi_ip_str = wifi_ip.unwrap_or("");
    let wifi_rssi_val = wifi_rssi.unwrap_or(0);

    // Build temperature string
    let _ = write!(buf, r#"{{"ts":{ts},"temp":"#);
    match temp {
        Some(t) => {
            let _ = write!(buf, "{t:.1}");
        }
        None => {
            let _ = write!(buf, "null");
        }
    }

    // Build remaining fields
    let _ = write!(
        buf,
        r#","mv":{mv},"vlv":"{vlv}","brt":{{"sts":"{brt_status}","vl":{brt_vol:.1},"spd":{brt_speed:.1}}},"wifi":{{"connected":{wifi_connected},"ap_mode":{is_ap_mode},"ssid":"{wifi_ssid_str}","rssi":{wifi_rssi_val},"ip":"{wifi_ip_str}"}}}}"#,
    );

    buf
}

/// Handle POST /api/command — execute a JSON command.
///
/// Parses the command, dispatches it, and returns the serialized response.
///
/// # Arguments
///
/// * `body` - Raw JSON bytes from the request body.
///
/// This is a stub that returns a placeholder response.
/// Full dispatch integration is in Phase 5.
// Stub: if-let is more readable than map_or_else with divergent branch bodies.
#[allow(clippy::option_if_let_else)]
pub fn handle_api_command(body: &[u8]) -> String<MAX_RESPONSE_SIZE> {
    // Try to parse the body as a CommandEnvelope
    let body_str = core::str::from_utf8(body).unwrap_or("");
    if let Ok(_envelope) =
        serde_json::from_str::<crate::application::command::CommandEnvelope>(body_str)
    {
        // Stub: acknowledge receipt. Full dispatch in Phase 5.
        let mut buf: String<MAX_RESPONSE_SIZE> = String::new();
        let _ = write!(buf, r#"{{"status":"ok","message":"received"}}"#);
        buf
    } else {
        let mut buf: String<MAX_RESPONSE_SIZE> = String::new();
        let _ = write!(buf, r#"{{"status":"error","message":"Invalid JSON"}}"#);
        buf
    }
}

/// Handle GET /api/valve/state — current valve position.
///
/// Accepts an optional position parameter (defaults to "input").
pub fn handle_valve_state() -> String<MAX_RESPONSE_SIZE> {
    // Default to "input" for now — Phase 5 will read actual hardware state
    let mut buf: String<MAX_RESPONSE_SIZE> = String::new();
    let _ = write!(buf, r#"{{"status":"ok","data":{{"position":"input"}}}}"#);
    buf
}

/// Handle POST /api/valve/set — set valve position.
// Stub: if-let is more readable than map_or_else with divergent branch bodies.
#[allow(clippy::option_if_let_else)]
pub fn handle_valve_set(body: &[u8]) -> String<MAX_RESPONSE_SIZE> {
    let body_str = core::str::from_utf8(body).unwrap_or("");
    if let Ok(val) = serde_json::from_str::<serde_json::Value>(body_str) {
        let pos = val["position"].as_str().unwrap_or("input");
        let mut buf: String<MAX_RESPONSE_SIZE> = String::new();
        let _ = write!(buf, r#"{{"status":"ok","data":{{"position":"{pos}"}}}}"#);
        buf
    } else {
        let mut buf: String<MAX_RESPONSE_SIZE> = String::new();
        let _ = write!(buf, r#"{{"status":"error","message":"Invalid JSON"}}"#);
        buf
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_api_ping() {
        let resp = handle_api_ping();
        assert_eq!(resp.as_str(), r#"{"status":"ok"}"#);
    }

    #[test]
    fn test_api_status_basic() {
        let resp = handle_api_status(
            false, None, None, None, true, None, 0, "in", "idle", 0.0, 0.0, 0,
        );
        assert!(resp.contains(r#""sts":"idle""#));
        assert!(resp.contains(r#""wifi""#));
    }

    #[test]
    fn test_api_status_with_wifi() {
        let resp = handle_api_status(
            true,
            Some("MyWiFi"),
            Some(-45),
            Some("192.168.1.100"),
            false,
            Some(23.5),
            1500,
            "out",
            "working",
            5.0,
            10.0,
            12345,
        );
        assert!(resp.contains(r#""sts":"working""#));
        assert!(resp.contains(r#""connected":true"#));
        assert!(resp.contains(r#""ssid":"MyWiFi""#));
        assert!(resp.contains(r#""rssi":-45"#));
        assert!(resp.contains(r#""ip":"192.168.1.100""#));
        assert!(resp.contains(r#""temp":23.5"#));
        assert!(resp.contains(r#""mv":1500"#));
        assert!(resp.contains(r#""vlv":"out""#));
    }

    #[test]
    fn test_api_status_fits_buffer() {
        let resp = handle_api_status(
            true,
            Some("A-Very-Long-SSID-Name-That-Should-Not-Exceed-MAX"),
            Some(-100),
            Some("192.168.1.255"),
            false,
            Some(-55.0),
            i16::MAX,
            "out",
            "working-error-disconnected",
            50.0,
            20.0,
            u64::MAX,
        );
        assert!(resp.len() <= MAX_RESPONSE_SIZE);
        assert!(resp.ends_with('}'));
    }

    #[test]
    fn test_api_command_valid() {
        let body = br#"{"id":1,"cmd":"serial.ping"}"#;
        let resp = handle_api_command(body);
        assert!(resp.contains(r#""status":"ok""#));
    }

    #[test]
    fn test_api_command_invalid_json() {
        let body = b"not json";
        let resp = handle_api_command(body);
        assert!(resp.contains(r#""status":"error""#));
    }

    #[test]
    fn test_valve_state() {
        let resp = handle_valve_state();
        assert!(resp.contains(r#""position":"input""#));
    }

    #[test]
    fn test_valve_set() {
        let body = br#"{"position":"output"}"#;
        let resp = handle_valve_set(body);
        assert!(resp.contains(r#""position":"output""#));
    }
}
