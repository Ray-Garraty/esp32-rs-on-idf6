use serde::Serialize;

#[derive(Debug, Clone, Serialize)]
pub struct DeviceStatus {
    pub wifi_mode: String,
    pub wifi_connected: bool,
    pub wifi_ssid: Option<String>,
    pub wifi_rssi: Option<i32>,
    pub wifi_ip: Option<String>,
    pub ap_mode: bool,
}

/// Format status response matching C++ format_status_response JSON shape
pub fn format_status_response(
    connected: bool,
    ssid: Option<&str>,
    rssi: Option<i32>,
    ip: Option<String>,
    ap_mode: bool,
) -> String {
    use serde_json::json;

    let mode = if ap_mode {
        "AP"
    } else if connected {
        "STA"
    } else {
        "OFF"
    };

    let obj = json!({
        "wifi_mode": mode,
        "wifi_connected": connected,
        "wifi_ssid": ssid,
        "wifi_rssi": rssi,
        "wifi_ip": ip,
        "ap_mode": ap_mode,
        "temp": null,
        "mv": null,
        "vlv": "unk",
        "brt": {
            "sts": "idle",
            "vl": 0.0,
            "spd": 0.0,
        },
        "ts": 0,
    });

    serde_json::to_string(&obj).unwrap_or_default()
}
