---
type: Plan
title: WebUI Transfer — Replicate legacy C++ dashboard design in Rust firmware
description: >
  Replace the minimal stub WebUI (Phase 4) with the full-featured legacy WebUI:
  9 accordion sections, 7 JS modules, 2 CSS files, captive portal, and all
  HTTP/SSE endpoints required for correct operation. Based on legacy/data/.
tags: [webui, frontend, captive-portal, sse, api]
timestamp: 2026-07-01
status: pending
---

# WebUI Transfer -- Legacy C++ Dashboard Replication

## Summary

The current Rust firmware has a minimal stub WebUI (5 status cards, single
`app.js`, 53-line `index.html`) created in Phase 4 for SSE verification only.
The legacy C++ project (`legacy/data/`) has a full-featured dashboard with:
Bootstrap 5.3 accordion with 9 sections, 7 JS modules (~1000 lines), 2 CSS
files with dark theme support, a polished captive portal, and 4 SSE event types
(status, debug, log, limitsw).

This plan covers copying the legacy assets verbatim, adding the missing HTTP
routes to serve them, and enriching the SSE stream and API endpoints so the
dashboard displays live data correctly.

### Acceptance Criteria

| ID | Criterion | Verification |
|----|-----------|--------------|
| AC-01 | Legacy `index.html` served at `GET /` — 9 accordion sections render | Manual: browser shows temperature, ADC, valve, stepper, logs, SSE, ADC cal, pinout, burette cal sections |
| AC-02 | All 7 JS modules served at `/js/*.js` — no 404 in browser console | Manual: open browser devtools console at `/` — all JS loads |
| AC-03 | Dark/light theme toggle works | Manual: click moon/sun button, theme persists across reload |
| AC-04 | Captive portal shows legacy gradient design at `GET /wifi`; submit flow works correctly | Manual: open `/wifi` — gradient background, password toggle, spinner on submit; filling form and clicking "Connect" shows success |
| AC-05 | SSE `status` events update hardware cards in real time | Manual: temperature, electrode mV, valve, burette status update every ~10ms |
| AC-06 | SSE `debug` events update stepper driver panel | Manual: StallGuard value, threshold, motor busy, overheat indicator update |
| AC-07 | SSE `log` events populate the system log textarea | Manual: log messages appear in "System Log" section |
| AC-08 | SSE `limitsw` events update limit switch indicators | Manual: `hw-limit-min` and `hw-limit-max` show `✅ Activated` when triggered |
| AC-09 | `GET /api/logs` returns `{"entries":[...]}` with real log data | Inspection: endpoint returns log entries with `level` and `msg` fields |
| AC-10 | `GET /api/logs/download` returns plain-text log file | Manual: download button saves `ecotiter-logs.txt` |
| AC-11 | `POST /api/command` accepts all 17 legacy commands and returns `{"status":"ok","message":"received"}` (stub) | Automated: each command format accepted without error |
| AC-12 | No build errors, 0 clippy warnings, all host tests pass (~254 `#[test]` annotations, ~28 gated behind `cfg(target_arch = "xtensa")`) | Automated: pre-commit hook |
| AC-13 | Unsafe blocks remain at 23 (baseline unchanged) | Automated: `scripts/check_unsafe.py` |

## Scope

### In scope

1. Static file serving: 7 JS routes + combine CSS into single `/style.css`
2. Captive portal: replace inline `WIFI_HTML` with legacy `captive.html`
3. SSE enrichment: add `debug`, `log`, `limitsw` event types to main loop
4. API enrichment: `GET /api/logs` with real entries, `GET /api/logs/download`
5. `webui.rs`: update `include_str!` constants for all new assets
6. `http_server.rs`: register routes for JS files, `/api/logs/download`
7. SSE handler (http_server.rs): replace hardcoded `event: status` with dynamic event type from sender
8. Add `"success": true` to `/wifi/connect` response so legacy `captive.html:120` submit flow (`if (result.success)`) works
9. Fix log field names in `logger.rs`: `l`→`level`, `m`→`msg` to match legacy JS expectations
10. Patch confirmed JS/CSS bugs in copied assets: SSE race condition (G-19), `waitForBuretteIdle` timeout (G-20), broken `LOG_LEVEL_REGEX` (G-21), O(n²) unshift (G-22), dead `setState` (G-23), missing dark-theme CSS var (G-24)

### Out of scope (deferred to Phase 5)

- Wiring actual hardware state into SSE events (temp, ADC, valve, burette — still
  placeholder values)
- Command dispatch implementation (all commands return stub `{"message":"received"}`)
- Parameter name alignment between legacy JS and Rust Command enum (11 known
  mismatches documented in the gap analysis)
- Log ring buffer integration — `/api/logs` returns entries from a bounded buffer
  that is populated via the `log` SSE event path

