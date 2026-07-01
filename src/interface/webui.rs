//! Embedded WebUI assets served by the HTTP server.
//!
//! HTML, CSS, and JS files are embedded at compile time via `include_str!`.
//! These are referenced by `infrastructure::network::http_server` for route
//! handling.

#![forbid(unsafe_code)]
/// Main dashboard HTML page with WebSocket connection for live status display.
pub const INDEX_HTML: &str = include_str!("../webui/index.html");

/// Combined styling: style.css + theme.css (dark theme support).
pub const STYLE_CSS: &str = include_str!("../webui/style.css");

/// JS: Application state and config constants.
pub const STATE_JS: &str = include_str!("../webui/js/state.js");
/// JS: WebSocket client for real-time events (replaces legacy SSE).
pub const WS_JS: &str = include_str!("../webui/js/ws.js");
/// JS: DOM updates for hardware status, debug, stepper.
pub const UI_UPDATE_JS: &str = include_str!("../webui/js/ui-update.js");
/// JS: Log filtering, download, render.
pub const LOGS_JS: &str = include_str!("../webui/js/logs.js");
/// JS: Stepper motor controls (start/stop, direction, mode).
pub const STEPPER_JS: &str = include_str!("../webui/js/stepper.js");
/// JS: ADC calibration (5 points), burette volume cal, speed cal.
pub const CALIBRATION_JS: &str = include_str!("../webui/js/calibration.js");
/// JS: App init, theme toggle, sendCommand, toggleValve.
pub const INIT_JS: &str = include_str!("../webui/js/init.js");

/// Captive portal WiFi configuration page (legacy gradient design).
pub const WIFI_HTML: &str = include_str!("../webui/captive.html");
