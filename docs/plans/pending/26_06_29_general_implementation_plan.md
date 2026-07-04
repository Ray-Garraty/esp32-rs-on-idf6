---
type: Plan
title: General Implementation Plan — esp32-rs-on-idf6 Firmware (Rust, ESP-IDF v6)
description: Full-scope build plan for production-grade Rust firmware from legacy C++ business logic. All changes scoped to src/. LittleFS logging excluded.
tags: [plan, implementation, firmware, esp32, production, audit]
timestamp: 2026-06-29
status: active
audit_date: 2026-07-03
last_session: 2026-07-03
phases_completed: [0, 1, 2]
phases_partial: [3]
phases_implemented_ahead: [4, 5]
---

# General Implementation Plan - esp32-rs-on-idf6 Firmware (Rust, ESP-IDF v6)

## Phase Completion Status

| Phase | Status | Date | Report / Notes |
|-------|--------|------|----------------|
| 0 — Scaffold + Build Pipeline | ✅ COMPLETED | 2026-06-30 | `docs/plans/completed/26_06_30_phase0_scaffold_report.md`. Minor: brownout via `sdkconfig`, not `WRITE_PERI_REG`. |
| 1 — Domain Pure Business Logic | ✅ COMPLETED | 2026-06-30 | `docs/plans/completed/26_06_30_phase_1_domain.md`. Minor: `calculate_speed_calibration()` uses OLS-through-mean (legacy-compat), not OLS-through-origin as described. |
| 2 — Infrastructure Hardware Drivers | ✅ COMPLETED | 2026-06-30 | `docs/plans/completed/26_06_30_phase2_infrastructure_report.md`. **Gaps:** ADC stabilization logic not implemented; `stallguard` NVS namespace not wired. |
| 3 — Application | ⚠️ PARTIAL | 2026-07-03 | **Fixed 2026-07-03:** 5 command param mismatches ✅, `panic!()` removed ✅, `DELETE /api/logs` wired ✅, `stallguard` NVS wired ✅, build.rs clippy fixed ✅. **Remaining:** REST stubs (`/api/command`, `/api/valve/*`), pending cal state machines, Homing SM. |
| 4 — Network | ✅ IMPLEMENTED (ahead) | 2026-07-03 | WiFi (AP/STA/DNS/captive portal), BLE (NUS, zombie defense, coexistence) all functional. **WebSocket restored 2026-07-03** from commit `08c0cab` — `CONFIG_HTTPD_WS_SUPPORT=y` re-enabled, `broadcast_websocket_event()` functional, `WsSender` with session-ID tracking. Minor: BLE notify thread body is TODO stub. |
| 5 — Integration | ✅ IMPLEMENTED (ahead) | 2026-07-03 | `motor_task.rs` (fill/empty/dose/rinse/stop/emergencyStop), full main loop (ADC, UART, WiFi, BLE, transport SM, watchdog, LED). Homing at boot implemented (but as hardcoded sequence, not SM). PendingOpsManager + 60s watchdog present. |
| 6 — TMC2209 UART Driver | ⏳ DEFERRED | — | Files not created (expected). |
| 7 — Tauri Client | ⏳ PENDING | — | Separate repo — no firmware-side code required per plan. |

## Audit Results (2026-07-03)

Codebase audit conducted via `explore` sub-agent reviewing all `src/` files against plan items. Full report in agent output (task: `ses_0d824890cffeTdqNhmUVMcVt2P`). **245 total tests** (as of 2026-07-03 session end).

### Key Gaps (must fix before production)

| # | Issue | Severity | Status | File(s) |
|---|-------|----------|--------|---------|
| 1 | **WebSocket `/ws/stream` was no-op** — `broadcast_websocket_event()` did nothing | 🔴 CRITICAL | ✅ **FIXED 2026-07-03** — restored from `08c0cab`, `CONFIG_HTTPD_WS_SUPPORT=y` re-enabled | `infrastructure/network/http_server.rs` |
| 2 | **5 command param signatures deviate from frozen Serial API**: `cal.calcVolume`, `cal.calcSpeed`, `system.getFormattedLogs`, `system.readLog`, `burette.moveToStop` | 🔴 CRITICAL | ✅ **FIXED 2026-07-03** — all 5 variants updated, handlers wired to domain functions | `application/command.rs`, `application/handlers/` |
| 3 | **REST handlers are stubs** — `/api/command`, `/api/valve/set`, `/api/valve/state` return placeholder responses, don't call real dispatch/drivers | 🔴 CRITICAL | ⏳ PENDING | `interface/rest_api.rs` |
| 4 | **Pending calibration state machines missing** — `PENDING_CAL_DOSE`, `PENDING_CAL_SPEED`, `PENDING_CAL_SPEED_SEQ` not modelled | 🟡 MAJOR | ⏳ PENDING | `application/state_machine.rs` |
| 5 | **ADC stabilization logic not implemented** — plan describes 10-attempt, 5mV-window, 32-sample median | 🟡 MAJOR | ⏳ PENDING | `infrastructure/drivers/adc.rs` |
| 6 | **`panic!()` in production code** — 4 violations (main.rs:300, 314 channel disconnect panics) | 🟡 MAJOR | ✅ **FIXED 2026-07-03** — replaced with `log::error!` + `std::process::exit(1)` | `main.rs` |
| 7 | **BLE notify thread body is TODO** — notifications not sent | 🟡 MAJOR | ⏳ PENDING | `infrastructure/network/ble.rs` |
| 8 | **Homing not a state machine** — hardcoded in main.rs instead of `state_machine.rs` | 🟢 MINOR | ⏳ PENDING | `main.rs` |
| 9 | **`stallguard` NVS namespace not wired** — no read/write code for threshold | 🟢 MINOR | ✅ **FIXED 2026-07-03** — `stallguard_read_threshold()` / `stallguard_write_threshold()` added, handlers wired | `infrastructure/storage/nvs.rs` |
| 10 | **`DELETE /api/logs` stub** — returns ok but doesn't clear buffer | 🟢 MINOR | ✅ **FIXED 2026-07-03** — `logger::clear_entries()` added, `rest_api::handle_api_logs_clear()` wired | `logger.rs`, `interface/rest_api.rs` |
| 11 | **build.rs clippy errors** — `items_after_statements`, `needless_raw_string_hashes` | 🟢 MINOR | ✅ **FIXED 2026-07-03** — const' moved above statements, `#[allow]` for false-positive raw string hashes | `build.rs` |

### Verification Requirements Status

| Requirement | Status | Notes |
|-------------|--------|-------|
| No `unwrap()` in production code | ✅ PASS | Enforced by `lib.rs` lint |
| No `expect()` in production code | ✅ PASS | Enforced by `lib.rs` lint |
| No `panic!()` in production code | ✅ PASS | **FIXED 2026-07-03** — 4 violations removed |
| No `todo!()` / `unreachable!()` in production | ✅ PASS | Zero occurrences |
| No `esp-idf-*` in `domain/` layer | ✅ PASS | Verified |
| `heapless` used in hot paths | ✅ PASS | Enforced by `clippy.toml` |
| Commands match frozen `SERIAL_API.md` | ✅ PASS | **FIXED 2026-07-03** — all 5 violations resolved |
| `emergencyStop` remains public | ✅ PASS | Variant is public |

### Session 2026-07-03 — Fixes Applied

