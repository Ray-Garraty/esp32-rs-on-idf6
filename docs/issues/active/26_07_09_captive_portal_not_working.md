---
type: Known Issue
title: Captive portal not working on phone connect
description: Phone connects to AP but no captive portal popup. HTTP redirect loop (ERR_TOO_MANY_REDIRECTS) or blank /wifi page (3 bytes served).
tags: [network, captive-portal, http, dns, wifi]
timestamp: 2026-07-09
status: active
---

# Captive Portal Not Working on Phone Connect

## Problem

When a phone (Xiaomi MIUI) connects to AP "EcoTiter-FCD2":
1. Phone gets DHCP IP (192.168.4.x) ✅
2. DNS queries are intercepted and answered -> resolved to 192.168.4.1 ✅
3. DNS queries are logged (query/response) ✅
4. HTTP 404 handler triggers (logged as "Redirecting to /wifi") ✅
5. **Captive portal popup does NOT appear** ❌
6. Manual navigation to `http://192.168.4.1/wifi` shows blank page (3 bytes served) ❌
7. Earlier: `ERR_TOO_MANY_REDIRECTS` when 404 handler redirected to `/` (dashboard with CDN deps)

## Attempted fixes (in chronological order, all failed to resolve the issue)

| # | Fix | File | Status |
|---|-----|------|--------|
| 1 | Added DNS diagnostic logging (RC-A) | `wifi.cpp` | ✅ Working — DNS queries logged |
| 2 | Fixed `DOMAIN_NAME_SERVER` param type `uint32_t*` -> `uint8_t` (RC-D) | `wifi.cpp` | ✅ Correct but not root cause |
| 3 | Increased `max_open_sockets` 4->5 (RC-C) | `http_server.cpp` | ❌ Raised to 13 later (like example) |
| 4 | Added `/ncsi.txt` route + catch-all 404 handler (RC-B, Arduino-style) | `http_server.cpp` | ❌ Replaced with different approach |
| 5 | Changed catch-all to `httpd_register_err_handler(HTTPD_404_NOT_FOUND)` | `http_server.cpp` | ❌ Cyclic redirects |
| 6 | Added explicit probe handlers (`/generate_204`, `/ncsi.txt`, etc.) | `http_server.cpp` | ❌ Cyclic redirects |
| 7 | Changed `Location: /wifi` to full URL with body text | `http_server.cpp` | ❌ Cyclic redirects |
| 8 | Changed redirect to `/` with `"Redirect to the captive portal"` body | `http_server.cpp` | ❌ `ERR_TOO_MANY_REDIRECTS` |
| 9 | Moved `esp_netif_create_default_wifi_ap()` before `esp_wifi_init()` | `wifi.cpp` | ❌ TCP still not accepting connections? |
| 10 | `max_open_sockets = 13` (as in official example) | `http_server.cpp` | ❌ Redirect loop fixed but /wifi blank |
| 11 | `CONFIG_LWIP_MAX_SOCKETS = 16` | `sdkconfig.defaults` | ✅ |
| 12 | Redirect to `/wifi` instead of `/` (break CDN resource loop) | `http_server.cpp` | ✅ Stop `ERR_TOO_MANY_REDIRECTS` but /wifi blank |

## Current state

- DNS: fully working, all queries answered, logged
- HTTP 404 handler: redirects to `/wifi` with body `"Redirect to the captive portal"` ✅
- No redirect loops ✅
- **`/wifi` page serves 3 bytes instead of full CAPTIVE_HTML (~1500 bytes)** ❌

This means `sizeof(CAPTIVE_HTML) - 1` in `webui.hpp` returns 3 instead of the actual HTML size. Likely the `constexpr auto` resolves to `const char*` (pointer) instead of `const char[N]` (array) in some build configuration, making `sizeof` return pointer size (4 or 8 on xtensa) instead of string length.

## Root cause (unconfirmed)

### Primary hypothesis: `/wifi` content size = 3 bytes
`sizeof(CAPTIVE_HTML)` returns pointer size (4 on xtensa) minus 1 = 3 instead of actual HTML length.

In `webui.hpp`:
```cpp
static constexpr auto CAPTIVE_HTML = R"htmlraw(<!DOCTYPE html>...)htmlraw";
// sizeof(CAPTIVE_HTML) should be string + 1 (null), but may be pointer size
```

The `std::string_view(CAPTIVE_HTML, sizeof(CAPTIVE_HTML) - 1)` stores wrong length.

### Secondary: TCP connections to port 80 may still fail
Earlier tests showed `ERR_CONNECTION_TIMED_OUT` before the init order fix. After fix #9 (netif before wifi_init), the 404 handler fires, which means TCP works. But `/wifi` with 3 bytes may be indistinguishable from a blank page to the phone's captive portal detection.

## Solution (not yet found)

Fix `/wifi` content size. Replace `sizeof(CAPTIVE_HTML) - 1` with explicit length or use `std::string_view` with `std::char_traits<char>::length`:

```cpp
// Option 1: explicit length
static constexpr size_t CAPTIVE_HTML_LEN = 1492; // count manually
// Option 2: use string_view literal
static constexpr std::string_view CAPTIVE_HTML = "...";
// Option 3: use strlen (constexpr in C++23 with consteval)
```

Once `/wifi` serves proper HTML, test:
1. `http://192.168.4.1/wifi` renders captive portal setup page
2. Phone shows captive portal popup on AP connect
3. WiFi credentials can be submitted via form

## Edge cases

- **Xiaomi MIUI** may use HTTPS-only captive portal detection (port 443). If `/wifi` renders correctly but popup still doesn't appear, need HTTPS server on port 443.
- **iOS** requires content body in 404 response (confirmed by ESP-IDF docs)
- **Windows NCSI** uses `/ncsi.txt` — caught by catch-all redirect
- CDN resources in dashboard (`/`) cause infinite redirect loops if 404 handler redirects to `/` — fixed by redirecting to `/wifi`

## Related

- Official ESP-IDF example: `examples/protocols/http_server/captive_portal/`
- ESP-IDF docs: `httpd_register_err_handler()`, `esp_netif_create_default_wifi_ap()`
- Stack overflow: `sizeof()` on `constexpr auto` string literal
