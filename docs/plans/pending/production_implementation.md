---
type: Plan
title: Production Implementation Plan — EcoTiter Firmware (Rust, ESP-IDF v6)
description: Atomic build plan for production-grade Rust firmware, derived from legacy C++ reference and Rust feasibility prototype. All changes scoped to src/.
tags: [plan, implementation, firmware, esp32, production]
timestamp: 2026-06-29
status: draft
---

# Production Implementation Plan

## References (read-only)

| Source | Path | Purpose |
|---|---|---|
| Legacy C++ | `legacy/` | Business logic reference (planner, calibration, transport SM, handlers, TMC2209) |
| Rust prototype | `prototype/` | Feasibility validation (RMT config, WiFi AP DHCP fix, NimBLE patch, sdkconfig) |
| Serial API contract | `C:\Users\vlbes\projects\ecotiter_firmware\docs\API\SERIAL_API.md` | Wire protocol: commands, responses, errors, broadcast — MUST NOT change |
| HTTP API contract | `C:\Users\vlbes\projects\ecotiter_firmware\docs\API\HTTP_API.md` | REST endpoints, SSE — will be unified with serial format |
| Tauri client | `C:\Users\vlbes\projects\ecotiter_tauri` | Primary consumer of the serial/BLE API. 31 invoke commands, `parse_broadcast()`, NUS BLE UUIDs |
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

### Commands (31 total, from Tauri `backend.ts` + SERIAL_API.md)
| Command | Params | Phases |
|---|---|---|
| `system.getStatus` | none | single |
| `serial.ping` | none | single (no id echo needed) |
| `burette.fill` | `speed_ml_min` | two-phase |
| `burette.empty` | `speed_ml_min` | two-phase |
| `burette.doseVolume` | `volume_ml`, `speed_ml_min` | two-phase |
| `burette.rinse` | `cycles`, `speed_ml_min` | two-phase |
| `burette.stop` | none | single (aborts current, result on original id) |
| `burette.emergencyStop` | none | single, **internal only** — NOT exposed in API |
| `burette.getStatus` | none | single |
| `burette.cal.get` | none | single |
| `burette.cal.calcVolume` | `mass_g`, `temp_c`, `pressure_kpa`, `target_vol_ml` | single |
| `burette.cal.calcSpeed` | `measurements: [{freq_hz, speed_ml_min}]` | single |
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
| `burette.setDirection` | `dir` | single |

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

### Phase 0: Scaffold + Build Pipeline

**Files:** `src/lib.rs` `src/main.rs` `src/config.rs` `src/errors.rs` `src/domain/types.rs` `src/domain/memory.rs` `src/domain/logging.rs`

- `src/lib.rs`: lint attributes (`#![deny(clippy::all, ...)]`) + `cfg(target_arch = "xtensa")`-gated module declarations
- `src/config.rs`: compile-time constants (pins, timing, BLE params) — port from `prototype/src/config.rs` + `prototype/src/pins.rs`
- `src/errors.rs`: full `AppError` hierarchy from `project.md:279-325` — `thiserror` enums with `Recoverable` trait
- `src/domain/types.rs`: newtype wrappers — `Steps(i32)`, `Hz(u32)`, `Ml(f32)`, `MlMin(f32)`, `Direction { Cw, Ccw }`
- `src/domain/memory.rs`: fixed-size buffer aliases — `CommandBuffer::<256>`, `ResponseBuffer::<512>`, `LogBuffer::<100>`
- `src/domain/logging.rs`: `LogEntry { ts, level, msg }`, ring buffer `heapless::Deque<LogEntry, 100>`, `Log` trait impl
- `src/main.rs`: `fn main()` — `link_patches()`, `logger::init()`, `esp_task_wdt_deinit()`, `Peripherals::take()`, empty loop

**Also:** update `sdkconfig.defaults` from prototype (NimBLE, WiFi, ADC, main task stack 16384, pthread stack 8192), verify `Cargo.toml` deps + `build.rs` (esp32-nimble patch), `.cargo/config.toml`, `clippy.toml`