| # | Fix | Files Changed | Verified |
|---|-----|---------------|----------|
| 1 | `panic!()` → `log::error!` + `exit(1)` | `main.rs:300,314` | ✅ build + flash + 30s smoke |
| 2 | `DELETE /api/logs` — `logger::clear_entries()` + `rest_api::handle_api_logs_clear()` | `logger.rs`, `rest_api.rs`, `http_server.rs` | ✅ build + flash + 30s smoke |
| 3 | `build.rs` clippy — const' moved above `let src`, `#[allow(needless_raw_string_hashes)]` | `build.rs` | ✅ clippy-host + clippy-xtensa pass |
| 4 | `stallguard` NVS — `stallguard_read_threshold()` / `stallguard_write_threshold()` + handlers wired | `nvs.rs`, `sensors.rs` | ✅ build + flash + 30s smoke |
| 5 | 5 frozen API commands — `cal.calcVolume` (gravimetric), `cal.calcSpeed` (OLS), `moveToStop` (dir+speed), `getFormattedLogs`/`readLog` (params) | `command.rs`, `burette_cal.rs`, `burette_ops.rs`, `system.rs`, `burette.rs`, `motor_task.rs`, `dispatch.rs` | ✅ 245 tests pass + build + flash + 30s smoke |
| 6 | WebSocket restored — `CONFIG_HTTPD_WS_SUPPORT=y`, `WsSender` with session-ID tracking, `broadcast_websocket_event()` functional | `http_server.rs`, `sdkconfig.defaults`, `main.rs` | ✅ build + flash + 30s smoke |
| 7 | `scripts/build.sh` created — ESP toolchain setup + cargo dispatch | `scripts/build.sh` | ✅ all modes tested |

# General Implementation Plan

## References (read-only)

| Source | Path | Purpose |
|---|---|---|
| Legacy C++ | `legacy/` | Business logic reference (planner, calibration, transport SM, handlers, TMC2209) |
| Rust prototype | `prototype/` | Feasibility validation (RMT config, WiFi AP DHCP fix, NimBLE patch, sdkconfig) |
| Serial API contract | `C:\Users\vlbes\projects\ecotiter_firmware\docs\API\SERIAL_API.md` | Wire protocol: commands, responses, errors, broadcast — MUST NOT change |
| HTTP API contract | `C:\Users\vlbes\projects\ecotiter_firmware\docs\API\HTTP_API.md` | REST endpoints, SSE — will be unified with serial format |
| Tauri client | `C:\Users\vlbes\projects\ecotiter_tauri` | Primary consumer of the serial/BLE API. 10 ESP32 commands used, `parse_broadcast()`, NUS BLE UUIDs |
| Project spec | `docs/refs/project.md` | SSOT: hardware pinout, thread architecture, error hierarchy, module tree |
| Coding style | `docs/refs/coding_style.md` | 4-layer arch, enum over trait, heapless hot paths, no async, thiserror errors |
| Testing strategy | `docs/refs/guides/testing.md` | 3-tier: host unit tests, on-device integration, pytest HIL |
| Feasibility study | `prototype/26_06_27_rust_esp_idf_v6_feasibility_study.md` | RMT stepper validation, WiFi AP DHCP fix, NimBLE IDF v6 patch, heap measurements (184 KB free) |

## Architecture Decisions (from docs/refs/)

| Decision | Choice | Rationale |
|---|---|---|
| Crate structure | Mono-crate, 4-layer (`domain/` → `application/` → `infrastructure/` → `interface/`) | `project.md:326-388` |
| Error handling | `thiserror` enum hierarchy: `AppError → Hardware/Protocol/State/Resource` + `Recoverable` trait | `coding_style.md:§2`, `project.md:279-325` |
| Command dispatch | `enum Command + serde::Deserialize + match` (Enum over Trait) | `coding_style.md:§3` |
| JSON | `serde` + `serde_json`. Hot-path: `heapless::String + write!` / `json!()` macro. `Vec`/`String` forbidden in hot paths by `clippy.toml` | `coding_style.md:§5` |
| Concurrency | Sync only. `std::thread` + atomics + `mpsc` channels. No async/await. | `coding_style.md:§6`, `project.md:135-166` |
| Memory | `heapless` containers in all hot paths. `Vec`/`String` only in init/config. | `coding_style.md:§5`, `clippy.toml` |
| Stepper | `StepperMotor` trait in `domain/`, `RmtStepper` impl in `infrastructure/drivers/` | `project.md:98-117` |
| Broadcast format | **Single compact format across all transports** — Serial, BLE, SSE, `/api/status`. One `BroadcastEvent` struct, one `serialize_broadcast()`. | User decision 2026-06-29 |
| SSE events | Single `/api/events` endpoint with typed events (`status`, `debug`, `log`). Clients filter via `addEventListener`. | User decision 2026-06-29 |
| API contract | Serial/BLE API is frozen — Tauri client depends on it. HTTP API is unified to match serial format. | User decision 2026-06-29 |

## Wire Protocol Summary (Serial API — frozen)

### Command format
```json
{"id": <uint64>, "cmd": "<command>", ...params}
```

### Two-phase protocol (fill, empty, dose, rinse)
```
Phase 1 — ACK (≤1s):  {"id":42, "status":"ok", "data":{"status":"accepted"}}
Phase 2 — Result (≤600s): {"id":42, "status":"ok", "data":{...}}
Error:                   {"id":42, "status":"error", "message":"burette_busy"}
```

### Single-phase commands (stop, ping, getStatus, cal.*)
```json
{"id":42, "status":"ok", "data":{...}}
{"id":42, "status":"error", "message":"invalid_params"}
```

### Broadcast (every ~300ms, no id)
```json
{"ts":58473, "temp":24.5, "mv":41.8, "vlv":"in", "brt":{"sts":"idle", "vl":5.2, "spd":0}}
```
- `temp: null` when sensor disconnected
- `vlv`: `"in"`, `"out"`, `"unk"`
- `brt.sts`: `"idle"`, `"working"`, `"error"`

### Broadcast via SSE (`event: status`)
Same format + optional `"meta":{"ip":"192.168.1.100"}`.

### Error codes (frozen)
`burette_busy`, `start_failed`, `limit_full_reached`, `limit_empty_reached`, `stall_detected`, `stopped`, `watchdog_timeout`, `invalid_params`

