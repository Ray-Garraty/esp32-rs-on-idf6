//! ESP-IDF HTTP server with REST API, captive portal, SSE, and WebUI.
//!
//! # Architecture
//!
//! The HTTP server runs in a dedicated ESP-IDF task (stack size configured via
//! `config::HTTP_SERVER_STACK`). Routes are registered at construction time.
//!
//! - REST API handlers call `rest_api.rs` functions for JSON building.
//! - POST /api/command uses the stub path (Phase 5 adds full dispatch).
//! - GET /api/events uses the blocking handler pattern with `mpsc::sync_channel`
//!   and `ManuallyDrop<Response>` to stream SSE events from the HTTP server task.
//! - Captive portal routes redirect common probe URLs to /wifi.
//! - WebUI routes serve the embedded HTML dashboard.
//!
//! # Safety
//!
//! The SSE handler blocks inside the HTTP server task (12 KB stack) waiting for
//! events via `mpsc::Receiver::recv()`. This is safe because:
//! - ESP-IDF HTTP handlers run in a dedicated task (separate from main loop)
//! - The `httpd_req_t` pointer stays valid while the handler is running
//! - `ManuallyDrop` prevents the response from being dropped (closing the connection)
//! - `httpd_resp_send_chunk()` sends data without completing the response
//! - A zero-length chunk at the end marks the response as complete

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc;
use std::sync::{Arc, Mutex};

use core::mem::ManuallyDrop;

use embedded_svc::http::Method;
use esp_idf_svc::handle::RawHandle;
use esp_idf_svc::http::server::{Configuration, EspHttpServer};

use crate::config;
use crate::domain::memory::HTTP_POST_BUF_SIZE;
use crate::domain::memory::MAX_RESPONSE_SIZE;
use crate::errors::NetworkError;
use crate::infrastructure::network::wifi::WifiManager;
use crate::interface::rest_api;
use crate::interface::webui;

// `WifiManager` used with `'static` lifetime for `fn_handler` `Send` bound.
type WifiMgr = Arc<Mutex<WifiManager<'static>>>;

/// SSE event payload — fixed-size string matching `MAX_RESPONSE_SIZE`.
pub type SseEvent = heapless::String<{ MAX_RESPONSE_SIZE }>;
/// Shared sender for SSE events — main loop pushes, HTTP handler receives in blocking loop.
pub type SseTx = Arc<Mutex<Option<mpsc::SyncSender<SseEvent>>>>;

/// HTTP server wrapper.
pub struct HttpServer {
    /// The underlying ESP-IDF HTTP server.
    server: EspHttpServer<'static>,
    /// Set to `true` when WiFi credentials are saved and a restart is needed.
    restart_pending: Arc<AtomicBool>,
}

impl HttpServer {
    /// Create a new HTTP server and register all routes.
    ///
    /// # Arguments
    ///
    /// * `wifi_mgr` - Shared WiFi manager (`Arc<Mutex<>>`) for status/connect routes.
    /// * `sse_tx` - Shared sender for SSE events (main loop → HTTP handler).
    ///
    /// # Errors
    ///
    /// Returns `NetworkError::HttpServerInitFailed` if the server cannot be created.
    // Values are moved into closures; taking references would require extra Arcs.
    #[allow(clippy::needless_pass_by_value)]
    pub fn new(wifi_mgr: WifiMgr, sse_tx: SseTx) -> Result<Self, NetworkError> {
        let config = Configuration {
            stack_size: config::HTTP_SERVER_STACK,
            ..Default::default()
        };

        let server = EspHttpServer::new(&config).map_err(|_| NetworkError::HttpServerInitFailed)?;

        let restart_pending = Arc::new(AtomicBool::new(false));

        let mut http = Self {
            server,
            restart_pending: Arc::clone(&restart_pending),
        };

        http.register_captive_routes(Arc::clone(&wifi_mgr), restart_pending)?;
        http.register_api_routes(Arc::clone(&wifi_mgr), sse_tx)?;
        http.register_webui_routes()?;

        log::info!("HTTP: server started on port {}", config::HTTP_PORT);

        Ok(http)
    }

    /// Returns `true` if a restart is pending (WiFi credentials were saved via captive portal).
    pub fn restart_pending(&self) -> bool {
        self.restart_pending.load(Ordering::Acquire)
    }

    // ── Captive portal routes ───────────────────────────────────

