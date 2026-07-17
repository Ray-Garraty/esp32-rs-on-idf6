---
type: Known Issue
title: WebSocket session drops every 1-2s due to unread frame payload in ws_handler
description: frame.len initialized to 1024 instead of 0, causing httpd_ws_recv_frame to skip header parsing and leave payload unconsumed; next request finds misaligned TCP stream and fails, killing the session
tags: [websocket, http_server, httpd_ws_recv_frame, network]
timestamp: 2026-07-17
status: solved
resolved: 2026-07-17
---

# WebSocket session drops due to unread frame payload in ws_handler

## Problem

WebUI disconnects every 1-2 seconds: ADC readings disappear and control buttons deactivate. The browser logs show the connection status toggling between connected and disconnected in a steady cycle. The serial log confirms repeated WS session additions (`"WS session added for fd=..."`) for the same client.

Broadcast log shows intermittent:
```
W (13056) httpd_ws: httpd_ws_recv_frame: Failed to receive payload
```

## Root cause

**File:** `components/infrastructure/network/src/http_server.cpp:441-446`

```cpp
httpd_ws_frame_t frame{};
uint8_t buf[1024]{};
frame.payload = buf;
frame.len = sizeof(buf);   // <-- BUG: should be 0
frame.type = HTTPD_WS_TYPE_TEXT;
```

`httpd_ws_recv_frame_internal()` (ESP-IDF `httpd_ws.c:379`) interprets `frame->len` as follows:

> If frame len is 0, will get frame len from req. Otherwise regard frame len already achieved by calling httpd_ws_recv_frame before.

With `frame.len = 1024`, the function **skips parsing the frame header** (opcode, length, mask) and immediately returns `ESP_OK` with `frame->left_len = 0` — **without consuming any payload bytes from the socket**. The actual frame data remains in the TCP receive buffer.

On the next invocation (e.g., a browser WebSocket PING or the next broadcast cycle), the TCP stream is misaligned. `httpd_ws_recv_frame` fails with `"Failed to receive payload"`, the handler calls `removeSession(fd)`, and the client stops receiving broadcasts. After 4 seconds (`WS_TIMEOUT_MS = 4000`) the frontend marks the connection dead.

### Cycle summary

1. Client connects and subscribes
2. Text frame arrives → `ws_handler` reads header (by framework) but skips payload (by bug)
3. Next frame (PING from browser, or another text frame) arrives → TCP stream is misaligned
4. `httpd_ws_recv_frame` fails → `removeSession()` → broadcasts stop reaching client
5. Frontend timeout after 4s → `setConnectionStatus(false)` → buttons disabled, ADC values reset
6. Client reconnects → goto 1

## Solution

In `ws_handler` (`http_server.cpp`):

1. **Remove `frame.len = sizeof(buf)`** — `frame` is zero-initialized, `.len` is already 0.
2. **Pass `sizeof(buf)` as the third argument (`max_len`)** to `httpd_ws_recv_frame`, not `frame.len`.
3. **Do not remove the session on recv failure** — log a warning and return `ESP_OK`. Stale sessions are cleaned up by the broadcast loop which checks `httpd_ws_get_fd_info`.
4. **Keep session removal on WS CLOSE frame** — that path is legitimate.

```cpp
httpd_ws_frame_t frame{};
uint8_t buf[1024]{};
frame.payload = buf;
// frame.len = 0 (from value-initialization) — correct

esp_err_t err = httpd_ws_recv_frame(req, &frame, sizeof(buf));
if (err != ESP_OK) {
    ESP_LOGW(TAG, "ws_handler: recv failed (fd=%d)", fd);
    return ESP_OK;
}

if (frame.type == HTTPD_WS_TYPE_CLOSE) {
    if (fd >= 0 && req->user_ctx) {
        static_cast<HttpServer*>(req->user_ctx)->removeSession(fd);
    }
    return ESP_OK;
}

if (frame.len > 0) {
    size_t safeLen = std::min<size_t>(frame.len, sizeof(buf) - 1);
    buf[safeLen] = '\0';
    ESP_LOGI(TAG, "WS RX: %s", reinterpret_cast<char*>(buf));
}
```

## Edge cases

- **Zero-length text frames:** Handled by the `frame.len > 0` guard — no out-of-bounds write.
- **Client sends after server closes:** The broadcast loop already filters dead fds via `httpd_ws_get_fd_info`.
- **Malformed frames:** `httpd_ws_recv_frame` returns an error, handler returns `ESP_OK` without removing the session. The connection self-heals on the next successful frame.
- **Multiple WS clients:** Each fd is tracked independently in the `sessions_` array; the bug affects all equally and the fix applies to all equally.