### Commands (32 total, from legacy dispatch table)
| Command | Params | Phases |
|---|---|---|
| `system.getStatus` | none | single |
| `serial.ping` | none | single (no id echo needed) |
| `burette.fill` | `speed_ml_min` | two-phase |
| `burette.empty` | `speed_ml_min` | two-phase |
| `burette.doseVolume` | `volume_ml`, `speed_ml_min` | two-phase |
| `burette.rinse` | `cycles`, `speed_ml_min` | two-phase |
| `burette.stop` | none | single (aborts current, result on original id) |
| `burette.emergencyStop` | none | single (auto on Tauri timeout, no UI invocation) |
| `burette.getStatus` | none | single |
| `burette.cal.get` | none | single |
| `burette.cal.calcVolume` | `mass_g`, `temp_c`, `pressure_kpa`, `target_vol_ml` | single |
| ⚠️ actual code has | `steps: i32` | — deviates from frozen API |
| `burette.cal.calcSpeed` | `measurements: [{freq_hz, speed_ml_min}]` | single |
| ⚠️ actual code has | `steps_per_sec: u16` | — deviates from frozen API |
| `burette.cal.run` | `mode: "dose"|"speed"`, `freq_hz`, `speed_ml_min` | two-phase |
| `burette.cal.runSpeedSeq` | `freqs: [u16;3]`, `speed_ml_min` | two-phase |
| `burette.cal.getResult` | none | single |
| `burette.cal.save` | none | single |
| `burette.cal.reset` | none | single |
| `valve.setPosition` | `position: "input"|"output"` | single |
| `valve.getState` | none | single |
| `temperature.read` | none | single |
| `stallGuard.getThreshold` | none | single |
| `stallGuard.setThreshold` | `value` | single |
| `adc.cal.get` | none | single |
| `adc.cal.measure` | `known_mv` | single |
| `adc.cal.compute` | none | single |
| `adc.cal.save` | none | single |
| `adc.cal.reset` | none | single |
| `burette.moveSteps` | `steps`, `speed_hz` | single |
| `burette.moveToStop` | `dir`, `speed_hz` | single |
| ⚠️ actual code has | `speed_hz: u16` only (no `dir`) | — deviates from frozen API |
| `burette.setDirection` | `dir` | single |
| `system.getFormattedLogs` | `start`, `limit`, `level` | single |
| ⚠️ actual code has | unit variant (no params) | — deviates from frozen API |
| `system.readLog` | `start`, `limit` | single |
| ⚠️ actual code has | unit variant (no params) | — deviates from frozen API |

### BLE GATT (NUS)
| Attribute | UUID |
|---|---|
| Service | `6E400001-B5A3-F393-E0A9-E50D24DCCA9E` |
| RX (Tauri→ESP32) | `6E400002-B5A3-F393-E0A9-E50D24DCCA9E` |
| TX (ESP32→Tauri) | `6E400003-B5A3-F393-E0A9-E50D24DCCA9E` |

- BLE advertised name: `EcoTiter-XXXX` (last 2 bytes of MAC)
- Write: WriteWithoutResponse
- Zombie defense: 5 consecutive notify failures → disconnect + restart advertising

---

## Implementation Phases

### Phase 0: Scaffold + Build Pipeline ✅ COMPLETED

**Completed:** 2026-06-30 — See [Completion Report](../completed/26_06_30_phase0_scaffold_report.md)

**Files:** `src/lib.rs` `src/main.rs` `src/config.rs` `src/errors.rs` `src/domain/types.rs` `src/domain/memory.rs` `src/domain/logging.rs`

- `src/lib.rs`: lint attributes (`#![deny(clippy::all, ...)]`) + `cfg(target_arch = "xtensa")`-gated module declarations
- `src/config.rs`: compile-time constants (pins, timing, BLE params) — port from `prototype/src/config.rs` + `prototype/src/pins.rs`
- `src/errors.rs`: full `AppError` hierarchy from `project.md:279-325` — `thiserror` enums with `Recoverable` trait
- `src/domain/types.rs`: newtype wrappers — `Steps(i32)`, `Hz(u32)`, `Ml(f32)`, `MlMin(f32)`, `Direction { Cw, Ccw }`
- `src/domain/memory.rs`: fixed-size buffer aliases — `CommandBuffer::<256>`, `ResponseBuffer::<512>`, `LogBuffer::<100>`
- `src/domain/logging.rs`: `LogEntry { ts, level, msg }`, ring buffer `heapless::Deque<LogEntry, 100>`, `Log` trait impl
- `src/main.rs`: `fn main()` — `link_patches()`, `logger::init()`, `esp_task_wdt_deinit()`, suppress httpd_txrx log noise via `esp_log_level_set()`, `Peripherals::take()`. **Note:** loop is now fully populated with ADC, UART, WiFi, BLE, transport SM etc. (see Phase 5 — code advanced beyond plan). Brownout disabled via `sdkconfig.defaults CONFIG_BROWNOUT_DET=n` (compile-time), not `WRITE_PERI_REG`.

**Also:** update `sdkconfig.defaults` from prototype (NimBLE, WiFi, ADC, main task stack 16384, pthread stack 8192), verify `Cargo.toml` deps + `build.rs` (esp32-nimble patch), `.cargo/config.toml`, `clippy.toml`

**Results (verified 2026-06-30):**
- ✅ `cargo +esp build` — 0 errors, 0 warnings
- ✅ `cargo clippy-esp` — 0 warnings
- ✅ `cargo test-host` — compiles, no tests (addressed in Phase 1)
- ✅ Flash + boot log — `=== EcoTiter firmware ===`, no panic, no Guru Meditation
- ✅ Free heap = **237 KB** (>150 KB), largest block = **108 KB** (>80 KB)

**Reference files:** `prototype/src/lib.rs` (module layout), `prototype/src/main.rs` (entry point), `prototype/src/types.rs`, `prototype/src/errors.rs`, `prototype/src/logger.rs`, `prototype/src/config.rs`, `prototype/src/pins.rs`

---

### Phase 1: Domain — Pure Business Logic ✅ COMPLETED

**Completed:** 2026-06-30 — See [Completion Report](../completed/26_06_30_phase_1_domain.md)

**Files:** `src/domain/burette.rs` `src/domain/calibration.rs` `src/domain/planner.rs` `src/domain/channels.rs` `src/stepper/ramp.rs`

**`src/domain/burette.rs`** — Burette state machine:
- `BuretteState` enum: `Idle`, `Homing`, `Filling`, `Emptying`, `Dosing`, `Rinsing`, `Stopping`, `Error`
- `BuretteOperation` enum: `None`, `Fill`, `Empty`, `Dose`, `Rinse`
- Validation rules: cannot dose if empty, cannot fill if full, etc.
- State transition validation (5-step pipeline per `coding_style.md:§4`)

**`src/domain/calibration.rs`** — Calibration math (no NVS, no hardware):
- `CalibrationConfig`: `steps_per_ml`, `nominal_vol`, `speed_coeff`, `min_freq`, `max_freq`, `calibration_date`
- `volume_to_steps()` / `steps_to_volume()` — with clamping
- `speed_to_frequency()` / `frequency_to_speed()` — with clamping
- `get_z_factor()` — bilinear interpolation from ISO 8655 table (31 temp × 6 pressure)
- `calculate_new_steps_per_ml()` — gravimetric correction
- `calculate_speed_calibration()` — OLS regression **through mean** (legacy C++ compatibility): `k = (Σfv - Σf·Σv/n) / (Σff - Σf²/n)`, returns `k`, `r_squared`. **Deviation from plan:** plan described OLS-through-origin `k = Σ(f·v) / Σ(f²)`.
- `burette_cal_is_default()` — compare all fields within epsilon
- `burette_cal_set_pending()` / `burette_cal_get_pending_copy()` — thread-safe pending cal pattern (spinlock-protected)
- `AdcCalibration`: `a`, `b` coefficients, OLS from reference points. Model: `raw = a*ref + b` → `calibrated = (raw - b) / a`
- All domain types: `no_std` compatible, no `esp-idf-*` imports