**Acceptance:**
- ✅ `cargo +esp build` — 0 errors, 0 warnings (our code)
- ✅ `cargo clippy-esp` — 0 warnings
- ✅ `cargo test-host` — compiles, no tests yet
- ✅ Flash + boot log on COM5 — `=== EcoTiter firmware ===`, no panic, no Guru Meditation
- ✅ Free heap > 150 KB, largest block > 80 KB

**Reference files:** `prototype/src/lib.rs` (module layout), `prototype/src/main.rs` (entry point), `prototype/src/types.rs`, `prototype/src/errors.rs`, `prototype/src/logger.rs`, `prototype/src/config.rs`, `prototype/src/pins.rs`

---

### Phase 1: Domain — Pure Business Logic

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
- `calculate_speed_calibration()` — OLS regression, returns `k`, `r_squared`
- `AdcCalibration`: `a`, `b` coefficients, OLS from 5 reference points
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

**Acceptance:**
- ✅ `cargo test --lib` — planner tests (60 cases, ported from `legacy/test/test_planner.cpp`)
- ✅ `cargo test --lib` — speed calibration tests (27 cases, ported from `legacy/test/test_speed.cpp`)
- ✅ `cargo test --lib` — ramp tests (10 cases, ported from `prototype/src/stepper/ramp.rs`)
- ✅ `cargo test --lib` — Z-factor interpolation (boundary values, table exact matches)
- ✅ `cargo test --lib` — OLS regression tests (perfect fit, noisy data, single-point guard, degenerate)
- ✅ All tests pass on host (`x86_64`), zero `esp-idf-*` imports in `domain/`

**Reference files:** `legacy/src/burette_planner.cpp`, `legacy/src/burette_cal.cpp`, `legacy/test/test_planner.cpp`, `legacy/test/test_speed.cpp`, `prototype/src/stepper/ramp.rs`

---

### Phase 2: Infrastructure — Hardware Drivers

**Files:**
- `src/infrastructure/drivers/stepper.rs` — `RmtStepper: StepperMotor`
- `src/infrastructure/drivers/adc.rs` — ADC1_CH6 oneshot + 64-sample rolling avg
- `src/infrastructure/drivers/onewire.rs` — DS18B20 software bitbang
- `src/infrastructure/drivers/valve.rs` — GPIO12/13 valve control
- `src/infrastructure/drivers/limitswitch.rs` — GPIO32/35 ISR + AtomicBool
- `src/infrastructure/drivers/led.rs` — blink state machine, transport mode indication
- `src/infrastructure/storage/nvs.rs` — Preferences/NVS wrappers for WiFi creds + calibration

| Driver | Key Details |
|---|---|
| `RmtStepper` | `TxChannelDriver` (channel 0, 1 MHz). `move_steps(&[u32])`: converts intervals to RMT Symbols, `CopyEncoder` + `send_and_wait()`. `PinDriver::output` for DIR (GPIO26) and EN (GPIO27, active LOW). `set_direction()` / `enable()` / `disable()` / `emergency_stop()`. |
| `AdcDriver` | `AdcDriver<ADC1>` + `AdcChannelDriver` (GPIO34, DB_12). 64-read ring buffer → rolling average. `set_raw_mv()` / `calibrated_mv()` via atomics. |
| `OneWireBus` | Software bitbang via `PinDriver::input_output_od()` (GPIO33) + `Ets::delay_us()`. `reset()`, `write_byte()`, `read_byte()`, `skip_rom()`, `convert_t()`, `read_scratchpad()`. Runs in dedicated thread. |
| `Valve` | `set_position("input"/"output")` → GPIO12 HIGH / GPIO13 HIGH. Last-value atomic. |
| `LimitSwitch` | `PinDriver::input` + `PosEdge` interrupt → `AtomicBool`. `is_triggered()`, `clear()`, `rearm()`. |
| `Led` | `set_transport_mode(TransportMode)`. Blink state machine: `IDLE → ON_PHASE → OFF_BETWEEN → OFF_FINAL`. Transport: `OFF (USB)`, `ON (advertising)`, `1Hz (connected)`. |
| `Nvs` | Raw `esp-idf-sys` FFI (same pattern as prototype). Namespaces: `wifi` (ssid, password), `cal` (steps_per_ml, nominal_vol, speed_coeff, a_x1000, b). |