### Gap Analysis Summary

The audit of legacy `data/` JS vs current Rust API revealed issues at three
severity levels:

#### Blocking (WebUI will not load)

| # | Issue | Fix |
|---|-------|-----|
| G-01 | 7 JS routes missing: `/js/state.js`, `/js/sse.js`, `/js/ui-update.js`, `/js/logs.js`, `/js/stepper.js`, `/js/calibration.js`, `/js/init.js` | Add routes in `http_server.rs` |
| G-02 | Captive portal is inline minimal stub, not legacy `captive.html` | Replace `WIFI_HTML` in `webui.rs` |

#### High (WebUI loads but features broken)

| # | Issue | Fix |
|---|-------|-----|
| G-03 | SSE `debug` event not emitted — stepper driver panel stays empty | Add `debug` SSE push in main loop + fix SSE handler to not hardcode `event: status` |
| G-04 | SSE `log` event not emitted — system log stays empty | Add `log` SSE push in main loop + fix SSE handler to not hardcode `event: status` |
| G-05 | SSE `limitsw` event not emitted — limit switch indicators stay "—" | Add `limitsw` SSE push in main loop + fix SSE handler to not hardcode `event: status` |
| G-06 | `GET /api/logs` returns `{"logs":[]}` — JS expects `{"entries":[{"level":"INFO","msg":"..."}]}` | Change JSON format, populate from log buffer; fix field names: `logger::get_entries_json()` uses `l`/`m` but JS expects `level`/`msg` |
| G-07 | `GET /api/logs/download` route does not exist | Add route in `http_server.rs` (handler in `rest_api.rs` following existing pattern) |
| G-08 | `GET /api/logs?limit=20` query parameter ignored | Parse query param in handler |
| G-18 | Captive portal submit flow broken: `captive.html:120` checks `result.success`, API returns `{"status":"ok"}` (no `success` field) | Add `"success":true` to `/wifi/connect` response JSON |
| G-19 | SSE double-init race condition: `pingServer` and `onerror` can spawn concurrent `initSse()` → leaked EventSource | Add `isReconnecting` guard flag; `pingServer` skips `initSse()` if reconnect already in progress |
| G-20 | `waitForBuretteIdle()` infinite `while(true)` without timeout — hangs forever if server never returns `"idle"` | Add `timeoutMs` parameter (default 60s), throw on expiry |
| G-21 | `LOG_LEVEL_REGEX = /[(\w+)]/` — character class, not capture group; `extractLevel()` always returns `'INFO'`, level filter broken | Replace with `LOG_LEVEL_REGEX = /\[(\w+)\]/` |

#### Low (JS/CSS quality issues in copied assets)

| # | Issue | Fix |
|---|-------|-----|
| G-22 | `loadInitialLogs()` O(n²) from `unshift` inside `forEach` | Replace with `map + reverse + spread`, single assignment |
| G-23 | `setState()` declared in `state.js` but never called anywhere | Remove dead `setState()` function |
| G-24 | `#sse-log-entries` border hardcoded `#ccc` — no dark-theme override | Replace with `var(--border-dark)` |

#### Medium (Phase 5 — command dispatch wiring)

| # | Issue | Rust expects | Legacy sends |
|---|-------|-------------|--------------|
| G-09 | `burette.moveSteps` — missing `direction`, `freq` | `steps`, `speed_hz` | `steps`, `direction`, `freq` |
| G-10 | `burette.moveToStop` — missing `direction`, `freq` | `speed_hz` | `direction`, `freq` |
| G-11 | `stallGuard.setThreshold` — param name mismatch | `value` | `threshold` |
| G-12 | `adc.cal.measure` — param meaning mismatch | `samples` | `ref_mv` |
| G-13 | `adc.cal.compute` — requires param, legacy sends none | `raw_mv` | (no params) |
| G-14 | `adc.cal.save` — requires params, legacy sends none | `a`, `b` | (no params) |
| G-15 | `burette.cal.calcVolume` — completely different params | `steps` | `mass_g`, `temp_c`, `pressure_kpa` |
| G-16 | `burette.cal.runSpeedSeq` — array vs scalar | `freq_count: u8` | `freqs: [...]` |
| G-17 | `burette.cal.calcSpeed` — completely different params | `steps_per_sec` | `measurements: [{freq_hz, speed_ml_min}]` |

## Steps / Execution log

### Step 1 — Add JS and CSS assets to `src/webui/` ✅

Done. All 7 JS files copied verbatim, CSS combined (style.css + theme.css),
`captive.html` copied, `index.html` resource paths adjusted.