**`src/domain/planner.rs`** — Pure planning/validation logic:
- `DosePlan`, `FillPlan`, `EmptyPlan`, `RinsePlan`, `CalRunPlan`, `CalSpeedSeqPlan`
- `plan_dose_volume()`: validate volume/speed, decide fill-first vs direct, multi-cycle plan
- `plan_fill()`, `plan_empty()`, `plan_rinse()`: simple validation
- `plan_cal_run()`, `plan_cal_speed_seq()`: calibration planning
- Host-testable — no ESP-IDF dependencies, runs on `x86_64`

**`src/stepper/ramp.rs`** — Trapezoidal acceleration:
- `RampConfig`: `accel_steps`, `decel_steps`, `min_interval_us`, `max_interval_us`
- `compute_ramp(total_steps, config) -> Vec<u32>` — pre-compute per-step intervals
- Triangular profile when `total_steps < accel + decel`
- Integer arithmetic only. Pure function, no dependencies.

**`src/domain/channels.rs`** — Thread communication:
- `SystemChannels`: `cmd_tx/rx`, `status_tx/rx`, `log_tx/rx`
- Bounded `mpsc` channels for command dispatch and status broadcast

**Results (verified 2026-06-30):**
- ✅ `cargo test --lib` — **47 planner tests** pass (ported from `legacy/test/test_planner.cpp`)
- ✅ `cargo test --lib` — **53 calibration tests** pass (speed, OLS, Z-factor, pending cal — ported from `legacy/test/test_speed.cpp`)
- ✅ `cargo test --lib` — **13 ramp tests** pass (ported from prototype + property-based)
- ✅ `cargo test --lib` — **138 total tests, 0 failures**
- ✅ Zero `esp-idf-*` imports in `domain/` and `stepper/` modules
- ✅ Cross-compilation xtensa — 0 errors, 0 warnings
- ✅ ESP32 boot — heap 237 KB free, 108 KB largest block

**Reference files:** `legacy/src/burette_planner.cpp`, `legacy/src/burette_cal.cpp`, `legacy/test/test_planner.cpp`, `legacy/test/test_speed.cpp`, `prototype/src/stepper/ramp.rs`

---

### Phase 2: Infrastructure — Hardware Drivers

**Files:**
- `src/infrastructure/drivers/stepper.rs` — `RmtStepper: StepperMotor`
- `src/infrastructure/drivers/adc.rs` — ADC1_CH6 oneshot + 64-sample rolling avg
- `src/infrastructure/drivers/onewire.rs` — DS18B20 software bitbang
- `src/infrastructure/drivers/valve.rs` — GPIO14 valve control
- `src/infrastructure/drivers/limitswitch.rs` — GPIO32/35 ISR + AtomicBool
- `src/infrastructure/drivers/led.rs` — blink state machine, transport mode indication
- `src/infrastructure/storage/nvs.rs` — Preferences/NVS wrappers for WiFi creds + calibration

| Driver | Key Details |
|---|---|
| `RmtStepper` | `TxChannelDriver` (channel 0, 1 MHz). `move_steps(&[u32])`: converts intervals to RMT Symbols, `CopyEncoder` + `send_and_wait()`. `PinDriver::output` for DIR (GPIO26) and EN (GPIO27, active LOW). `set_direction()` / `enable()` / `disable()` / `emergency_stop()`. |
| `AdcDriver` | `AdcDriver<ADC1>` + `AdcChannelDriver` (GPIO34, DB_12). 64-read ring buffer → rolling average. `set_raw_mv()` / `calibrated_mv()` via atomics. **Stabilization**: up to 10 attempts, range ≤5mV over 32 samples → then 32-sample median. **Calibration**: `calibrated = a * raw + b`. OLS: `raw = a*ref + b` → invert `coeff_a=1/a, coeff_b=-b/a`. |
| `OneWireBus` | Software bitbang via `PinDriver::input_output_od()` (GPIO33) + `Ets::delay_us()`. `reset()`, `write_byte()`, `read_byte()`, `skip_rom()`, `convert_t()`, `read_scratchpad()`. Runs in dedicated thread. |
| `Valve` | `set_position("input"/"output")` → GPIO14 LOW (input) / HIGH (output). Last-value atomic. |
| `LimitSwitch` | `PinDriver::input` + `PosEdge` interrupt → `AtomicBool`. `is_triggered()`, `clear()`, `rearm()`. |
| `Led` | `set_transport_mode(TransportMode)`. Blink state machine: `IDLE → ON_PHASE → OFF_BETWEEN → OFF_FINAL`. Transport: `OFF (USB)`, `ON (advertising)`, `1Hz (connected)`. |
| `Nvs` | Raw `esp-idf-sys` FFI (same pattern as prototype). Namespaces: `burette_cal` (steps_per_ml, nominal_vol, speed_coeff, min_freq, max_freq, cal_date), `adc_cal` (coeff_a, coeff_b, r_squared, cal_date), `wifi` (ssid, password), `stallguard` (threshold). |

**Acceptance:**

| Test | Method |
|---|---|
| RMT stepper 500 Hz CW, direction toggle CCW | 👁 oscilloscope on GPIO25; motor rotation visible |
| EN pin active LOW | 👁 motor silent on boot, starts on `enable()`, stops on `disable()` |
| Valve OPEN → CLOSE → OPEN cycle | 👁 audible click; multimeter on GPIO14 |
| Limit FULL (GPIO32): press switch | ✅ motor stops (if running); `is_triggered()` = true |
| Limit EMPTY (GPIO35): press switch | 👁 log shows event |
| ADC raw_mv: no electrode connected | ✅ `curl http://esp32/api/status` → `"mv": 0` |
| ADC with pH electrode | ✅ `curl /api/status` → `"mv": -264.0` (actual reading) |
| DS18B20 temperature | ✅ `curl /api/status` → `"temp": 25.3` |
| DS18B20 disconnected | ✅ `curl /api/status` → `"temp": null` |
| LED transport modes | 👁 USB→OFF, BLE adv→ON, BLE conn→1Hz blink |
| NVS write calibration → `esp_restart()` | 👁 values survive reboot |

**⚠️ Gaps identified (2026-07-03 audit):**
- **ADC stabilization logic** not implemented: plan describes up to 10 attempts, range ≤5mV over 32 samples → 32-sample median. Actual code reads raw ADC and applies rolling average only.
- ~~**`stallguard` NVS namespace** not wired~~ → **FIXED 2026-07-03** — `stallguard_read_threshold()` / `stallguard_write_threshold()` added, handlers wired.
- **ADC calibration model** differs: actual code uses `calibrated = a * raw + b` directly; plan describes `raw = a*ref + b` → invert `coeff_a=1/a, coeff_b=-b/a`.

**Reference files:** `prototype/src/stepper/rmt_stepper.rs`, `prototype/src/limitswitch.rs`, `prototype/src/temperature.rs`, `prototype/src/adc.rs`, `prototype/src/main.rs` (hardware init pattern)

---

### Phase 3: Application — Command Dispatch + REST API + Unify Broadcast ⚠️ PARTIAL

**Files:**
- `src/application/command.rs` — 32-variant `Command` enum, `serde::Deserialize`, `match` dispatch
- `src/application/state_machine.rs` — Burette SM + transport SM transitions
- `src/application/scheduler.rs` — 300ms broadcast timer, task pacing
- `src/interface/serial.rs` — USB UART reader (newline-split JSON, `heapless::String<256>` buffer)
- `src/interface/rest_api.rs` — `EspHttpServer` route handlers for all 12 HTTP routes
- `src/interface/webui.rs` — embedded HTML/CSS/JS via `include_str!`