    // Values are moved into closures; taking references would require extra Arcs.
    #[allow(
        clippy::needless_pass_by_value,
        clippy::option_if_let_else,
        clippy::manual_string_new
    )]
    fn register_captive_routes(
        &mut self,
        wifi_mgr: WifiMgr,
        restart_pending: Arc<AtomicBool>,
    ) -> Result<(), NetworkError> {
        // GET /wifi — captive portal WiFi configuration form
        self.server
            .fn_handler(
                "/wifi",
                Method::Get,
                move |request| -> Result<(), esp_idf_svc::io::EspIOError> {
                    let mut resp = request.into_ok_response()?;
                    resp.write(webui::WIFI_HTML.as_bytes())?;
                    resp.flush()?;
                    Ok(())
                },
            )
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        // POST /wifi/connect — save credentials and trigger restart
        let wifi_mgr_clone = Arc::clone(&wifi_mgr);
        let restart = Arc::clone(&restart_pending);
        self.server
            .fn_handler(
                "/wifi/connect",
                Method::Post,
                move |mut request| -> Result<(), esp_idf_svc::io::EspIOError> {
                    // Read body
                    let mut body = [0u8; HTTP_POST_BUF_SIZE];
                    let n = request.read(&mut body).unwrap_or(0);
                    let body_str = core::str::from_utf8(&body[..n]).unwrap_or("{}");

                    if let Ok(val) = serde_json::from_str::<serde_json::Value>(body_str) {
                        let ssid = val["ssid"].as_str().unwrap_or("");
                        let password = val["password"].as_str().unwrap_or("");

                        if !ssid.is_empty() {
                            // Lock WiFi and save credentials
                            if let Ok(mut wifi) = wifi_mgr_clone.lock() {
                                // Try connecting (blocking, but this is in HTTP task context)
                                if wifi.try_connect_sta(ssid, password) {
                                    WifiManager::save_credentials_to_nvs(ssid, password);
                                    restart.store(true, Ordering::Release);
                                }
                            }
                        }
                    }

                    let mut resp = request.into_ok_response()?;
                    let json = r#"{"status":"ok","message":"Credentials saved. Restarting..."}"#;
                    resp.write(json.as_bytes())?;
                    resp.flush()?;
                    Ok(())
                },
            )
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        // GET /wifi/status — return current WiFi status JSON
        let wifi_status_mgr = Arc::clone(&wifi_mgr);
        self.server
            .fn_handler("/wifi/status", Method::Get, move |request| -> Result<(), esp_idf_svc::io::EspIOError> {
                let json = if let Ok(wifi) = wifi_status_mgr.lock() {
                    let ap_mode = wifi.is_ap_mode();
                    let connected = wifi.is_connected();
                    let ssid = wifi.wifi_ssid().unwrap_or("").to_string();
                    let rssi = wifi.wifi_rssi().unwrap_or(0);
                    let ip = wifi
                        .wifi_ip()
                        .map_or_else(|| "".to_string(), |a| a.to_string());
                    format!(
                        r#"{{"ap_mode":{ap_mode},"connected":{connected},"ssid":"{ssid}","rssi":{rssi},"ip":"{ip}"}}"#
                    )
                } else {
                    r#"{"error":"wifi_locked"}"#.to_string()
                };

                let mut resp = request.into_ok_response()?;
                resp.write(json.as_bytes())?;
                resp.flush()?;
                Ok(())
            })
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        // Captive portal probe redirects (all → /wifi)
        let probes = [
            "/generate_204",
            "/hotspot-detect.html",
            "/ncsi.txt",
            "/connecttest.txt",
            "/gen_204",
        ];

        for path in &probes {
            self.server
                .fn_handler(
                    path,
                    Method::Get,
                    |request| -> Result<(), esp_idf_svc::io::EspIOError> {
                        let mut resp =
                            request.into_response(302, None, &[("Location", "/wifi")])?;
                        resp.flush()?;
                        Ok(())
                    },
                )
                .map_err(|_| NetworkError::HttpServerInitFailed)?;
        }

        Ok(())
    }

    // ── API routes ──────────────────────────────────────────────

    // Values are moved into closures; route registration is inherently verbose.
    #[allow(
        clippy::needless_pass_by_value,
        clippy::too_many_lines,
        clippy::option_if_let_else
    )]
    fn register_api_routes(
        &mut self,
        wifi_mgr: WifiMgr,
        sse_tx: SseTx,
    ) -> Result<(), NetworkError> {
        // GET /api/ping — health check
        self.server
            .fn_handler(
                "/api/ping",
                Method::Get,
                |request| -> Result<(), esp_idf_svc::io::EspIOError> {
                    let json = rest_api::handle_api_ping();
                    let mut resp = request.into_ok_response()?;
                    resp.write(json.as_bytes())?;
                    resp.flush()?;
                    Ok(())
                },
            )
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        // GET /api/status — full device status
        let status_wifi = Arc::clone(&wifi_mgr);
        self.server
            .fn_handler(
                "/api/status",
                Method::Get,
                move |request| -> Result<(), esp_idf_svc::io::EspIOError> {
                    let json = if let Ok(wifi) = status_wifi.lock() {
                        let wifi_connected = wifi.is_connected();
                        let wifi_ssid = wifi.wifi_ssid().map(ToString::to_string);
                        let wifi_rssi = wifi.wifi_rssi();
                        let wifi_ip = wifi.wifi_ip().map(|a| a.to_string());
                        let is_ap_mode = wifi.is_ap_mode();

                        rest_api::handle_api_status(
                            wifi_connected,
                            wifi_ssid.as_deref(),
                            wifi_rssi,
                            wifi_ip.as_deref(),
                            is_ap_mode,
                            None,   // temp — read from shared state in Phase 5
                            0,      // mv — read from ADC in Phase 5
                            "in",   // vlv — read from valve in Phase 5
                            "idle", // brt status
                            0.0,    // brt volume
                            0.0,    // brt speed
                            0,      // ts
                        )
                    } else {
                        rest_api::handle_api_status(
                            false, None, None, None, false, None, 0, "in", "idle", 0.0, 0.0, 0,
                        )
                    };

                    let mut resp = request.into_ok_response()?;
                    resp.write(json.as_bytes())?;
                    resp.flush()?;
                    Ok(())
                },
            )
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        // POST /api/command — execute a JSON command (stub path, Phase 5 adds dispatch)
        self.server
            .fn_handler(
                "/api/command",
                Method::Post,
                move |mut request| -> Result<(), esp_idf_svc::io::EspIOError> {
                    let mut body = [0u8; HTTP_POST_BUF_SIZE];
                    let n = request.read(&mut body).unwrap_or(0);
                    let json = rest_api::handle_api_command(&body[..n]);
                    let mut resp = request.into_ok_response()?;
                    resp.write(json.as_bytes())?;
                    resp.flush()?;
                    Ok(())
                },
            )
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        // POST /api/valve/set — set valve position
        self.server
            .fn_handler(
                "/api/valve/set",
                Method::Post,
                move |mut request| -> Result<(), esp_idf_svc::io::EspIOError> {
                    let mut body = [0u8; HTTP_POST_BUF_SIZE];
                    let n = request.read(&mut body).unwrap_or(0);
                    let json = rest_api::handle_valve_set(&body[..n]);
                    let mut resp = request.into_ok_response()?;
                    resp.write(json.as_bytes())?;
                    resp.flush()?;
                    Ok(())
                },
            )
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        // GET /api/valve/state — get current valve position
        self.server
            .fn_handler(
                "/api/valve/state",
                Method::Get,
                |request| -> Result<(), esp_idf_svc::io::EspIOError> {
                    let json = rest_api::handle_valve_state();
                    let mut resp = request.into_ok_response()?;
                    resp.write(json.as_bytes())?;
                    resp.flush()?;
                    Ok(())
                },
            )
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        // GET /api/events — SSE stream (blocking handler pattern)
        // Safety: the handler blocks inside the HTTP server task (12 KB stack)
        // waiting for events via mpsc channel. This is safe because:
        // 1. ESP-IDF HTTP handlers run in a dedicated task (separate from main loop)
        // 2. The httpd_req_t pointer stays valid while the handler is running
        // 3. ManuallyDrop prevents the response from being dropped
        // 4. httpd_resp_send_chunk sends data without completing the response
        let sse_tx_clone = sse_tx;
        self.server
            .fn_handler(
                "/api/events",
                Method::Get,
                move |mut request| -> Result<(), esp_idf_svc::io::EspIOError> {
                    // 1. Extract raw request handle BEFORE consuming the request
                    // Safety: handle() returns *mut c_void; cast to *mut httpd_req_t
                    // is safe because the ESP-IDF HTTP server owns the request and
                    // keeps it alive while the handler runs.
                    let raw_req = request
                        .connection()
                        .handle()
                        .cast::<esp_idf_sys::httpd_req_t>();

                    // 2. Send response headers — ManuallyDrop prevents closing connection
                    let resp = request.into_response(
                        200,
                        Some("OK"),
                        &[
                            ("Content-Type", "text/event-stream"),
                            ("Cache-Control", "no-cache"),
                        ],
                    )?;
                    let _resp = ManuallyDrop::new(resp);

                    // 3. Create channel for SSE events
                    let (tx, rx) = mpsc::sync_channel::<SseEvent>(8);

                    // 4. Store sender for main loop
                    if let Ok(mut guard) = sse_tx_clone.lock() {
                        *guard = Some(tx);
                    } else {
                        log::info!("SSE: mutex poisoned");
                        return Ok(());
                    }
                    log::info!("SSE: client connected");

                    // 5. Blocking loop: push events via httpd_resp_send_chunk
                    loop {
                        if let Ok(event_data) = rx.recv() {
                            // Build SSE frame: "event: status\ndata: {json}\n\n"
                            let mut frame: heapless::String<{ MAX_RESPONSE_SIZE + 64 }> =
                                heapless::String::new();
                            let _ = core::fmt::write(
                                &mut frame,
                                format_args!("event: status\ndata: {event_data}\n\n"),
                            );
                            let bytes = frame.as_bytes();

                            // Safety: raw_req is a valid *mut httpd_req_t obtained
                            // from the ESP-IDF HTTP server. The handler is still
                            // running so the request structure is alive.
                            // httpd_resp_send_chunk sends a chunk without completing
                            // the response, which is correct for SSE.
                            let ret = unsafe {
                                esp_idf_sys::httpd_resp_send_chunk(
                                    raw_req,
                                    bytes.as_ptr().cast(),
                                    bytes.len().cast_signed(),
                                )
                            };
                            if ret != esp_idf_sys::ESP_OK {
                                log::warn!("SSE: send_chunk error {ret}, client disconnected");
                                break;
                            }
                        } else {
                            log::info!("SSE: channel closed");
                            break;
                        }
                    }

                    // 6. Cleanup: clear sender, zero-length chunk to end response
                    if let Ok(mut guard) = sse_tx_clone.lock() {
                        *guard = None;
                    }
                    // Safety: zero-length chunk signals response complete to ESP-IDF.
                    // raw_req is still valid because the handler hasn't returned yet.
                    unsafe {
                        esp_idf_sys::httpd_resp_send_chunk(raw_req, core::ptr::null(), 0);
                    }
                    log::info!("SSE: client disconnected");
                    Ok(())
                },
            )
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        // GET /api/logs — drain log entries
        self.server
            .fn_handler(
                "/api/logs",
                Method::Get,
                move |request| -> Result<(), esp_idf_svc::io::EspIOError> {
                    // Phase 4: return empty array. Phase 5 will wire the log ring buffer.
                    let json = r#"{"logs":[]}"#;
                    let mut resp = request.into_ok_response()?;
                    resp.write(json.as_bytes())?;
                    resp.flush()?;
                    Ok(())
                },
            )
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        // DELETE /api/logs — clear log ring buffer
        self.server
            .fn_handler(
                "/api/logs",
                Method::Delete,
                |request| -> Result<(), esp_idf_svc::io::EspIOError> {
                    let json = r#"{"status":"ok"}"#;
                    let mut resp = request.into_ok_response()?;
                    resp.write(json.as_bytes())?;
                    resp.flush()?;
                    Ok(())
                },
            )
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        Ok(())
    }

    // ── WebUI routes ────────────────────────────────────────────

    fn register_webui_routes(&mut self) -> Result<(), NetworkError> {
        // GET / — main dashboard page
        self.server
            .fn_handler(
                "/",
                Method::Get,
                |request| -> Result<(), esp_idf_svc::io::EspIOError> {
                    let mut resp = request.into_ok_response()?;
                    resp.write(webui::INDEX_HTML.as_bytes())?;
                    resp.flush()?;
                    Ok(())
                },
            )
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        // GET /style.css
        self.server
            .fn_handler(
                "/style.css",
                Method::Get,
                |request| -> Result<(), esp_idf_svc::io::EspIOError> {
                    let mut resp = request.into_ok_response()?;
                    resp.write(webui::STYLE_CSS.as_bytes())?;
                    resp.flush()?;
                    Ok(())
                },
            )
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        // GET /app.js
        self.server
            .fn_handler(
                "/app.js",
                Method::Get,
                |request| -> Result<(), esp_idf_svc::io::EspIOError> {
                    let mut resp = request.into_ok_response()?;
                    resp.write(webui::APP_JS.as_bytes())?;
                    resp.flush()?;
                    Ok(())
                },
            )
            .map_err(|_| NetworkError::HttpServerInitFailed)?;

        Ok(())
    }
}