**Acceptance:**

| Test | Method |
|---|---|
| RMT stepper 500 Hz CW, direction toggle CCW | 👁 oscilloscope on GPIO25; motor rotation visible |
| EN pin active LOW | 👁 motor silent on boot, starts on `enable()`, stops on `disable()` |
| Valve OPEN → CLOSE → OPEN cycle | 👁 audible click; multimeter on GPIO12/13 |
| Limit FULL (GPIO32): press switch | ✅ motor stops (if running); `is_triggered()` = true |
| Limit EMPTY (GPIO35): press switch | 👁 log shows event |
| ADC raw_mv: no electrode connected | ✅ `curl http://esp32/api/status` → `"mv": 0` |
| ADC with pH electrode | ✅ `curl /api/status` → `"mv": -264.0` (actual reading) |
| DS18B20 temperature | ✅ `curl /api/status` → `"temp": 25.3` |
| DS18B20 disconnected | ✅ `curl /api/status` → `"temp": null` |
| LED transport modes | 👁 USB→OFF, BLE adv→ON, BLE conn→1Hz blink |
| NVS write calibration → `esp_restart()` | 👁 values survive reboot |

**Reference files:** `prototype/src/stepper/rmt_stepper.rs`, `prototype/src/limitswitch.rs`, `prototype/src/temperature.rs`, `prototype/src/adc.rs`, `prototype/src/main.rs` (hardware init pattern)

---

### Phase 3: Application — Command Dispatch + REST API + Unify Broadcast

**Files:**
- `src/application/command.rs` — 31-variant `Command` enum, `serde::Deserialize`, `match` dispatch
- `src/application/state_machine.rs` — Burette SM + transport SM transitions
- `src/application/scheduler.rs` — 300ms broadcast timer, task pacing
- `src/interface/serial.rs` — USB UART reader (newline-split JSON, `heapless::String<256>` buffer)
- `src/interface/rest_api.rs` — `EspHttpServer` route handlers for all 12 HTTP routes
- `src/interface/webui.rs` — embedded HTML/CSS/JS via `include_str!`

**Command dispatch architecture:**
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
    // ... all remaining commands
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
| GET | `/api/logs` | Ring buffer entries (query: `start`, `limit`) |
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

**Acceptance:**

| Test | Method |
|---|---|
| `system.getStatus` | ✅ `curl -X POST http://esp32/api/command -d '{"id":0,"cmd":"system.getStatus"}'` → JSON with `brt`, `vlv`, `mv`, `temp` |
| `serial.ping` | ✅ `curl ... -d '{"cmd":"serial.ping"}'` → `{"status":"ok"}` |
| `burette.fill` | ✅ `curl ... -d '{"id":1,"cmd":"burette.fill","speed_ml_min":10}'` → ACK, then result |
| `burette.doseVolume 1ml@5` | ✅ `curl ... -d '{"id":2,"cmd":"burette.doseVolume","volume_ml":1,"speed_ml_min":5}'` → ACK, motor runs, result |
| `burette.stop` | ✅ `curl ... -d '{"id":3,"cmd":"burette.stop"}'` → motor stops, original command returns `"stopped"` |
| `burette.getStatus` | ✅ returns `"idle"` or `"working"` |
| `valve.setPosition` | ✅ `curl ... -d '{"id":4,"cmd":"valve.setPosition","position":"input"}'` |
| Two-phase protocol | ✅ ACK sent within 1s, result sent within 600s |
| Error: `invalid_params` | ✅ `curl ... -d '{"id":5,"cmd":"burette.doseVolume","volume_ml":-1,"speed_ml_min":5}'` → `"error"/"invalid_params"` |
| Error: `burette_busy` | ✅ two simultaneous commands → second returns `"burette_busy"` |
| `temp: null` | ✅ when DS18B20 not connected |
| `/api/status` broadcast | ✅ `curl /api/status` → `{"ts","mv","temp","vlv","brt":{"sts","vl","spd"},"meta":{"ip":...}}` |
| SSE `event: status` | ✅ `curl -N /api/events` → stream every ~300ms, compact format |
| SSE `event: debug` | ✅ debug data when available |
| SSE `event: log` | ✅ log entries when generated |
| HTTP blocks for long ops | ✅ `POST /api/command` for dose returns only after motor completes (not suitable from browser — documented) |
| All 31 commands respond | ✅ every command from the table returns correct JSON |