**Command dispatch architecture (as-implemented — differs from plan for 5 variants):**
```rust
#[derive(Deserialize)]
#[serde(tag = "cmd")]
enum Command {
    #[serde(rename = "system.getStatus")] SystemGetStatus,
    #[serde(rename = "serial.ping")] SerialPing,
    #[serde(rename = "burette.fill")] BuretteFill { speed_ml_min: f32 },
    #[serde(rename = "burette.empty")] BuretteEmpty { speed_ml_min: f32 },
    #[serde(rename = "burette.doseVolume")] BuretteDoseVolume { volume_ml: f32, speed_ml_min: f32 },
    #[serde(rename = "burette.rinse")] BuretteRinse { cycles: u8, speed_ml_min: f32 },
    #[serde(rename = "burette.stop")] BuretteStop,
    #[serde(rename = "burette.getStatus")] BuretteGetStatus,
    #[serde(rename = "burette.cal.get")] BuretteCalGet,
    #[serde(rename = "burette.cal.run")] BuretteCalRun { mode: Option<String>, speed_ml_min: f32, freq_hz: u16 },
    #[serde(rename = "burette.cal.runSpeedSeq")] BuretteCalRunSpeedSeq { freqs: [u16; 3], speed_ml_min: f32 },
    #[serde(rename = "burette.cal.getResult")] BuretteCalGetResult,
    #[serde(rename = "burette.cal.save")] BuretteCalSave,
    #[serde(rename = "burette.cal.reset")] BuretteCalReset,
    // ❌ DEVIATES from plan: plan specifies mass_g, temp_c, pressure_kpa, target_vol_ml
    #[serde(rename = "burette.cal.calcVolume")] BuretteCalCalcVolume { steps: i32 },
    // ❌ DEVIATES from plan: plan specifies measurements: [{freq_hz, speed_ml_min}]
    #[serde(rename = "burette.cal.calcSpeed")] BuretteCalCalcSpeed { steps_per_sec: u16 },
    #[serde(rename = "valve.setPosition")] ValveSetPosition { position: ValvePosition },
    #[serde(rename = "valve.getState")] ValveGetState,
    #[serde(rename = "temperature.read")] TemperatureRead,
    #[serde(rename = "stallGuard.getThreshold")] StallGuardGetThreshold,
    #[serde(rename = "stallGuard.setThreshold")] StallGuardSetThreshold { value: u8 },
    #[serde(rename = "adc.cal.get")] AdcCalGet,
    #[serde(rename = "adc.cal.measure")] AdcCalMeasure { known_mv: f32 },
    #[serde(rename = "adc.cal.compute")] AdcCalCompute,
    #[serde(rename = "adc.cal.save")] AdcCalSave,
    #[serde(rename = "adc.cal.reset")] AdcCalReset,
    #[serde(rename = "burette.moveSteps")] BuretteMoveSteps { steps: i32, speed_hz: u16 },
    // ❌ DEVIATES from plan: plan specifies dir + speed_hz, actual has only speed_hz
    #[serde(rename = "burette.moveToStop")] BuretteMoveToStop { speed_hz: u16 },
    #[serde(rename = "burette.setDirection")] BuretteSetDirection { dir: Direction },
    // ❌ DEVIATES from plan: plan specifies start, limit, level — actual is unit variant
    #[serde(rename = "system.getFormattedLogs")] SystemGetFormattedLogs,
    // ❌ DEVIATES from plan: plan specifies start, limit — actual is unit variant
    #[serde(rename = "system.readLog")] SystemReadLog,
}
```

**Handler trait pattern:**
```rust
trait CommandHandler {
    fn handle(&self, cmd: &Command, id: u64) -> Result<CommandResponse, AppError>;
}
```

**Serial reader architecture:**
- Ring buffer accumulates bytes until `\n`
- `serde_json::Deserializer::from_slice(&buf)` — no `Value` alloc
- Response serialized to `heapless::String<512>` via `write!` or `json!()`
- **Serial silent mode**: `AtomicBool g_serial_silent` suppresses `Serial.print` / `println!` during command processing. Set `true` on entering `execute_command()`, restore on exit. Prevents UART echo from corrupting JSON response.

**SSE architecture (single endpoint, typed events):**
```
GET /api/events
  handler: send HTTP 200 + Content-Type: text/event-stream
           extract fd via httpd_req_to_sockfd()
           store in AtomicI32
           return immediately (0.1ms)

main loop (every tick):
  if fd != -1 {
    write(event: status\ndata: {compact json}\n\n)
    write(event: debug\ndata: {debug json}\n\n)  // rate-limited
    write(event: log\ndata: {log json}\n\n)       // on new entries
    if write error → fd.store(-1)
  }
```

**REST API routes:**

| Method | Route | Handler |
|---|---|---|
| GET | `/api/status` | Compact broadcast JSON + `meta.ip` |
| POST | `/api/command` | Deserialize → dispatch → serialize response (blocks until result for long ops) |
| POST | `/api/valve/set` | Valve control (dedicated endpoint for simpler POST from WebUI) |
| GET | `/api/valve/state` | Current position |
| GET | `/api/events` | SSE stream |
| GET | `/api/logs` | Ring buffer entries (query: `start`, `limit`, `level`) |
| DELETE | `/api/logs` | Clear ring buffer |
| GET | `/api/ping` | `{"status":"ok"}` |
| GET | `/` | WebUI |
| GET | `/wifi` | Captive portal form |
| POST | `/wifi/connect` | Save credentials + `esp_restart()` |
| GET | `/wifi/status` | WiFi STA status |
| GET | `/generate_204` | 302 → `/wifi` |
| GET | `/hotspot-detect.html` | 302 → `/wifi` |
| GET | `/ncsi.txt` | 302 → `/wifi` |
| GET | `/connecttest.txt` | 302 → `/wifi` |
| GET | `/gen_204` | 302 → `/wifi` |

**Note:** `/api/status` returns the same compact format as serial/BLE broadcast, with `meta.ip` added for HTTP clients.

**⚠️ Acceptance (as-implemented — many are stub responses, not real dispatch):**

| Test | Status | Notes |
|------|--------|-------|
| `system.getStatus` via POST `/api/command` | ⚠️ STUB | `handle_api_command()` returns `{"status":"ok","message":"received"}` — no real dispatch |
| `serial.ping` | ⚠️ STUB | Same stub handler |
| `burette.fill` via HTTP | ⚠️ STUB | Not dispatched — real motor flow goes through serial/BLE paths only |
| `burette.doseVolume` via HTTP | ⚠️ STUB | Same |
| `burette.stop` | ⚠️ STUB | Same |
| `burette.getStatus` | ⚠️ STUB | Same stub response |
| `valve.setPosition` via HTTP | ⚠️ STUB | `handle_valve_set()` doesn't call real driver |
| `valve.getState` via HTTP | ⚠️ STUB | Always returns `"input"` |
| Two-phase protocol via serial/BLE | ✅ Functional | Motor task + `PendingOpsManager` handle ACK/result flow |
| Error handling | ✅ Functional | `dispatch()` returns proper `AppError` variants |
| `/api/status` broadcast | ⚠️ STUB | Returns hardcoded `None`/`"in"`/`"idle"`/`0` for sensor values |
| SSE `event: status` | ❌ NOT WORKING | `broadcast_websocket_event()` is a no-op |
| SSE `event: debug` | ❌ NOT WORKING | No SSE at all |
| SSE `event: log` | ❌ NOT WORKING | No SSE at all |
| HTTP blocks for long ops | ❌ NOT IMPLEMENTED | `POST /api/command` is stub, doesn't block |
| All 32 commands respond (via serial/BLE) | ⚠️ PARTIAL | Commands dispatch to correct handlers, but `system.getFormattedLogs`/`system.readLog` accept wrong params |

