---
type: Architecture Reference
title: WiFi Subsystem Specification
description: Init order, IP visibility timing, coexistence, and captive portal architecture
tags: [wifi, ap, sta, init-order, ip, captive-portal]
timestamp: 2026-07-13
---

# WiFi Subsystem Specification

## Overview

The ESP32-S3 first attempts to connect as a STA to a saved home/office WiFi network. If no credentials are saved or the connection fails, it falls back to Soft-AP mode on `192.168.4.1/24` for device configuration (captive portal). Both modes can run concurrently during fallback.

**Design constraint:** An IP address (STA or AP) MUST be visible in the serial log **within 30 seconds** of `BOOT OK`. No other subsystem (motor, temperature, sensors) may block or delay WiFi initialization.

---

## Init Order

### Init order

```
app_main()
  → xTaskCreate(net_owner)   // WiFi in another task
  → xTaskCreate(motor)       // motor in another task

netTaskEntry()
  → wifi.init()
  → tryStartSTA()             // blocking up to 10s — starts WiFi in STA mode
  ├── [success] → STA_CONNECTED → HTTP → BLE (no AP)
  └── [failure] → startAP()  // fallback: AP started for captive portal
  → HttpServer.init()
  → BLE.init()
```

`tryStartSTA()` blocks in `net_owner` only — other tasks (motor, temp, main) run independently.
`waitForSTA(timeoutMs)` uses FreeRTOS event group (non-CPU-busy wait).

All subsystems are independent FreeRTOS tasks. No subsystem blocks or waits for another.

### IP Logging — Requirements

| Event | Log line | When |
|-------|----------|------|
| STA started | `Connecting to STA: <ssid>` | ≤ 3 s after BOOT OK |
| STA got IP | `STA got IP: 192.168.1.x` | ≤ 13 s after BOOT OK |
| STA failed | `STA connection timeout/failure` | ≤ 13 s after BOOT OK |
| AP started (fallback) | `AP started: EcoTiter-XXXX (192.168.4.1)` | ≤ 15 s after BOOT OK |

The user MUST see at least one IP address in the log within 25 seconds of BOOT OK under all conditions:

| Scenario | IP visible in 25s | Source |
|----------|-------------------|--------|
| No STA credentials saved | ✅ `192.168.4.1` | AP fallback after 10s timeout |
| STA credentials saved, connects | ✅ STA IP | `IP_EVENT_STA_GOT_IP` |
| STA credentials saved, fails | ✅ `192.168.4.1` | AP fallback after 10s timeout |

---

## Dual-Mode Architecture

### STA Mode (first attempt)

- Init order: `tryStartSTA()` → starts WiFi in `WIFI_MODE_STA`, reads NVS credentials, calls `esp_wifi_connect()`, **blocks up to 10s** for result via event group
- If connected: no AP started; HTTP server binds to STA IP (`0.0.0.0:80`)
- `wifi.process()` in net_owner loop handles reconnection after STA drop
- On `IP_EVENT_STA_GOT_IP`: log IP, start mDNS
- On `WIFI_EVENT_STA_DISCONNECTED`: log disconnect, auto-reconnect

### Credential Storage (NVS)

Up to 5 WiFi networks can be saved. On boot, `tryStartSTA()` tries each saved
network in order until one connects. The captive portal's `POST /wifi/connect`
handler saves new credentials via `connectSTA()`.

**NVS keys** (namespace `wifi`):

| Key | Value | Max length |
|-----|-------|-----------|
| `ssid_0` … `ssid_4` | SSID (UTF-8) | 32 bytes each |
| `password_0` … `password_4` | WPA2 password | 64 bytes each |
| `count` | Number of saved networks (0–5) | 1 byte |

**Save policy (`connectSTA` → `saveCredentials`):**
1. If the SSID already exists in slots 0–4 → update its password, move to front
2. If `count < 5` → append as new entry, increment `count`
3. If `count == 5` → replace the least recently used slot (FIFO eviction)
4. Rewrite `count` after every change

**Load policy (`tryStartSTA`):**
1. Read `count` from NVS. If 0 → return false (no credentials).
2. For each slot 0..count-1: read `ssid_N`, `password_N`, attempt connection.
3. On first successful connection → return true.
4. If none connect → return false (caller starts AP fallback).