**Reference files:** `legacy/src/command.cpp`, `legacy/src/handlers/`, `prototype/src/webserver.rs`, `prototype/src/status.rs`, `prototype/src/logger.rs`

---

### Phase 4: Network — WiFi + BLE

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
- NUS service: `6E400001-B5A3-F393-E0A9-E50D24DCCA9E`
- RX characteristic: `6E400002`, write / write-without-response
- TX characteristic: `6E400003`, notify
- Advertising: `EcoTiter-XXXX`, connectable, every 1s
- Command queue: bounded `mpsc` (8 entries), drained from main loop
- Zombie defense level 1: 5 consecutive failed notifications → disconnect + flush + restart advertising
- Zombie defense level 2: `connected_count() == 0` but internal flag says connected → kill
- BLE notify thread: `std::thread` (8 KB stack), `recv` from mpsc → `notify()`

**Acceptance:**

| Test | Method |
|---|---|
| AP `EcoTiter-AP` visible | 👁 phone WiFi scan shows AP |
| Phone gets IP (192.168.4.x) | 👁 phone connected to AP, IP assigned |
| Captive portal triggers | 👁 phone auto-opens `/wifi` page |
| WiFi form → STA connect | 👁 enter router SSID/password → ESP32 restarts in STA mode |
| `esp_restart()` → STA reconnect | 👁 boots, connects to saved WiFi |
| DNS responder | ✅ `curl http://192.168.4.1/generate_204` → 302 |
| `/api/status` from browser | ✅ phone browser shows compact JSON |
| SSE from browser | ✅ `curl -N http://esp32/api/events` on PC |
| BLE advertising `EcoTiter-XXXX` | 👁 nRF Connect / BLE scanner |
| BLE connect + write JSON command | 👁 motor responds via BLE |
| USB heartbeat timeout → BLE takeover | 👁 unplug USB → BLE becomes active transport |
| LED: advertising (ON), connected (1Hz) | 👁 visible |
| No Guru Meditation | ✅ 0 panics (tested >60s) |

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
| WiFi STA connected, streaming data, BLE advertising + connected | Both active. NimBLE + lwIP share radio via ESP_IDF_COEX. |

**Known ESP32 limitation:** BT/WiFi coexistence can cause throughput degradation on both radios, but must NOT cause panics, disconnects, or command loss.

**Method:** `ping -t` to ESP32 over WiFi (100ms interval), while simultaneously sending BLE notifications every 100ms. Run 60s. Check: ping loss < 5%, no BLE disconnect.

---

### Phase 5: Integration — Motor Task + Full Main Loop

**Files:** `src/motor_task.rs` `src/main.rs` (complete)

