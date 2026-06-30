//! Embedded WebUI assets served by the HTTP server.
//!
//! HTML, CSS, and JS files are embedded at compile time via `include_str!`.
//! These are referenced by `infrastructure::network::http_server` for route
//! handling.

/// Main dashboard HTML page with SSE connection for live status display.
pub const INDEX_HTML: &str = include_str!("../webui/index.html");

/// Basic styling for the dashboard page.
pub const STYLE_CSS: &str = include_str!("../webui/style.css");

/// JavaScript for SSE event handling and status display updates.
pub const APP_JS: &str = include_str!("../webui/app.js");

/// Captive portal WiFi configuration page (minimal inline HTML).
pub const WIFI_HTML: &str = r#"<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>EcoTiter — WiFi Setup</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: -apple-system, sans-serif; background: #f5f5f5; padding: 20px; }
        .container { max-width: 400px; margin: 40px auto; background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        h1 { color: #2E7D32; margin-bottom: 20px; text-align: center; }
        label { display: block; margin-bottom: 5px; color: #666; font-size: 0.9rem; }
        input { width: 100%; padding: 10px; margin-bottom: 15px; border: 1px solid #ddd; border-radius: 5px; font-size: 1rem; }
        button { width: 100%; padding: 12px; background: #4CAF50; color: white; border: none; border-radius: 5px; font-size: 1rem; cursor: pointer; }
        button:hover { background: #45a049; }
        .status { margin-top: 15px; text-align: center; color: #666; }
    </style>
</head>
<body>
    <div class="container">
        <h1>EcoTiter</h1>
        <h2 style="text-align:center;margin-bottom:20px;color:#666;font-size:1rem;">WiFi Setup</h2>
        <form id="wifi-form">
            <label for="ssid">Network Name (SSID)</label>
            <input type="text" id="ssid" name="ssid" placeholder="Enter WiFi SSID" required>
            <label for="password">Password</label>
            <input type="password" id="password" name="password" placeholder="Enter WiFi password">
            <button type="submit">Connect</button>
        </form>
        <p class="status" id="status-msg"></p>
    </div>
    <script>
        document.getElementById('wifi-form').addEventListener('submit', function(e) {
            e.preventDefault();
            var ssid = document.getElementById('ssid').value;
            var password = document.getElementById('password').value;
            var msg = document.getElementById('status-msg');
            msg.textContent = 'Connecting...';
            fetch('/wifi/connect', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ ssid: ssid, password: password })
            })
            .then(function(r) { return r.json(); })
            .then(function(data) {
                msg.textContent = data.message || 'Saved. Restarting...';
                setTimeout(function() { window.location.href = '/'; }, 2000);
            })
            .catch(function(err) {
                msg.textContent = 'Error: ' + err;
            });
        });
    </script>
</body>
</html>
"#;