**⚠️ Gaps identified (2026-07-03 audit) — status after session:**
- ~~**SSE endpoint completely non-functional**~~ → **FIXED** — WebSocket `/ws/stream` restored from `08c0cab`, `CONFIG_HTTPD_WS_SUPPORT=y` re-enabled, `broadcast_websocket_event()` functional.
- **REST API stubs:** `/api/command`, `/api/valve/set`, `/api/valve/state`, `/api/status` all return placeholder data, don't wire to real drivers. ⏳ PENDING
- ~~**5 command variant param mismatches**~~ → **FIXED** — all 5 variants updated, handlers wired to domain functions.
- **Pending calibration state machines not implemented:** `PENDING_CAL_DOSE`, `PENDING_CAL_SPEED`, `PENDING_CAL_SPEED_SEQ` absent from `state_machine.rs`. ⏳ PENDING
- **Homing SM not in state_machine.rs:** hardcoded in `main.rs` as imperative sequence. ⏳ PENDING
- ~~**`DELETE /api/logs` stub**~~ → **FIXED** — `logger::clear_entries()` + `rest_api::handle_api_logs_clear()` wired.
- ~~**`panic!()` in production**~~ → **FIXED** — replaced with `log::error!` + `std::process::exit(1)`.

**Reference files:** `legacy/src/command.cpp`, `legacy/src/handlers/`, `prototype/src/webserver.rs`, `prototype/src/status.rs`, `prototype/src/logger.rs`

---

### Phase 4: Network — WiFi + BLE ✅ IMPLEMENTED (ahead of plan)

**Files:**
- `src/infrastructure/network/wifi.rs` — `WifiManager`
- `src/infrastructure/network/http_server.rs` — `EspHttpServer` with all routes + SSE
- `src/infrastructure/network/ble.rs` — BLE NUS service + zombie defense

**WiFi architecture:**
- `EspWifi` + `BlockingWifi` from `esp-idf-svc`
- AP mode: custom `EspNetif` via `new_with_conf()` with IP `192.168.4.1/24`, `swap_netif_ap()` before `wifi.start()` (fix from Phase 1d)
- STA mode: connect to saved credentials, NVS-backed, reconnect on boot
- DNS responder: UDP socket on `192.168.4.1:53`, manual DNS response builder
- Captive portal: 5 probe URLs → 302 `/wifi`
- `process()`: poll DNS, reconnect timer (30s), BLE coexistence flag

**BLE architecture:**
- `esp32-nimble` (patched for IDF v6 via `build.rs`)
- BT/WiFi coexistence: `esp_coex_preference_set(ESP_COEX_PREFER_BT)` at init
- NUS service: `6E400001-B5A3-F393-E0A9-E50D24DCCA9E`
- RX characteristic: `6E400002`, write / write-without-response
- TX characteristic: `6E400003`, notify
- Advertising: `EcoTiter-XXXX`, connectable, every 100ms
- Connection params (deferred 2s after connect): min 30ms, max 50ms, latency 4, supervision timeout 30s
- Command queue: bounded `mpsc` (8 entries), drained from main loop
- Zombie defense level 1: 5 consecutive failed notifications → disconnect + flush + restart advertising
- Zombie defense level 2: `connected_count() == 0` but internal flag says connected → kill
- Zombie defense level 3: in `ble_send()` — if `getConnectedCount() == 0` but local `g_ble_connected == true`, kill zombie
- BLE notify thread: `std::thread` (8 KB stack), `recv` from mpsc → `notify()`. **⚠️ Body is TODO — notifications not sent.**

**⚠️ Acceptance (code implemented, hardware verification TBD):**

| Test | Status | Notes |
|------|--------|-------|
| AP `EcoTiter-AP` visible | ✅ code | WiFi AP configured with custom netif |
| Phone gets IP (192.168.4.x) | ✅ code | DHCP server on AP netif |
| Captive portal triggers | ✅ code | 5 probe URLs → 302 `/wifi` |
| WiFi form → STA connect | ✅ code | POST `/wifi/connect` saves to NVS + restart flag |
| `esp_restart()` → STA reconnect | ✅ code | STA connect on boot with saved creds |
| DNS responder | ✅ code | `UdpSocket` on port 53, non-blocking |
| `/api/status` from browser | ⚠️ stub response | Returns hardcoded values (Phase 3 gap) |
| WebSocket from browser | ✅ code | **RESTORED 2026-07-03** — `WsSender` with session-ID tracking, `broadcast_websocket_event()` functional |
| BLE advertising `EcoTiter-XXXX` | ✅ code | NUS service, custom UUIDs, connectable |
| BLE connect + write JSON command | ⚠️ partial | RX callback writes to `cmd_tx`, but BLE notify thread body is TODO |
| USB heartbeat timeout → BLE takeover | ✅ code | Transport SM in main loop handles handoff |
| LED: advertising (ON), connected (1Hz) | ✅ code | `Led` driver fully implemented |
| No Guru Meditation | ✅ verified | **2026-07-03** — 30s smoke test, 0 crashes |

**Reference files:** `prototype/src/wifi.rs`, `prototype/src/webserver.rs`

**Known constraints:**
- `CONFIG_LWIP_MAX_SOCKETS=8` — SSE (1 socket) + HTTP API + DNS
- `EspHttpServer::Configuration{stack_size: 12288}` — prevents stack overflow in Rust fn_handlers
- Main task stack: 16384 (from sdkconfig.defaults)

### 🚦 Concurrency & Race Condition Verification (Phase 4 gate)

After BLE is integrated, the firmware has three concurrent command sources (UART, BLE, HTTP) plus three background threads (motor, temperature, BLE notify). The following scenarios MUST be verified on real hardware:

#### Scenario 1: Concurrent command reception
| Transport A | Transport B | Expected behaviour |
|---|---|---|
| USB: `burette.fill` | BLE: `burette.stop` (within 100ms) | Fill starts, stop aborts it. Result sent on original fill id with `"stopped"`. |
| USB: `burette.fill` | HTTP: `burette.doseVolume` | First command wins ACK, second gets `"burette_busy"`. |
| BLE: `burette.doseVolume` (long) | USB plug-in during execution | USB takeover → BLE disconnect → command continues to completion → result lost (acceptable, Tauri retries). |

**Method:** Python script sending overlapping commands via serial + BLE simultaneously. Repeat 20×.

#### Scenario 2: Transport handoff race
| Sequence | Expected |
|---|---|
| 1. BLE connected, command running | 1. OK |
| 2. USB cable plugged in | 2. `transport_sm()` detects USB → switches active transport to USB, calls `ble_disconnect_all()` |
| 3. USB: `system.getStatus` | 3. Response comes via USB |
| 4. BLE notify failure detected | 4. Zombie defense triggers cleanup (advertising restarted) |
| 5. No panic, no Guru Meditation | 5. ✅ |