**Files created:** `src/webui/js/state.js`, `src/webui/js/sse.js`,
`src/webui/js/ui-update.js`, `src/webui/js/logs.js`,
`src/webui/js/stepper.js`, `src/webui/js/calibration.js`,
`src/webui/js/init.js`, `src/webui/style.css`, `src/webui/captive.html`,
`src/webui/index.html`

### Step 1b — Patch confirmed JS/CSS bugs ✅

| G-ID | File | Status |
|------|------|--------|
| G-19 | `src/webui/js/sse.js` | ✅ Added `isReconnecting` guard, `pingServer` skips `initSse()` if already reconnecting |
| G-20 | `src/webui/js/calibration.js` | ✅ `waitForBuretteIdle()` accepts `timeoutMs` param (default 60s), throws on expiry |
| G-21 | `src/webui/js/logs.js` | ❌ NOT A BUG — legacy regex already correct `/\[(\w+)\]/` |
| G-22 | `src/webui/js/init.js` | ✅ O(n²) `forEach + unshift` → `map + reverse + spread` |
| G-23 | `src/webui/js/state.js` | ✅ Removed dead `setState()` function and JSDoc |
| G-24 | `src/webui/style.css` | ✅ `#ccc` → `var(--border-dark)` |

**Also extracted magic numbers in JS/CSS** to `CONFIG` in `state.js`:
`BURETTE_IDLE_TIMEOUT_MS`, `BURETTE_POLL_INTERVAL_MS`, `BC.DEFAULT_TEMP_C`,
`BC.DEFAULT_PRESSURE_KPA`, `BC.DEFAULT_SPEED_ML_MIN`, `BC.SECONDS_PER_MINUTE`,
`SG_THRESHOLD_MAX`, `LOG_DEFAULT_LIMIT`, `SSE_PING_THRESHOLD_MS`,
`SSE_RECONNECT_DELAY_MS`.

### Step 2 — Update `webui.rs` constants ✅

- Replaced `APP_JS` with 7 individual JS constants (`STATE_JS`…`INIT_JS`)
- Replaced inline `WIFI_HTML` with `include_str!("../webui/captive.html")`

### Step 3 — Register JS routes in `http_server.rs` ✅

- Added 9 static routes via `macro_rules! static_route!` (7 JS + `/` + `/style.css`)
- Removed old `/app.js` route and `src/webui/app.js` file

### Step 3a — Fix SSE handler for dynamic event types ✅

- Changed `SseEvent` string type → `SseMessage { event_type, data }` struct
- SSE frame now uses `event: {type}\ndata: {data}\n\n` instead of hardcoded `event: status`
- Updated `SseTx` type and all consumers (`main.rs`, `http_server.rs`)

### Step 3b — Fix captive portal submit flow ✅

- Added `"success":true` to `POST /wifi/connect` response JSON

### Step 4 — Fix log field names in `logger.rs` ✅

- Changed `"l":"INFO"` → `"level":"INFO"`, `"m":"..."` → `"msg":"..."` in `get_entries_json()`

### Step 5 — Add `/api/logs/download` route ✅

- Added `handle_api_logs_download()` in `rest_api.rs` (stub, returns empty string)
- Registered route `GET /api/logs/download` with `Content-Type: text/plain`

### Step 6 — Fix `/api/logs` response format ✅

- Changed response from `{"logs":[]}` → `crate::logger::get_entries_json(limit)`
- Default limit: `config::LOG_DEFAULT_LIMIT = 20`

### Step 7 — Enrich SSE events in `main.rs` ✅

- `status` event now uses real `tick_count * MAIN_LOOP_TICK_MS` as `ts` and real `mv` from ADC
- `debug` event pushed every tick with real `raw_mv` from ADC
- `limitsw` event pushed every `config::SSE_LIMITSW_INTERVAL_TICKS` ticks
- `log` event stub (Phase 5 wires ring buffer)
- Added `core::fmt::Write` import for dynamic JSON formatting in SSE push

### Step 8 — Verify ✅

| Check | Result |
|-------|--------|
| `cargo +esp build` | ✅ 0 errors (lib + bin) |
| `cargo +esp clippy -- -D warnings` | ✅ 0 warnings (lib + bin) |
| `cargo test --lib` | ✅ 226/226 pass |
| `scripts/check_unsafe.py` | ✅ 23 blocks (baseline unchanged) |
| Flash (`espflash flash`) | ✅ "Flashing has completed!" confirmed |
| Serial monitor (30s) | ✅ Boots to AP mode `192.168.4.1`, all routes registered, temp + ADC logs streaming |
| Browser dashboard | ⏳ Not yet verified (user continues validation next session) |

**Applied changes after flash output review:**
- Fixed `ts:0` → real `tick_count * MAIN_LOOP_TICK_MS` and `mv:0` → real ADC raw_mv in SSE pushes
- Extracted all remaining magic numbers to `config.rs`:
  `SSE_CHANNEL_CAPACITY`, `SSE_FRAME_OVERHEAD`, `LOG_DEFAULT_LIMIT`,
  `SSE_LIMITSW_INTERVAL_TICKS`, `LOG_INTERVAL_TICKS`

