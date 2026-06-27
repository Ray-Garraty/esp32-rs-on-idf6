use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use log::*;

use esp_idf_hal::io::EspIOError;
use esp_idf_svc::http::server::{EspHttpServer, Method};

use crate::config;
use crate::wifi::WifiManager;

pub type SharedWifi = Arc<Mutex<WifiManager>>;

#[allow(dead_code)]
pub struct WebServer {
    server: EspHttpServer<'static>,
    restart_pending: Arc<AtomicBool>,
}

impl WebServer {
    pub fn new(wifi_mgr: SharedWifi) -> Self {
        let mut server = EspHttpServer::new(&esp_idf_svc::http::server::Configuration {
            stack_size: 12288,
            ..Default::default()
        })
        .expect("EspHttpServer::new()");
        let restart_pending = Arc::new(AtomicBool::new(false));

        Self::register_captive_routes(&mut server, wifi_mgr.clone(), restart_pending.clone());
        Self::register_api_routes(&mut server, wifi_mgr.clone());
        Self::register_webui_routes(&mut server);

        info!("HTTP server started on port {}", config::HTTP_PORT);
        Self {
            server,
            restart_pending,
        }
    }

    pub fn restart_pending(&self) -> bool {
        self.restart_pending.load(Ordering::Relaxed)
    }

    fn register_captive_routes(
        server: &mut EspHttpServer<'static>,
        wifi_mgr: SharedWifi,
        restart_pending: Arc<AtomicBool>,
    ) {
        // Captive portal page
        let mgr = wifi_mgr.clone();
        server
            .fn_handler("/wifi", Method::Get, move |request| -> Result<(), EspIOError> {
                let _ = &mgr;
                let mut resp = request.into_ok_response()?;
                resp.write(CAPTIVE_PORTAL_HTML.as_bytes())?;
                Ok(())
            })
            .ok();

        // WiFi connect POST
        let mgr = wifi_mgr.clone();
        let rp = restart_pending.clone();
        server
            .fn_handler("/wifi/connect", Method::Post, move |mut request| -> Result<(), EspIOError> {
                let body = {
                    let (_hdr, conn) = request.split();
                    let mut buf = [0u8; 512];
                    let mut data = Vec::new();
                    loop {
                        match conn.read(&mut buf) {
                            Ok(0) => break,
                            Ok(n) => data.extend_from_slice(&buf[..n]),
                            Err(_) => break,
                        }
                    }
                    data
                };

                let body_str = String::from_utf8_lossy(&body);
                let parsed: serde_json::Value = match serde_json::from_str(&body_str) {
                    Ok(v) => v,
                    Err(_) => {
                        let mut resp = request.into_ok_response()?;
                        resp.write(b"{\"success\":false,\"message\":\"Invalid JSON\"}")?;
                        return Ok(());
                    }
                };

                let ssid = match parsed["ssid"].as_str() {
                    Some(s) => s,
                    None => {
                        let mut resp = request.into_ok_response()?;
                        resp.write(b"{\"success\":false,\"message\":\"Missing ssid\"}")?;
                        return Ok(());
                    }
                };
                let password = match parsed["password"].as_str() {
                    Some(p) => p,
                    None => {
                        let mut resp = request.into_ok_response()?;
                        resp.write(b"{\"success\":false,\"message\":\"Missing password\"}")?;
                        return Ok(());
                    }
                };

                info!("WiFi credentials received: {}", ssid);

                // Try to connect before saving
                let connected = {
                let mut wifi = mgr.lock().unwrap();
                wifi.stop();
                wifi.try_connect_sta(ssid, password)
                };

                if connected {
                    WifiManager::save_credentials_to_nvs(ssid, password);
                    info!("WiFi connected successfully, restarting in STA mode");
                    rp.store(true, Ordering::Relaxed);
                    let mut resp = request.into_ok_response()?;
                    resp.write(b"{\"success\":true,\"message\":\"Connected and saved\"}")?;
                } else {
                    warn!("WiFi connection failed for: {}", ssid);
                    let mut resp = request.into_ok_response()?;
                    resp.write(b"{\"success\":false,\"message\":\"Connection failed. Check SSID and password.\"}")?;
                }
                Ok(())
            })
            .ok();

        // WiFi status GET
        let mgr = wifi_mgr.clone();
        server
            .fn_handler("/wifi/status", Method::Get, move |request| -> Result<(), EspIOError> {
                let wifi = mgr.lock().unwrap();
                let status_json = serde_json::json!({
                    "ap_mode": wifi.is_ap_mode(),
                    "connected": wifi.is_connected(),
                    "ssid": wifi.wifi_ssid(),
                    "rssi": wifi.wifi_rssi(),
                    "ip": wifi.wifi_ip(),
                });
                drop(wifi);

                let mut resp = request.into_ok_response()?;
                let json = serde_json::to_string(&status_json).unwrap_or_default();
                resp.write(json.as_bytes())?;
                Ok(())
            })
            .ok();

        // Captive portal probe URLs → redirect to /wifi
        let probes = [
            "/generate_204",
            "/hotspot-detect.html",
            "/ncsi.txt",
            "/connecttest.txt",
            "/gen_204",
        ];
        for uri in &probes {
            server
                .fn_handler(uri, Method::Get, move |request| -> Result<(), EspIOError> {
                    let mut resp = request.into_response(302, Some("Found"), &[("Location", "/wifi")])?;
                    resp.write(b"")?;
                    Ok(())
                })
                .ok();
        }
    }