**Method:** Scripted USB plug/unplug cycle 10× during active BLE command. Monitor serial log for panics.

#### Scenario 3: Command queue overflow
| Condition | Expected |
|---|---|
| 8 concurrent commands queued (BLE RX) | 9th is dropped, sender gets nothing (BLE WriteWithoutResponse = no flow control) |
| Main loop drains queue at 10ms tick | All 8 processed, none lost |
| Queue overflow during motor move | Motor continues, overflowed commands discarded |

#### Scenario 4: Shared state (NVS, logger, broadcast)
| Resource | Access pattern | Race risk |
|---|---|---|
| NVS (`cal` namespace) | Command handler reads/writes. WiFi reads `wifi` namespace. | Low — NVS is reentrant in ESP-IDF. Separate namespaces. |
| Logger ring buffer | `info!()` from any thread. `get_logs()` from HTTP handler. | **Must use `try_lock()`** (not `lock()`) — log is hot path. If lock busy, skip log entry (acceptable). |
| Broadcast struct | Main loop writes. SSE push reads. HTTP `/api/status` reads. | Use `Atomic` fields or `Arc<RwLock>` with `try_read()`. Never block main loop on broadcast read. |
| `active_transport` | `transport_sm()` writes. Command dispatch reads. | `AtomicU8` — lock-free. Transient inconsistency (one tick) is harmless. |

**Method:** Run Tauri client sending commands every 100ms for 60s while simultaneously:
- Polling `/api/status` via curl every 200ms (from PC)
- Streaming `/api/events` via SSE (from phone connected to AP)
- Logging via UART at INFO level
- Verify: no panics, no stale data, heap stable.

#### Scenario 5: BT/WiFi radio coexistence
| Condition | Expected |
|---|---|
| WiFi STA connected, streaming data, BLE advertising + connected | Both active. NimBLE + lwIP share radio via `ESP_COEX_PREFER_BT`. |

**Known ESP32 limitation:** BT/WiFi coexistence can cause throughput degradation on both radios, but must NOT cause panics, disconnects, or command loss.

**Method:** `ping -t` to ESP32 over WiFi (100ms interval), while simultaneously sending BLE notifications every 100ms. Run 60s. Check: ping loss < 5%, no BLE disconnect.

---

### Phase 5: Integration — Motor Task + Full Main Loop ✅ IMPLEMENTED (ahead of plan)

**Files:** `src/motor_task.rs` `src/main.rs` (complete)

**Motor task (actual — command-driven via mpsc, not atomic-polling):**
```rust
// src/motor_task.rs
pub fn spawn(stepper: RmtStepper<'static>, cmd_rx: Arc<Mutex<Receiver<MotorCommand>>>,
             status_tx: Sender<StatusEvent>, response_tx: Sender<CommandResponse>) {
    std::thread::Builder::new()
        .stack_size(MOTOR_THREAD_STACK)
        .name("motor".into())
        .spawn(move || loop {
            let cmd = {
                let rx = cmd_rx.lock().unwrap();
                rx.recv_timeout(Duration::from_millis(10))
            };
            match cmd {
                Ok(command) => handle_command(command, &stepper, &status_tx, &response_tx),
                Err(RecvTimeoutError::Timeout) => continue,
                Err(RecvTimeoutError::Disconnected) => break,
            }
        })
        .expect("motor task spawn");
}
```
Handles: Fill, Empty, Dose, Rinse (multi-cycle), Stop, EmergencyStop, Reset — with ramp computation and position tracking via `execute_motion()`.

**Full main loop (actual — richer than planned, see `main.rs`):**
```rust
fn main() {
    link_patches();
    logger::init();
    unsafe { esp_task_wdt_deinit(); }
    let peripherals = Peripherals::take().unwrap();

    // Init hardware
    let stepper = RmtStepper::new(peripherals.rmt.channel0, ...);
    let adc = AdcDriver::new(peripherals.adc1, ...);
    let mut valve = Valve::new(peripherals.pins.gpio14);
    let mut wifi = WifiManager::new(...);
    let server = HttpServer::new(...);

    // Homing sequence (hardcoded, not SM)
    homing_sequence(&stepper, &valve, &nvs);

    // Spawn threads
    spawn_motor_task(stepper, cmd_rx, status_tx, response_tx);
    spawn_uart_reader(...);
    spawn_temperature_thread(gpio33);
    spawn_owner_thread(wifi, server, ble, ...); // WiFi + HTTP + BLE owner

    // Main loop (safety-critical, never block)
    loop {
        adc_sample(&mut adc);
        process_uart_input();
        drain_response_channel();
        pending_ops.watchdog_check();
        transport_sm(usb_alive, ble_connected);
        broadcast_push();
        led_process();
        if G_RESTART_PENDING.load(...) { esp_restart(); }
        logger_process();            // flush log entries
        limitswitch_event_push();
        stack_watermark_check();     // every 1000 ticks
        std::thread::sleep(Duration::from_millis(10));
    }
}
```

**Transport state machine (from SERIAL_API.md transport rules):**
```rust
fn transport_process(serial: &SerialReader, ble: &BleService) {
    let usb_alive = serial.last_activity_elapsed() < Duration::from_secs(10);
    let ble_ok = ble.is_connected();

    if usb_alive {
        set_active_transport(Transport::Usb);
        if ble_ok { ble.disconnect_all(); }
        led_set_mode(LedMode::Off);
    } else if ble_ok {
        set_active_transport(Transport::Ble);
        led_set_mode(LedMode::Connected);
    } else {
        set_active_transport(Transport::Usb);  // fallback
        ble_start_advertising();
        led_set_mode(LedMode::Advertising);
    }
}
```

**Two-phase command execution flow:**
1. Command received (serial/BLE/HTTP) → parse `Command` enum
2. If handler returns `two-phase`:
   - Immediately send ACK: `{"id":N, "status":"ok", "data":{"status":"accepted"}}`
   - Pass `(command, id, response_tx)` to state machine
3. Burette SM processes: validate → set motor target + valve position
4. Motor task reads atomics, runs RMT
5. On completion: SM sends result via `response_tx`
6. Main loop drains `response_rx` → sends result via original transport
7. Watchdog: 60s timeout → automatic `emergencyStop` + `"watchdog_timeout"` error

**Pending command execution state machines (added to state_machine.rs):**

**Calibration Dose SM (PENDING_CAL_DOSE):**
```
CAL_IDLE → CAL_FILLING → CAL_EMPTYING → CAL_DONE → CAL_IDLE
```
- IDLE: if volume not near full → fill, else skip to emptying
- FILLING: record position, set valve OUTPUT, start empty at test frequency
- EMPTYING: compute `steps_taken = abs(pos_after - pos_before)`, set volume=0, send result `{"steps_taken": N}`
- DONE: reset phase to IDLE

**Calibration Speed SM (PENDING_CAL_SPEED):**
```
CAL_IDLE → CAL_FILLING → CAL_EMPTYING → CAL_DONE → CAL_IDLE
```
- IDLE: if not at FULL limit → fill
- FILLING: record start time, set valve OUTPUT, move to EMPTY at test frequency
- EMPTYING: compute `speed = nominal_vol / (elapsed_ms / 60000)`, send result `{"speed_ml_min": S, "elapsed_ms": T}`