**Motor task:**
```rust
// src/motor_task.rs
pub fn spawn(mut stepper: RmtStepper<'static>) {
    std::thread::Builder::new()
        .stack_size(4096)
        .name("motor".into())
        .spawn(move || {
            loop {
                let target = TARGET_POSITION.load(Ordering::Acquire);
                let current = CURRENT_POSITION.load(Ordering::Acquire);
                if target != current {
                    let ramp = compute_ramp(/* differences... */);
                    stepper.move_steps(&ramp).ok();
                    CURRENT_POSITION.store(target, Ordering::Release);
                } else {
                    std::thread::sleep(Duration::from_millis(10));
                }
            }
        })
        .expect("motor task spawn");
}
```

**Full main loop (from `project.md:135-166` + `coding_style.md:§9`):**
```rust
fn main() {
    link_patches();
    EspLogger::initialize_default();
    unsafe { esp_task_wdt_deinit(); }

    let peripherals = Peripherals::take().unwrap();

    // Init all hardware drivers
    let stepper = RmtStepper::new(...);
    let mut adc = AdcDriver::new(...);
    let wifi_mgr = WifiManager::new(...);
    let webserver = WebServer::new(...);
    spawn_motor_task(stepper);
    spawn_temperature_thread(gpio33);

    loop {
        // Transport state machine (USB ↔ BLE priority)
        transport_process(&serial, &ble);

        // SSE push (non-blocking socket write)
        sse_push(&broadcast);

        // WiFi processing (DNS poll, reconnect)
        wifi_mgr.process();

        // ADC sample (1 read per tick, ~30µs)
        adc_sample(&mut adc);

        // LED process (blink SM)
        led_process();

        // Pacing tick (10ms)
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

**Acceptance:**
- ✅ Tauri auto-connect: scan USB ports → `{"id":0,"cmd":"system.getStatus"}` → handshake → `{"cmd":"burette.cal.get"}`
- ✅ Tauri manual control: Fill / Empty / Dose(1ml@5) / Rinse(3 cycles) / Valve toggle / Stop — all buttons work
- ✅ Dashboard in Tauri: mV, temp, burette volume/speed update in real time (via serial broadcast)
- ✅ Titration: Start Method → prepare (valve input → fill → valve output) → dose → stabilize → measure → EQ detection → results CSV
- ✅ BLE transport: Tauri `device_discover` → `device_connect` → commands work over BLE
- ✅ USB hotplug: unplug/replug USB → Tauri detects, reconnects automatically
- 👁 Reboot detection: ESP32 reset → Tauri detects via broadcast `ts` rollback

**Reference files:** `prototype/src/main.rs` (hardware init + loop pattern), `legacy/src/main.cpp` (transport SM, safety net, homing)

---

### Phase 6: TMC2209 UART Driver (deferred)

**Files:** `src/infrastructure/drivers/stepper_drv.rs` `src/infrastructure/drivers/tmc_regs.rs`

- TMC2209 register map: `GCONF`, `GSTAT`, `IOIN`, `CHOPCONF`, `PWMCONF`, `COOLCONF`, `SGTHRS`, `DRV_STATUS`, `TPWMTHRS`
- UART single-wire half-duplex on UART1 (GPIO17/GPIO16, 115200 baud)
- Shadow register cache (write-through)
- `stepperDrv_init()`: test_connection(), dump version, configure StealthChop
- `stepperDrv_read_sg_result()`, `stepperDrv_read_drv_status()`
- `stepperDrv_set_current()`, `stepperDrv_set_microsteps()`

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
- ✅ All 31 Tauri commands round-trip correctly
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
- `emergencyStop` must NOT be exposed in the external command API

## Notes

- WDT is disabled at boot (`esp_task_wdt_deinit()`) — RMT `send_and_wait()` blocks >250ms
- Main loop must never block — all blocking I/O in dedicated threads
- PinDriver in `esp-idf-hal` v0.46: `PinDriver<'d, MODE>` has 1 type parameter (not 2), no `RmtOutputPin` trait exists
- GPIO constructors have private fields — use `peripherals.pins.gpioXX.degrade_output()` at runtime
- EN pin for TMC2209 is active LOW — must be set immediately in constructor
- All heap measurements from prototype Phase 0: 184 KB free, 108 KB largest block — ample margin