**Design notes:**
- All NVS writes happen in `net_owner` task context (no cross-task writes).
- NVS is committed (`nvs_commit`) after each write to prevent data loss on power loss.
- Maximum 5 networks prevents NVS wear (NVS flash has ~100k erase cycles; 5 networks × infrequent writes is safe).

### AP Mode (fallback, only if STA fails)

- Activated by `startAP()` when `tryStartSTA()` fails (no credentials or timeout)
- SSID: `EcoTiter-{MAC}` (last 4 hex digits)
- Password: `ecotiter123` (from `config::AP_PASSWORD`)
- IP: `192.168.4.1/24`
- DHCP server enabled: leases in `192.168.4.x`
- Captive portal: DHCP option 114 → `http://192.168.4.1/wifi`
- DNS server: resolves `ecotiter.local` → `192.168.4.1`
- If STA later connects while AP is active → AP stops automatically (existing behaviour via `IP_EVENT_STA_GOT_IP` handler)

### mDNS

- Registered after IP assignment (`IP_EVENT_STA_GOT_IP` or immediately after AP fallback start)
- Hostname: `ecotiter`
- Resolves to AP IP (`192.168.4.1`) or STA IP, whichever is reachable from the client's network

---

## Coexistence

- `CONFIG_FREERTOS_UNICORE=n` (GR-12): WiFi ISR on CPU0, RMT on CPU1 — no conflict
- `ESP_COEX_PREFER_BALANCE` (GR-4): no BT preference, fair RF sharing
- WiFi RX buffer count: `WIFI_DYNAMIC_RX_BUFFER_NUM=6`, `WIFI_RX_BA_WIN=6` (matched to avoid `#error` in `wifi_init.c`)

### Interaction with Other Subsystems

| Subsystem | Concurrent with WiFi init? | Notes |
|-----------|---------------------------|-------|
| **RMT (stepper)** | ✅ Yes | Motor task runs independently; no WiFi dependency |
| **HTTP server** | ✅ Yes | Starts after init complete (STA or AP mode) |
| **BLE** | ✅ Yes | Init after HTTP (GR-3), no dependency on WiFi mode |
| **OneWire (temp)** | ✅ Yes | Bitbang on GPIO6, non-DMA |
| **ADC (pH)** | ✅ Yes | Oneshot read, no DMA |

---

## Verification

### Automated (CI)

| Test | What it checks |
|------|---------------|
| `serial_api_test.py` | WiFi-independent, tests serial command/response |
| `http_api_test.py` | HTTP on 192.168.4.1 — requires client connected to `EcoTiter-*` AP |
| `ble_test.py` | BLE NUS — requires BT adapter on test machine |

### Manual

```bash
# Flash and monitor for 30s
scripts/idf.sh flash
timeout 30 python3 scripts/monitor.py

# Check log for IP within 30s of BOOT OK
rg "AP started|STA got IP|No saved WiFi" logs/serial_*.log

# Connect to AP and test HTTP
curl http://192.168.4.1/api/ping
curl http://192.168.4.1/wifi
mDNS: curl http://ecotiter.local/api/ping
```

### Acceptance Criteria

1. ✅ `rg "STA got IP"` in serial log within 13 s of BOOT OK (if credentials saved + connectable network)
2. ✅ `rg "AP started.*192.168.4.1"` in serial log within 15 s of BOOT OK (if no STA credentials or timeout)
3. ✅ `rg "STA connection timeout"` in serial log within 13 s (if credentials saved but network unreachable)
4. ✅ `curl http://192.168.4.1/api/ping` returns `{"status":"ok"}` (AP fallback mode)
5. ✅ `curl http://<STA_IP>/api/ping` returns `{"status":"ok"}` (STA mode)
6. ✅ Motor homing concurrent with WiFi init — no RTCWDT_RTC_RST, no TWDT panic

---

## Related Documents

| Document | Link |
|----------|------|
| Project refs (threads, stack budgets, GPIO) | [project.md](project.md) |
| Watchdog specification | [watchdog_spec.md](watchdog_spec.md) |
| LL-044: Homing + WiFi AP interrupt storm (obsolesced by dual-core) | [../lessons_learned/LL-044.yaml](../lessons_learned/LL-044.yaml) |
| GR-12: Dual-core mandatory | [../../AGENTS.md](../../AGENTS.md) |
| Coding style (RAII, error handling) | [coding_style.md](coding_style.md) |