    fn register_api_routes(server: &mut EspHttpServer<'static>, wifi_mgr: SharedWifi) {
        // GET /api/status
        let mgr = wifi_mgr.clone();
        server
            .fn_handler("/api/status", Method::Get, move |request| -> Result<(), EspIOError> {
                let wifi = mgr.lock().unwrap();
                let status_obj = serde_json::json!({
                    "wifi_mode": format!("{:?}", wifi.mode()),
                    "wifi_connected": wifi.is_connected(),
                    "wifi_ssid": wifi.wifi_ssid(),
                    "wifi_rssi": wifi.wifi_rssi(),
                    "wifi_ip": wifi.wifi_ip(),
                    "ap_mode": wifi.is_ap_mode(),
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
                drop(wifi);

                let mut resp = request.into_ok_response()?;
                let json = serde_json::to_string(&status_obj).unwrap_or_default();
                resp.write(json.as_bytes())?;
                Ok(())
            })
            .ok();

        // GET /api/ping
        server
            .fn_handler("/api/ping", Method::Get, move |request| -> Result<(), EspIOError> {
                let mut resp = request.into_ok_response()?;
                resp.write(b"{\"status\":\"ok\"}")?;
                Ok(())
            })
            .ok();

        // GET /api/events (SSE)
        let mgr = wifi_mgr.clone();
        server
            .fn_handler("/api/events", Method::Get, move |mut request| -> Result<(), EspIOError> {
                let _body = {
                    let (_hdr, conn) = request.split();
                    let mut buf = [0u8; 512];
                    let mut data = Vec::new();
                    loop {
                        match conn.read(&mut buf) {
                            Ok(0) => break,
                            Ok(n) => data.extend_from_slice(&buf[..n]),
                            Err(_) => break,
                        }
                    }
                    data
                };

                let mut resp = request.into_ok_response()?;
                resp.write(b"data: {\"type\":\"connected\"}\n\n").ok();
                for _ in 0..60 {
                    let wifi = mgr.lock().unwrap();
                    let status_obj = serde_json::json!({
                        "wifi_mode": format!("{:?}", wifi.mode()),
                        "wifi_connected": wifi.is_connected(),
                        "wifi_ssid": wifi.wifi_ssid(),
                        "ap_mode": wifi.is_ap_mode(),
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
                    drop(wifi);

                    let json = serde_json::to_string(&status_obj).unwrap_or_default();
                    let msg = format!("event: status\ndata: {}\n\n", json);
                    if resp.write(msg.as_bytes()).is_err() {
                        break;
                    }
                    std::thread::sleep(Duration::from_millis(1000));
                }
                Ok(())
            })
            .ok();

        // POST /api/command
        server
            .fn_handler("/api/command", Method::Post, move |mut request| -> Result<(), EspIOError> {
                let body = {
                    let (_hdr, conn) = request.split();
                    let mut buf = [0u8; 512];
                    let mut data = Vec::new();
                    loop {
                        match conn.read(&mut buf) {
                            Ok(0) => break,
                            Ok(n) => data.extend_from_slice(&buf[..n]),
                            Err(_) => break,
                        }
                    }
                    data
                };

                let body_str = String::from_utf8_lossy(&body);
                let parsed: serde_json::Value = match serde_json::from_str(&body_str) {
                    Ok(v) => v,
                    Err(_) => {
                        let mut resp = request.into_ok_response()?;
                        resp.write(b"{\"status\":\"error\",\"message\":\"Invalid JSON\"}")?;
                        return Ok(());
                    }
                };

                let cmd = parsed["cmd"].as_str().unwrap_or("");
                info!("API command: {}", cmd);

                let response = match cmd {
                    "" => "{\"status\":\"error\",\"message\":\"Missing cmd\"}".to_string(),
                    _ => {
                        let result = serde_json::json!({
                            "status": "ok",
                            "message": format!("Command '{}' received (not yet implemented)", cmd),
                        });
                        serde_json::to_string(&result).unwrap_or_default()
                    }
                };

                let mut resp = request.into_ok_response()?;
                resp.write(response.as_bytes())?;
                Ok(())
            })
            .ok();

        // POST /api/valve/set
        server
            .fn_handler("/api/valve/set", Method::Post, move |mut request| -> Result<(), EspIOError> {
                let body = {
                    let (_hdr, conn) = request.split();
                    let mut buf = [0u8; 512];
                    let mut data = Vec::new();
                    loop {
                        match conn.read(&mut buf) {
                            Ok(0) => break,
                            Ok(n) => data.extend_from_slice(&buf[..n]),
                            Err(_) => break,
                        }
                    }
                    data
                };

                let body_str = String::from_utf8_lossy(&body);
                let parsed: serde_json::Value = match serde_json::from_str(&body_str) {
                    Ok(v) => v,
                    Err(_) => {
                        let mut resp = request.into_ok_response()?;
                        resp.write(b"{\"status\":\"error\",\"message\":\"Invalid JSON\"}")?;
                        return Ok(());
                    }
                };

                let position = parsed["position"].as_str().unwrap_or("input");
                let resp_json = serde_json::json!({
                    "status": "ok",
                    "data": { "position": position },
                });

                let mut resp = request.into_ok_response()?;
                let json = serde_json::to_string(&resp_json).unwrap_or_default();
                resp.write(json.as_bytes())?;
                Ok(())
            })
            .ok();

        // GET /api/valve/state
        server
            .fn_handler("/api/valve/state", Method::Get, move |request| -> Result<(), EspIOError> {
                let mut resp = request.into_ok_response()?;
                resp.write(b"{\"status\":\"ok\",\"data\":{\"position\":\"input\"}}")?;
                Ok(())
            })
            .ok();

        // GET /api/logs
        server
            .fn_handler("/api/logs", Method::Get, move |request| -> Result<(), EspIOError> {
                let mut resp = request.into_ok_response()?;
                resp.write(b"{\"total\":0,\"entries\":[]}")?;
                Ok(())
            })
            .ok();

        // DELETE /api/logs
        server
            .fn_handler("/api/logs", Method::Delete, move |request| -> Result<(), EspIOError> {
                let mut resp = request.into_ok_response()?;
                resp.write(b"{\"status\":\"ok\"}")?;
                Ok(())
            })
            .ok();

        // GET /api/logs/download
        server
            .fn_handler("/api/logs/download", Method::Get, move |request| -> Result<(), EspIOError> {
                let mut resp = request.into_ok_response()?;
                resp.write(b"No logs yet")?;
                Ok(())
            })
            .ok();

        // GET /api/nvs/status
        server
            .fn_handler("/api/nvs/status", Method::Get, move |request| -> Result<(), EspIOError> {
                let status_obj = serde_json::json!({
                    "freeHeap": 0,
                    "wifi": { "saved": false },
                });
                let mut resp = request.into_ok_response()?;
                let json = serde_json::to_string(&status_obj).unwrap_or_default();
                resp.write(json.as_bytes())?;
                Ok(())
            })
            .ok();
    }

    fn register_webui_routes(server: &mut EspHttpServer<'static>) {
        // Main dashboard
        server
            .fn_handler("/", Method::Get, move |request| -> Result<(), EspIOError> {
                let mut resp = request.into_ok_response()?;
                resp.write(crate::webui::INDEX_HTML.as_bytes())?;
                Ok(())
            })
            .ok();

        // CSS
        server
            .fn_handler("/css/style.css", Method::Get, move |request| -> Result<(), EspIOError> {
                let mut resp = request.into_response(200, Some("text/css"), &[])?;
                resp.write(crate::webui::STYLE_CSS.as_bytes())?;
                Ok(())
            })
            .ok();

        server
            .fn_handler("/css/theme.css", Method::Get, move |request| -> Result<(), EspIOError> {
                let mut resp = request.into_response(200, Some("text/css"), &[])?;
                resp.write(crate::webui::THEME_CSS.as_bytes())?;
                Ok(())
            })
            .ok();

        // JS
        server
            .fn_handler("/js/state.js", Method::Get, move |request| -> Result<(), EspIOError> {
                let mut resp = request.into_response(200, Some("application/javascript"), &[])?;
                resp.write(crate::webui::STATE_JS.as_bytes())?;
                Ok(())
            })
            .ok();

        server
            .fn_handler("/js/ui-update.js", Method::Get, move |request| -> Result<(), EspIOError> {
                let mut resp = request.into_response(200, Some("application/javascript"), &[])?;
                resp.write(crate::webui::UI_UPDATE_JS.as_bytes())?;
                Ok(())
            })
            .ok();

        server
            .fn_handler("/js/sse.js", Method::Get, move |request| -> Result<(), EspIOError> {
                let mut resp = request.into_response(200, Some("application/javascript"), &[])?;
                resp.write(crate::webui::SSE_JS.as_bytes())?;
                Ok(())
            })
            .ok();

        server
            .fn_handler("/js/logs.js", Method::Get, move |request| -> Result<(), EspIOError> {
                let mut resp = request.into_response(200, Some("application/javascript"), &[])?;
                resp.write(crate::webui::LOGS_JS.as_bytes())?;
                Ok(())
            })
            .ok();

        server
            .fn_handler("/js/stepper.js", Method::Get, move |request| -> Result<(), EspIOError> {
                let mut resp = request.into_response(200, Some("application/javascript"), &[])?;
                resp.write(crate::webui::STEPPER_JS.as_bytes())?;
                Ok(())
            })
            .ok();

        server
            .fn_handler("/js/calibration.js", Method::Get, move |request| -> Result<(), EspIOError> {
                let mut resp = request.into_response(200, Some("application/javascript"), &[])?;
                resp.write(crate::webui::CALIBRATION_JS.as_bytes())?;
                Ok(())
            })
            .ok();

        server
            .fn_handler("/js/init.js", Method::Get, move |request| -> Result<(), EspIOError> {
                let mut resp = request.into_response(200, Some("application/javascript"), &[])?;
                resp.write(crate::webui::INIT_JS.as_bytes())?;
                Ok(())
            })
            .ok();
    }

}

const CAPTIVE_PORTAL_HTML: &str = r#"<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>EcoTiter - Настройка WiFi</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
            padding: 20px;
        }
        .container {
            background: white;
            border-radius: 16px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            max-width: 400px;
            width: 100%;
            padding: 40px;
        }
        h1 { color: #333; margin-bottom: 10px; font-size: 24px; }
        .subtitle { color: #666; margin-bottom: 30px; font-size: 14px; }
        .form-group { margin-bottom: 20px; }
        label { display: block; margin-bottom: 8px; color: #555; font-weight: 500; font-size: 14px; }
        input[type="text"], input[type="password"] {
            width: 100%;
            padding: 12px 16px;
            border: 2px solid #e0e0e0;
            border-radius: 8px;
            font-size: 16px;
            transition: border-color 0.3s;
        }
        input:focus { outline: none; border-color: #667eea; }
        button {
            width: 100%;
            padding: 14px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            border: none;
            border-radius: 8px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            transition: transform 0.2s, box-shadow 0.2s;
        }
        button:hover { transform: translateY(-2px); box-shadow: 0 10px 20px rgba(102, 126, 234, 0.3); }
        button:active { transform: translateY(0); }
        button:disabled { opacity: 0.5; cursor: not-allowed; transform: none; }
        #toggle-password.active { background: #667eea !important; border-color: #667eea !important; color: white !important; }
        .status { margin-top: 20px; padding: 12px; border-radius: 8px; font-size: 14px; display: none; }
        .status.info { background: #e3f2fd; color: #1976d2; display: block; }
        .status.success { background: #e8f5e9; color: #388e3c; display: block; }
        .status.error { background: #ffebee; color: #d32f2f; display: block; }
        .spinner {
            display: inline-block;
            width: 16px; height: 16px;
            border: 2px solid #f3f3f3;
            border-top: 2px solid #667eea;
            border-radius: 50%;
            animation: spin 1s linear infinite;
            margin-right: 8px;
            vertical-align: middle;
        }
        @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
    </style>
</head>
<body>
    <div class="container">
        <h1>🌐 EcoTiter</h1>
        <p class="subtitle">Настройка подключения к WiFi сети</p>
        <form id="wifi-form">
            <div class="form-group">
                <label for="ssid">Название WiFi сети (SSID)</label>
                <input type="text" id="ssid" name="ssid" required placeholder="Введите SSID сети">
            </div>
            <div class="form-group">
                <label for="password">Пароль</label>
                <div style="display: flex; gap: 8px;">
                    <input type="password" id="password" name="password" required placeholder="Введите пароль" style="flex: 1;">
                    <button type="button" id="toggle-password" onclick="togglePasswordVisibility()"
                            style="width: 48px; min-width: 48px; font-size: 20px; padding: 8px; background: #e0e0e0; border: 2px solid #e0e0e0; border-radius: 8px; cursor: pointer;">👁</button>
                </div>
            </div>
            <button type="submit" id="submit-btn">Подключиться</button>
        </form>
        <div id="status" class="status"></div>
    </div>
    <script>
        function togglePasswordVisibility() {
            const pw = document.getElementById('password');
            const btn = document.getElementById('toggle-password');
            pw.type = pw.type === 'password' ? 'text' : 'password';
            btn.classList.toggle('active', pw.type === 'text');
        }

        const form = document.getElementById('wifi-form');
        const submitBtn = document.getElementById('submit-btn');
        const statusDiv = document.getElementById('status');
        form.addEventListener('submit', async (e) => {
            e.preventDefault();
            const ssid = document.getElementById('ssid').value;
            const password = document.getElementById('password').value;
            submitBtn.disabled = true;
            submitBtn.innerHTML = '<span class="spinner"></span> Подключение...';
            statusDiv.className = 'status info';
            statusDiv.innerHTML = 'Пробуем подключиться к сети...';
            try {
                const response = await fetch('/wifi/connect', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({ssid: ssid, password: password})
                });
                const result = await response.json();
                if (result.success) {
                    statusDiv.className = 'status success';
                    statusDiv.innerHTML = '✓ Подключение успешно! Сохранение настроек и перезагрузка...';
                    submitBtn.innerHTML = '✓ Готово!';
                    setTimeout(() => {
                        statusDiv.innerHTML += '<br><br>Устройство перезагружается. Подождите 10-15 секунд.';
                    }, 2000);
                    return;
                } else {
                    statusDiv.className = 'status error';
                    statusDiv.innerHTML = '✗ Ошибка: ' + (result.message || 'Не удалось подключиться');
                    submitBtn.disabled = false;
                    submitBtn.innerHTML = 'Подключиться';
                }
            } catch (error) {
                statusDiv.className = 'status error';
                statusDiv.innerHTML = '✗ Соединение прервано. Если подключение было успешным, устройство перезагружается. Подождите 10-15 секунд.';
                submitBtn.disabled = false;
                submitBtn.innerHTML = 'Подключиться';
            }
        });
    </script>
</body>
</html>"#;