**Speed Sequence SM (PENDING_CAL_SPEED_SEQ):**
```
START → FILL → EMPTY@freq[0] → FILL → EMPTY@freq[1] → FILL → EMPTY@freq[2] → DONE
```
- 3-point sequence: fill at computed speed, then empty at 3 different frequencies measuring time
- Results stored in internal array, no response sent — client fetches via `burette.cal.getResult`

**Homing SM (called once at boot):**
```
START → valve=INPUT → move to FULL at max_freq/2 → set volume=nominal_vol → DONE
```
- 120s timeout → emergency stop + error log
- Called from `main.rs` after hardware init, before entering main loop

**⚠️ Acceptance (code implemented, hardware/Tauri integration TBD):**

| Item | Status | Notes |
|------|--------|-------|
| Motor task: fill/empty/dose/rinse | ✅ implemented | Full ramp + position tracking via `execute_motion()` |
| Motor task: stop/emergencyStop | ✅ implemented | Atomic stop flag + channel disable |
| Motor task: multi-cycle rinse | ✅ implemented | Fill/empty cycling with configurable cycles |
| Main loop: ADC sample | ✅ implemented | 1 read per tick |
| Main loop: UART input | ✅ implemented | Spawned reader thread |
| Main loop: transport SM | ✅ implemented | USB ↔ BLE handoff based on activity |
| Main loop: broadcast push | ⚠️ serial only | SSE push is no-op (Phase 3 gap) |
| Main loop: watchdog (60s timeout) | ✅ implemented | `PendingOpsManager::watchdog_check()` |
| Main loop: restart detection | ✅ implemented | `G_RESTART_PENDING` flag |
| Main loop: LED process | ✅ implemented | Blink SM ticks every iteration |
| Main loop: stack watermark | ✅ implemented | Monitored every 1000 ticks |
| Tauri auto-connect via USB | ⏳ untested | Requires Tauri client + hardware |
| Homing at boot | ✅ implemented | 120s timeout safety, hardcoded sequence |
| No panic in production code | ❌ **4 violations** | See Phase 3 gaps |

**Reference files:** `prototype/src/main.rs` (hardware init + loop pattern), `legacy/src/main.cpp` (transport SM, safety net, homing)

---

### Phase 6: TMC2209 UART Driver (deferred)

**Files:** `src/infrastructure/drivers/stepper_drv.rs` `src/infrastructure/drivers/tmc_regs.rs`

- TMC2209 register map: `GCONF`, `GSTAT`, `IOIN`, `CHOPCONF`, `PWMCONF`, `COOLCONF`, `SGTHRS`, `DRV_STATUS`, `TPWMTHRS`
- UART single-wire half-duplex on UART2 (GPIO17/GPIO16, 115200 baud)
- Shadow register cache (write-through)
- `stepperDrv_init()`: test_connection(), dump version, configure StealthChop + CoolStep
- `stepperDrv_read_sg_result()`, `stepperDrv_read_drv_status()`
- `stepperDrv_set_current()` (800mA RMS), `stepperDrv_set_microsteps()` (16 µsteps, mres=4)
- **StealthChop configuration**: `en_spreadCycle = false`, `toff=4`, `tbl=1`, `vsense=false`
- **CoolStep**: `semin=5`, `semax=2`, `sedn=0b01`
- **TCOOLTHRS**: `0xFFFFF` (StallGuard enabled at all speeds above threshold)
- **StallGuard**: 10-bit SG_RESULT (0-1023). Debounce filter: 3 consecutive readings below `threshold × 2` before reporting stall. SGTHRS register written from NVS value.

**Acceptance:**
- ✅ `stallGuard.getThreshold` → returns current value
- ✅ `stallGuard.setThreshold 200` → saves to NVS, persists across reboot
- ✅ `DRV_STATUS` readable: `otpw`, `ot`, `s2ga`, `stst`, `ola`, `olb`
- ✅ StallGuard value changes with mechanical load on motor

**Reference files:** `legacy/src/stepper_drv.cpp`, `legacy/src/stallguard.cpp`, `legacy/src/tmc_regs.h` (if exists)

---

### Phase 7: Tauri Desktop Client (separate repo)

The Tauri client at `C:\Users\vlbes\projects\ecotiter_tauri` is already complete. This phase covers firmware-side compatibility verification and any edge-case fixes discovered during integration testing.

**Acceptance:**
- ✅ All 32 ESP32 commands round-trip correctly
- ✅ Two-phase protocol timing: ACK ≤ 1s, result ≤ 600s
- ✅ Broadcast: every 300ms, exact compact format
- ✅ Reboot detection: ts monotonic, rollback triggers reconnect
- ✅ USB heartbeat: `serial.ping` every 5s, 10s timeout
- ✅ BLE: NUS connect → write → notify → disconnect → reconnect cycle (10 iterations)
- ✅ USB ↔ BLE transport handoff during active operation
- ✅ Titration end-to-end: prepare → loop → EQ → CSV

---

## Verification Requirements

### Code quality (every phase)
- `cargo +esp build`: 0 errors, 0 warnings
- `cargo clippy-esp`: 0 warnings
- `cargo test-host`: all tests pass
- No `unwrap()` / `expect()` / `panic!()` / `todo!()` / `unreachable!()` in production code (enforced by `lib.rs` lint attributes)
- `heapless::Vec`/`heapless::String` used instead of `std::Vec`/`std::String` in all hot paths (enforced by `clippy.toml`)
- No `esp-idf-*` imports in `domain/` layer (compile-check on host)

### Hardware testing (every phase with hardware changes)
- Must pass 60s continuous run without Guru Meditation
- Free heap must not decrease over 60s steady-state (no leak)
- BLE connect + write + disconnect cycle must be repeatable 10× without crash

### API compatibility (firmware-api)
- `SERIAL_API.md` is the frozen contract — any deviation requires user approval
- All error codes from `SERIAL_API.md` must be supported
- Broadcast format must match `parse_broadcast()` in Tauri `broadcast.rs`
- `emergencyStop` IS used by Tauri backend automatically on two-phase command timeout (connection.rs), must remain public

## Notes

- WDT is disabled at boot (`esp_task_wdt_deinit()`) — RMT `send_and_wait()` blocks >250ms
- Main loop must never block — all blocking I/O in dedicated threads
- PinDriver in `esp-idf-hal` v0.46: `PinDriver<'d, MODE>` has 1 type parameter (not 2), no `RmtOutputPin` trait exists
- GPIO constructors have private fields — use `peripherals.pins.gpioXX.degrade_output()` at runtime
- EN pin for TMC2209 is active LOW — must be set immediately in constructor
- Brownout detector disabled at boot (`WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0)`) — prevents ESP32 reset during RMT/RF bursts
- All heap measurements from prototype Phase 0: 184 KB free, 108 KB largest block — ample margin

### Excluded from legacy

- **LittleFS** — file system and all file-based logging (`logger.cpp`: `getFormattedLogsFromFile()`, `syncTime()`, `compactFile()`, `LittleFS.begin()`). Logging is RAM ring buffer only. HTTP endpoint `GET /api/logs/download` is NOT implemented.
- **`burette.emergencyStop`** is a public command per frozen API (Tauri calls it automatically on two-phase timeout)