**Pending issues noted during validation:**
- `loadInitialLogs` JSON parse error at `/api/logs` (suspect `serde_json` `l`/`m` → `level`/`msg` mismatch or buffering issue — needs curl verification in next session)
- `G-21` was a false positive (legacy `logs.js` already has correct regex)

## Verification

| AC ID | Method | Details |
|-------|--------|---------|
| AC-01 | Manual | Open `http://ecotiter.local/` — all 9 accordion sections visible |
| AC-02 | Manual | Browser devtools console — no 404 or JS load errors |
| AC-03 | Manual | Click theme toggle (🌙/☀️) — page theme switches, reload persists |
| AC-04 | Manual | Open `http://ecotiter.local/wifi` — gradient background, styled form; submit form → success message (not error) |
| AC-05 | Manual | SSE `status` events: temperature, mV, valve, burette update every ~10ms |
| AC-06 | Manual | SSE `debug` events: stepper driver panel shows SG value, threshold, overheat |
| AC-07 | Manual | SSE `log` events: system log textarea populated |
| AC-08 | Manual | SSE `limitsw` events: limit switch badges update |
| AC-09 | Inspection | `curl http://esp32/api/logs` returns `{"entries":[...]}` |
| AC-10 | Manual | Click "Download" button — browser downloads `ecotiter-logs.txt` |
| AC-11 | Automated | `curl -X POST -d '{"id":1,"cmd":"serial.ping"}' http://esp32/api/command` → `{"status":"ok","message":"received"}` |
| AC-12 | Automated | Pre-commit: `cargo +esp build`, `cargo clippy`, `cargo test --lib` (all pass) |
| AC-13 | Automated | Pre-commit: `python3 scripts/check_unsafe.py` — 23 blocks |

## Files affected

### New files (9)

| File | Lines | Purpose |
|------|-------|---------|
| `src/webui/js/state.js` | 49 | `APP_STATE` + `CONFIG` |
| `src/webui/js/sse.js` | 168 | `EventSource`, 4 event types, SSE log table |
| `src/webui/js/ui-update.js` | 170 | DOM updates for hardware status, debug, stepper |
| `src/webui/js/logs.js` | 69 | Log filtering, download, render |
| `src/webui/js/stepper.js` | 75 | Stepper motor controls (start/stop, direction, mode) |
| `src/webui/js/calibration.js` | 429 | ADC cal (5 points), burette volume cal, speed cal |
| `src/webui/js/init.js` | 89 | App init, theme toggle, `sendCommand`, `toggleValve` |
| `src/webui/style.css` | 62 | Combined `style.css` + `theme.css` (dark theme) |
| `src/webui/captive.html` | 143 | Legacy captive portal with gradient and spinner |

### Modified files (8)

| File | Change |
|------|--------|
| `src/webui/index.html` | Replace stub (53 lines) with legacy dashboard (663 lines), adjust resource paths |
| `src/interface/webui.rs` | Replace `APP_JS` + inline `WIFI_HTML` with 7 JS constants + `captive.html` |
| `src/infrastructure/network/http_server.rs` | Add 7 JS routes + `/api/logs/download` route; fix SSE handler for dynamic event types; add `"success":true` to `/wifi/connect` response; refactor duplicate route registration into `static_route!` macro |
| `src/main.rs` | Add `debug`, `log`, `limitsw` SSE event pushes; use real `mv` and `ts` values in `status`/`debug` events |
| `src/infrastructure/logger.rs` | Rename JSON fields `l`→`level`, `m`→`msg` in `get_entries_json()`; add test module with 7 JSON validation tests |
| `src/interface/rest_api.rs` | Add `handle_api_logs()` (with `limit` param) and `handle_api_logs_download()` following existing pattern |
| `src/config.rs` | Add `SSE_CHANNEL_CAPACITY`, `SSE_FRAME_OVERHEAD`, `LOG_DEFAULT_LIMIT`, `SSE_LIMITSW_INTERVAL_TICKS`, `LOG_INTERVAL_TICKS` |
| `AGENTS.md` | Add flash safety note (`"Flashing has completed!"` required); add erase-flash command; add `type <tool>` lookup rule |

## Related Documentation

- Legacy WebUI source: `legacy/data/`
- Current WebUI stub: `src/webui/`
- HTTP server routes: `src/infrastructure/network/http_server.rs`
- REST API handlers: `src/interface/rest_api.rs`
- WebUI constants: `src/interface/webui.rs`
- Phase 4 report: `docs/plans/pending/26_06_30_phase4_network_report.md`
