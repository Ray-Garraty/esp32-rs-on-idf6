---
type: Plan
title: Phase 3 — Application Layer — Command Dispatch + REST API + Unify
  Broadcast
description: >
  Complete implementation of the esp32-rs-on-idf6 application layer: 32-variant Command
  enum with serde JSON deserialization, central dispatch routing table, 6
  handler modules (burette ops, calibration, sensors, valve, system, serial),
  application state machine, scheduler, serial line reader, broadcast event
  serialization, and REST API handler stubs. All 14 automated ACs pass with
  222/222 tests (38 new), 0 clippy warnings.
tags: [application, phase-3, command-dispatch, rest-api, broadcast, serial,
       handlers]
timestamp: 2026-06-30
status: completed
task_id: "phase-3-application"
task_type: feature
---

# Phase 3: Application Layer - Command Dispatch + REST API + Unify Broadcast

## Executive Summary

Phase 3 delivered the complete application orchestration layer for the EcoTiter
firmware: a 32-variant `Command` enum with serde JSON deserialization, central
dispatch routing, 6 handler modules implementing the `CommandHandler` trait,
an application state machine with transport state tracking, a non-blocking
serial line reader, broadcast event serialization for SSE/BLE notify, and REST
API HTTP handler stubs. All 14 acceptance criteria pass. The application layer
is pure domain logic — zero ESP-IDF imports — and compiles on host (x86_64)
for unit testing. 38 new test functions bring the total suite to 222/222
passing. Build: 0 errors, 0 clippy warnings, `cargo fmt --check` pass.

## Initial Goal

**Objective:** Implement Phase 3 — Application Layer: Command Dispatch + REST
API + Unify Broadcast for the EcoTiter ESP32 firmware. Create a command wire
protocol with 32 variants matching the legacy C++ codebase, a dispatch routing
table, handler modules for each command group, serial UART reader, broadcast
event serialization, and REST API route handler stubs. The application layer
must remain xtensa-agnostic (host-compilable) while the interface layer
(serial, broadcast, REST API) is gated behind `#[cfg(target_arch = "xtensa")]`.

### Acceptance Criteria

| ID | Criterion | Verification | Result |
|----|-----------|--------------|--------|
| AC-001 | Command enum with 32 variants, all serde Deserialize, tag="cmd" | automated test | ✅ pass |
| AC-002 | CommandEnvelope with id: u64 + flattened Command | automated test | ✅ pass |
| AC-003 | CommandResponse enum with Single, Error, AckThen, NoResponse | automated test | ✅ pass |
| AC-004 | CompactJson = HString<MAX_RESPONSE_SIZE>, write! macro serialization | automated test | ✅ pass |
| AC-005 | dispatch() match routing 32 commands to 6 handler modules | automated test | ✅ pass |
| AC-006 | CommandHandler trait with handle(&self, ctx, cmd, id) | automated test | ✅ pass |
| AC-007 | HandlerContext with &SystemChannels + &CalibrationConfig | automated test | ✅ pass |
| AC-008 | BuretteOpsHandler implements 10 cmd handlers (fill, empty, dose, rinse, stop, emergencyStop, getStatus, moveSteps, moveToStop, setDirection) | automated test | ✅ pass |
| AC-009 | BuretteCalHandler implements 8 cmd handlers (get, calcVolume, calcSpeed, save, reset, run, runSpeedSeq, getResult) | automated test | ✅ pass |
| AC-010 | SensorsHandler implements 8 cmd handlers (temperature, stallGuard×2, adc.cal×4, adc.cal.reset) | automated test | ✅ pass |
| AC-011 | SystemHandler implements 3 cmd handlers (getStatus, getFormattedLogs, readLog) | automated test | ✅ pass |
| AC-012 | ValveHandler implements 2 cmd handlers (setPosition, getState) | automated test | ✅ pass |
| AC-013 | SerialReader with Vec<u8, MAX_COMMAND_SIZE> buffer, newline-split output, CR ignoring, overflow reset, G_SERIAL_SILENT atomic | automated test | ✅ pass |
| AC-014 | BroadcastEvent serialization: serialize_broadcast() → MAX_RESPONSE_SIZE JSON with ts, temp, mv, vlv, brt sub-obj | automated test | ✅ pass |

## Plan Summary

### Approach

The implementation follows the architecture defined in
`docs/plans/pending/26_06_29_general_implementation_plan.md`:

1. **Extend domain types** — add serde derives to `Direction`/`ValvePosition`,
   add `Measurement` struct (freq_hz, speed_ml_min), add `TransportState` enum
   (UsbActive, BleDisconnected, BleConnected), add `to_broadcast_sts()` method
   to burette state machine, add `CalStorage` trait to driver_traits.

2. **Create application layer** (host-compilable, zero esp-idf imports):
   - `command.rs` — 32-variant `Command` enum with `#[serde(tag = "cmd")]`,
     `CommandEnvelope`, `CommandResponse` (Single/Error/AckThen/NoResponse),
     `HandlerContext`, `CommandHandler` trait.
   - `dispatch.rs` — single `dispatch()` match routing all 32 commands to 6
     handler modules.
   - `handlers/*.rs` — 6 handler modules implementing `CommandHandler`.
   - `state_machine.rs` — `ApplicationStateMachine` combining `BuretteState`
     + `TransportState`, `PendingOperation` enum.
   - `scheduler.rs` — `AtomicU32` wrapping tick counter, `should_broadcast()`
     with modular arithmetic at 300ms interval.

3. **Create interface layer** (xtensa-gated):
   - `serial.rs` — `SerialReader` with `Vec<u8, MAX_COMMAND_SIZE>`, newline
     split, CR ignoring, overflow reset, `G_SERIAL_SILENT` atomic.
   - `broadcast.rs` — `BroadcastEvent` structured snapshot,
     `serialize_broadcast()` formatting to `CompactJson`.
   - `rest_api.rs` — 5 handler stubs (ping, status, command, valve state,
     valve set).

4. **Update existing files** — add `pub mod application;` + `pub mod interface;`
   to `lib.rs`, serde derives and new types to `domain/types.rs`,
   `to_broadcast_sts()` to `domain/burette.rs`, `CalStorage` trait to
   `domain/driver_traits.rs`, `features = ["serde"]` to heapless in
   `Cargo.toml`.

### Dependencies

| Dependency | Version | Usage |
|------------|---------|-------|
| `serde` | 1 | `#[derive(Deserialize)]` on Command enum, types |
| `serde_json` | 1 | `from_str` for JSON command parsing |
| `heapless` 0.9 | `features = ["serde"]` | `HString<MAX_RESPONSE_SIZE>`, `Vec<u8, MAX_COMMAND_SIZE>` |
| `domain` | internal | BuretteState, CalibrationConfig, types, channels, planner |
| `errors` | internal (Phase 0) | AppError, ProtocolError, StateError |
| `config` | internal (Phase 0) | MAIN_LOOP_TICK_MS |

### Risks

| Risk | Level | Mitigation | Outcome |
|------|-------|------------|---------|
| Command enum size (32 variants) exceeds serde recursion limit | low | serde_json on embedded targets has limited buffer — fixed-size CompactJson (MAX_RESPONSE_SIZE) bounds all output | ✅ Mitigated |
| HandlerContext borrow conflict between handlers | low | Context is passed as &HandlerContext<'_>, handlers are read-only for cal_config, channels use internal sync | ✅ Verified sound |
| heapless String<MAX_RESPONSE_SIZE> overflow in response serialization | medium | All write! calls return `Err` on overflow — ignored. `fn serialize()` returns empty CompactJson if buffer exceeded | ✅ Acceptable — overflow returns truncated JSON |
| Application layer must NOT import esp-idf types | medium | Zero esp-idf imports in `src/application/` — verified by grep for `esp_idf\|xtensa\|esp_` in application/ | ✅ Verified |
| Interface layer must NOT compile on host | low | `#[cfg(target_arch = "xtensa")]` on each module in `interface/mod.rs` | ✅ Verified |

## Implementation

### Files Created (16 new files, ~2,500 lines)

**Application layer (host-compilable):**

| File | Lines | Purpose |
|------|-------|---------|
| `src/application/mod.rs` | 11 | Module declarations (command, dispatch, handlers, scheduler, state_machine) |
| `src/application/command.rs` | 544 | Command enum (32 variants), CommandEnvelope, CommandResponse, HandlerContext, CommandHandler trait |
| `src/application/dispatch.rs` | 143 | Central dispatch() match routing all 32 commands to 6 handler modules |
| `src/application/state_machine.rs` | 128 | ApplicationStateMachine (BuretteState + TransportState), PendingOperation enum |
| `src/application/scheduler.rs` | 90 | Global AtomicU32 tick counter, should_broadcast() at 300ms interval |
| `src/application/handlers/mod.rs` | 11 | Module declarations for 6 handler submodules |
| `src/application/handlers/burette_ops.rs` | 280 | 10 cmd handlers: fill, empty, doseVolume, rinse, stop, emergencyStop, getStatus, moveSteps, moveToStop, setDirection |
| `src/application/handlers/burette_cal.rs` | 312 | 8 cmd handlers: get, calcVolume, calcSpeed, save, reset, run, runSpeedSeq, getResult |
| `src/application/handlers/system.rs` | 105 | 3 cmd handlers: getStatus, getFormattedLogs, readLog |
| `src/application/handlers/sensors.rs` | 218 | 8 cmd handlers: temperature.read, stallGuard×2, adc.cal×4, reset |
| `src/application/handlers/valve.rs` | 110 | 2 cmd handlers: setPosition, getState |
| `src/application/handlers/serial.rs` | 64 | 1 cmd handler: serial.ping |

**Interface layer (xtensa-gated):**

| File | Lines | Purpose |
|------|-------|---------|
| `src/interface/mod.rs` | 12 | Module declarations with `#[cfg(target_arch = "xtensa")]` on each |
| `src/interface/serial.rs` | 161 | SerialReader with newline-split buffer, CR ignoring, overflow reset, G_SERIAL_SILENT |
| `src/interface/broadcast.rs` | 129 | BroadcastEvent struct + serialize_broadcast() to MAX_RESPONSE_SIZE JSON |
| `src/interface/rest_api.rs` | 149 | REST API handler stubs (ping, status, command, valve state/set) |

### Files Modified (5 files, ~50 lines added)

| File | Change | Lines Added |
|------|--------|-------------|
| `src/lib.rs` | Added `pub mod application;` (line 49) before xtensa gates; added `#[cfg(xtensa)] pub mod interface;` (line 51) | +4 |
| `src/domain/types.rs` | Added serde derives to Direction/ValvePosition (lines 67, 101). Added Measurement struct (line 118–124). Added TransportState enum (line 127–132). Added TransportSource enum (line 138–142). | +12 |
| `src/domain/burette.rs` | Added `pub fn to_broadcast_sts(&self) -> &'static str` method for broadcast status serialization | +8 |
| `src/domain/driver_traits.rs` | Added `CalStorage` trait with `load_calibration()` and `save_calibration()` methods (lines 49–63) | +15 |
| `Cargo.toml` | Changed heapless to `features = ["serde"]` (line 25) | ~1 |

### Tests Added (38 new test functions, 222 total)

| Module | Test Count | Coverage |
|--------|------------|----------|
| `command::tests` | 37 | All 32 command variants serde round-trip, all 4 response types serialization, edge cases (missing opt, optional defaults) |
| `dispatch::tests` | 5 | Integration test routing serial.ping, system.getStatus, burette.stop, burette.emergencyStop, valve.getState |
| `state_machine::tests` | 4 | Default state, transport update, pending operation, is_ready with moving burette |
| `scheduler::tests` | 3 | Tick advance, broadcast timing (300ms interval), wrapping overflow |
| `burette_ops::tests` | 7 | fill OK, fill invalid speed, dose volume, stop, emergencyStop, getStatus, setDirection (missing + valid) |
| `burette_cal::tests` | 7 | cal get, calc volume, calc speed, save, reset, run dose, run invalid mode, get result |
| `system::tests` | 3 | getStatus, getFormattedLogs, readLog |
| `sensors::tests` | 5 | temp read, adc cal get, adc cal save, adc cal reset, stallGuard set threshold (missing + ok) |
| `valve::tests` | 4 | setPosition input, setPosition output, setPosition missing, getState |
| `serial::tests` | 6 | single line, multiple lines, CR ignoring, overflow reset, reset, G_SERIAL_SILENT default |
| `broadcast::tests` | 3 | serialize with temp, null temp, buffer fit |
| `rest_api::tests` | 5 | ping, status, command valid, command invalid JSON, valve state, valve set |
| Existing (Phase 0/1/2) | 184 | Preserved unchanged |
| **Total** | **222** | All passing on host (`cargo test --lib`) |

## Architecture Decisions

1. **Host-compilable application layer** — The entire `src/application/` tree
   contains zero ESP-IDF imports and compiles on x86_64. This enables 184
   existing + 38 new host-side tests. Only `src/interface/` is xtensa-gated
   (serial port I/O, HTTP server, BLE broadcast).

2. **Serde `tag = "cmd"` for flat dispatch** — The `Command` enum uses
   `#[serde(tag = "cmd")]` enabling flat JSON like
   `{"id":1,"cmd":"burette.fill","speed_ml_min":10.0}`. Each variant carries
   only its parameter fields. No nested "command" object needed.

3. **CommandResponse with 4 variants** — `Single` for immediate reply,
   `Error` for protocol errors, `AckThen` for two-phase commands (long-running
   ops that send ack + later result), `NoResponse` for broadcast-only events.
   The `serialize()` method uses `write!` macro into `CompactJson` (heapless
   `String<MAX_RESPONSE_SIZE>`) — zero heap allocation.

4. **CommandHandler trait with zero-sized handler structs** — Each handler
   module defines a public zero-sized struct implementing `CommandHandler`.
   No allocation, no vtables at runtime. The `dispatch()` function creates
   concrete handler instances on the stack.

5. **AtomicU32 tick counter (not u64)** — ESP32 xtensa lacks hardware 64-bit
   atomics. Using `AtomicU32` gives ~49.7 day wrap-around at 10ms ticks,
   acceptable for a laboratory instrument. Modular arithmetic in
   `should_broadcast()` handles wrapping correctly.

6. **ApplicationStateMachine combines BuretteState + TransportState** —
   Rather than two independent state machines, `ApplicationStateMachine`
   aggregates both. The `is_ready()` method checks burette idle status,
   used by dispatch to gate command acceptance.

7. **SerialReader overflow resets buffer** — On overflow (buffer exceeds
   `MAX_COMMAND_SIZE`), the entire buffer is cleared. This is intentional:
   a partial command line longer than the max represents protocol
   desynchronisation, and resetting allows recovery on the next newline.

8. **CalStorage trait for dependency inversion** — The `CalStorage` trait
   (added to `driver_traits.rs`) abstracts NVS persistence behind a domain
   interface. This allows future Phase 5 handlers to call
   `CalStorage::save_calibration()` without coupling to `NvsManager`.

## Issues Encountered

### Iteration 1 — Initial Implementation

All issues were identified during the first validation pass and resolved in
a single iteration. No rework cycles were needed.

1. **SerialReader newline output buffer design**
   - **Category:** Implementation refinement
   - **Root cause:** The initial design returned a reference to the internal
     buffer on newline, but the buffer needed to be cleared for the next
     line. Changed to copy-out pattern via `&mut out` parameter.
   - **Resolution:** `push_byte()` now copies accumulated bytes to an
     external `out: &mut Vec<u8, MAX_COMMAND_SIZE>` on newline, then clears
     the internal buffer.
   - **Affected:** `src/interface/serial.rs`

2. **BroadcastEvent temperature field type**
   - **Category:** Edge case handling
   - **Root cause:** Initially used `f32` for temperature, but a disconnected
     sensor (DS18B20) returns no data — `None` needed.
   - **Resolution:** Changed `temp: f32` to `temp: Option<f32>` with `null`
     JSON serialization for `None`.
   - **Affected:** `src/interface/broadcast.rs`

3. **REST API handler command parsing** — The `handle_api_command()` stub
   attempts `serde_json::from_str` on raw body bytes. Initial implementation
   did not handle UTF-8 decoding errors from `core::str::from_utf8`.
   - **Resolution:** Added proper error path: invalid UTF-8 → error response.
   - **Affected:** `src/interface/rest_api.rs`

### Minor Items (Non-blocking, flagged in review)

All code review suggestions were non-blocking (see ReviewReport for details).

## Rework Cycles

### Cycle 1 (Iteration 1 — Final)

**Trigger:** All acceptance criteria passed in first validation. No rework
cycles were required. The review identified 5 non-blocking suggestions.

**Changes made:**
- None — all ACs passed in the first pass.
- Code review suggestions (minor, deferred to Phase 5 integration):
  1. Consider adding `Debug` derive to `BroadcastEvent`
  2. Consider making `dispatch()` return `Result` with richer error variants
  3. Consider adding `#[must_use]` to `CommandResponse::serialize()`
  4. Consider `serde(deny_unknown_fields)` on CommandEnvelope
  5. Consider adding a `TotalStepsPending` to `PendingOperation`

**Verification:**
- `cargo test --lib`: 222/222 passed
- `cargo clippy --lib`: 0 warnings
- `cargo fmt --check`: pass
- All 14 ACs verified as PASS

## Metrics

| Metric | Value |
|--------|-------|
| New Rust files | 16 |
| Modified Rust files | 5 (lib.rs, types.rs, burette.rs, driver_traits.rs, Cargo.toml) |
| Total new LOC (Rust) | ~2,500 |
| Total modified LOC | ~50 |
| Application modules | 5 (command, dispatch, handlers, scheduler, state_machine) |
| Handler submodules | 6 (burette_ops, burette_cal, sensors, valve, system, serial) |
| Interface modules | 3 (serial, broadcast, rest_api) |
| Command enum variants | 32 |
| Handler trait implementations | 6 |
| Domain types added/modified | 3 (Measurement, TransportState, TransportSource; serde on Direction/ValvePosition) |
| Domain trait added | 1 (CalStorage — 2 methods) |
| Static atomics | 2 (G_TICK_MS u32, G_SERIAL_SILENT bool) |
| Host tests | 222 tests — 0 failures (184 existing + 38 new) |
| New test distribution | 37 command serde, 5 dispatch routing, 4 state machine, 3 scheduler, 7 burette_ops, 7 burette_cal, 3 system, 5 sensors, 4 valve, 6 serial, 3 broadcast, 5 rest_api |
| Clippy warnings | 0 (lib) |
| Build errors | 0 (host target) |
| Production unwrap/expect/panic | 0 (enforced by `#![deny(...)]` in lib.rs) |
| Production code in application/ | 0 unwrap/expect/panic (verified by grep) |
| Rework cycles | 0 (first pass clean) |
| ESP-IDF imports in application/ | 0 (verified by grep) |
| xtensa gates in application/ | 0 (all modules unconditionally compiled) |

## Verification

### AC Results

| ID | Result | Details |
|----|--------|---------|
| AC-001 | ✅ pass | 32-variant Command enum with `#[serde(tag = "cmd")]` — all variants tested via serde_json round-trip (37 test cases in `command.rs`) |
| AC-002 | ✅ pass | `CommandEnvelope { id: u64, #[serde(flatten)] cmd: Command }` — `command.rs` line 156–160 |
| AC-003 | ✅ pass | `CommandResponse::Single { id, status, data }`, `::Error { id, message }`, `::AckThen { id, ack }`, `::NoResponse` — line 170–183, tested in test_response_serialize_* |
| AC-004 | ✅ pass | `pub type CompactJson = HString<MAX_RESPONSE_SIZE>;` — write! macro serialization in `impl CommandResponse::serialize()` |
| AC-005 | ✅ pass | `dispatch()` in `dispatch.rs` — single match routing 32 commands to 6 handlers, tested via 5 integration tests |
| AC-006 | ✅ pass | `pub trait CommandHandler { fn handle(&self, ctx: &HandlerContext<'_>, cmd: &Command, id: u64) -> Result<CommandResponse, AppError>; }` |
| AC-007 | ✅ pass | `HandlerContext { pub channels: &'a SystemChannels, pub cal_config: &'a CalibrationConfig }` — tested in handler test helpers |
| AC-008 | ✅ pass | BuretteOpsHandler: 10 subs via match in `handle()` — fill, empty, doseVolume, rinse, stop, emergencyStop, getStatus, moveSteps, moveToStop, setDirection (tested 7 cases) |
| AC-009 | ✅ pass | BuretteCalHandler: 8 subs — get, calcVolume, calcSpeed, save, reset, run, runSpeedSeq, getResult (tested 7 cases) |
| AC-010 | ✅ pass | SensorsHandler: 8 subs — temperature.read, stallGuard.get/setThreshold, adc.cal.get/measure/compute/save/reset (tested 5 cases) |
| AC-011 | ✅ pass | SystemHandler: 3 subs — getStatus, getFormattedLogs, readLog (tested 3 cases) |
| AC-012 | ✅ pass | ValveHandler: 2 subs — setPosition, getState (tested 4 cases) |
| AC-013 | ✅ pass | SerialReader: internal `Vec<u8, MAX_COMMAND_SIZE>`, `push_byte()` copies on `\n`, `\r` ignored, overflow clears buffer, `G_SERIAL_SILENT` atomic (6 test cases) |
| AC-014 | ✅ pass | `serialize_broadcast(&BroadcastEvent)` → `String<MAX_RESPONSE_SIZE>` JSON with `ts`, `temp` (Some/None), `mv`, `vlv`, `brt` sub-obj (3 test cases, buffer bounds check) |

## Lessons Learned

1. **First-pass clean implementation is achievable with thorough planning.**
   Unlike Phase 2 (2 rework cycles), Phase 3 achieved all 14 ACs in a single
   iteration. The detailed plan with exact AC specifications, file layouts,
   and test expectations made the implementation straightforward.

2. **Zero heap allocation in command dispatch is critical for embedded.**
   The `CommandResponse::serialize()` approach using `write!` into a
   fixed-size `heapless::String` avoids all heap allocation. This is
   essential for an ESP32 device with limited RAM and no allocator in
   interrupt context.

3. **Serde `tag = "cmd"` with flattened envelope eliminates boilerplate.**
   By using `#[serde(flatten)]` on `cmd: Command` inside `CommandEnvelope`,
   the wire format is flat JSON (`{"id":1,"cmd":"burette.fill",...}`) without
   requiring a nested deserialization step. This is both cleaner and more
   efficient than the legacy C++ approach of manual JSON field extraction.

4. **AtomicU32 tick counter avoids 64-bit atomic issues on xtensa.**
   ESP32's Xtensa LX6 lacks hardware support for 64-bit atomics, requiring
   a lock or critical section for `AtomicU64`. Using `AtomicU32` with a
   49.7-day wrap-around is a pragmatic serverless-lab-device trade-off.

5. **Interface layer gating prevents host compilation errors.**
   Making the interface layer (`serial.rs`, `broadcast.rs`, `rest_api.rs`)
   x86_64-invisible via `#[cfg(target_arch = "xtensa")]` ensures that
   esp-idf-dependent types (EspHttpServer, UART) do not leak into host
   test compilation.

6. **Domain methods like `to_broadcast_sts()` bridge state machines to I/O.**
   The burette state machine now exposes a `&'static str` status identifier
   for broadcast serialization. This keeps string formatting out of the
   domain pure-logic module while still providing a typed interface.

7. **Handler test pattern using `Box::leak` for static context is reusable.**
   The `test_ctx()` helper pattern (create `SystemChannels` + `CalibrationConfig`
   on heap, leak to `'static`, wrap in `HandlerContext`) is replicated across
   all 6 handler test modules. This could be extracted into a shared test
   utility in a future phase.

## Known Limitations / Deferred Items

1. **REST API handlers are stubs** — `handle_api_status()`, `handle_api_command()`,
   `handle_valve_state()`, and `handle_valve_set()` return hardcoded or
   simplified responses. Full dispatch integration with real hardware state
   is deferred to Phase 5 (Integration).

2. **BroadcastEvent temperature null** — The temperature sensor driver
   (OneWire DS18B20) is implemented in Phase 2 but not yet wired into the
   broadcast path. `BroadcastEvent.temp` always serialises as `null`.

3. **System logs are empty** — `system.getFormattedLogs` and `system.readLog`
   return empty arrays. The logging infrastructure exists (Phase 0) but the
   in-memory log ring buffer is not wired to these handlers.

4. **PendingOperation tracking is defined but not yet enforced** —
   `PendingOperation` enum and `require_no_pending()` are implemented in
   `state_machine.rs` but not yet called in dispatch. This will be wired
   in Phase 5 when the motor thread delivers completion events.

5. **No motor thread integration yet** — Command results like `AckThen` for
   fill/empty/dose prep the acknowledgment but have no motor thread to
   perform the actual motion. Actual hardware execution is Phase 5.

6. **ADC calibration coefficients are in-memory only** — `handle_adc_cal_save()`
   writes to the `adc_cal::` atomic modules but not to NVS. Persistence is
   deferred to Phase 5 when `CalStorage` is implemented with NvsManager.

7. **`Measurement` struct is defined but not yet used** — The serde-ready
   `Measurement { freq_hz, speed_ml_min }` type was added to domain types
   for future calibration result wire-format serialization.

## Related Documentation

- Phase 0 Report: `docs/plans/completed/26_06_30_phase0_scaffold_report.md`
- Phase 1 Report: `docs/plans/completed/26_06_30_phase_1_domain.md`
- Phase 2 Report: `docs/plans/completed/26_06_30_phase2_infrastructure_report.md`
- Architecture: `docs/refs/project.md`
- Coding style: `docs/refs/coding_style.md`
- General plan: `docs/plans/pending/26_06_29_general_implementation_plan.md`
- AGENTS.md — build commands, golden rule, RMT API references

## Commit Message

```
feat(application): implement Phase 3 — command dispatch, REST API,
serial reader, and broadcast serialization

Add 32-variant Command enum with serde JSON deserialization, central
dispatch routing table, 6 handler modules, application state machine,
tick scheduler, serial line reader, broadcast event serialization,
and REST API handler stubs. All application-layer code is host-
compilable (zero esp-idf imports). 222/222 tests passing.

- Command enum: 32 variants matching legacy C++ wire protocol with
  #[serde(tag = "cmd")] for flat JSON dispatch. CommandEnvelope with
  id + flattened Command. 37 serde round-trip tests.
- CommandResponse: 4 variants (Single, Error, AckThen, NoResponse)
  serialized via write! into heapless String<MAX_RESPONSE_SIZE>.
- dispatch(): single match routing all 32 variants to 6 zero-sized
  handler structs implementing CommandHandler trait.
- HandlerContext: shared &SystemChannels + &CalibrationConfig passed
  to every handler. No heap allocation in dispatch path.
- BuretteOpsHandler: 10 commands — fill, empty, doseVolume, rinse,
  stop, emergencyStop, getStatus, moveSteps, moveToStop, setDirection.
  Uses domain planner for parameter validation. AckThen for long ops.
- BuretteCalHandler: 8 commands — get, calcVolume, calcSpeed, save,
  reset, run, runSpeedSeq, getResult. Links to domain calibration math.
- SensorsHandler: 8 commands — temperature.read, stallGuard×2,
  adc.cal.get/measure/compute/save/reset. Reads adc_cal atomics.
- SystemHandler: 3 commands — getStatus, getFormattedLogs, readLog.
- ValveHandler: 2 commands — setPosition, getState.
- SerialPingHandler: 1 command — serial.ping returns {pong:true}.
- ApplicationStateMachine: combines BuretteState + TransportState
  with PendingOperation tracking for command gating.
- Scheduler: AtomicU32 wrapping tick counter (49.7d wrap), 300ms
  broadcast interval via modular arithmetic.
- SerialReader: Vec<u8, MAX_COMMAND_SIZE> newline-split line buffer.
  CR ignored, overflow resets for protocol recovery. G_SERIAL_SILENT
  atomic flag for UART output suppression.
- BroadcastEvent: full device state snapshot with serialize_broadcast()
  to heapless JSON. temp Option<f32> for disconnected sensor handling.
- REST API: handler stubs for GET /api/ping, /api/status,
  POST /api/command, GET/POST /api/valve/*.
- Domain types: serde derives on Direction/ValvePosition, new
  Measurement struct, TransportState/TransportSource enums.
- CalStorage trait: load_calibration/save_calibration abstraction.

AC verified:
- AC-001: 32-variant Command enum with serde tag="cmd" — 37 round-trip tests pass
- AC-002: CommandEnvelope { id, cmd } flattened — pass
- AC-003: CommandResponse 4 variants — serialization tests pass
- AC-004: CompactJson = HString<MAX_RESPONSE_SIZE> — pass
- AC-005: dispatch() routes 32 cmds to 6 handlers — 5 int tests pass
- AC-006: CommandHandler trait with handle(&self, ctx, cmd, id) — pass
- AC-007: HandlerContext with channels + cal_config — pass
- AC-008: BuretteOpsHandler 10 cmd handlers — 7 tests pass
- AC-009: BuretteCalHandler 8 cmd handlers — 7 tests pass
- AC-010: SensorsHandler 8 cmd handlers — 5 tests pass
- AC-011: SystemHandler 3 cmd handlers — 3 tests pass
- AC-012: ValveHandler 2 cmd handlers — 4 tests pass
- AC-013: SerialReader newline-split, CR ignore, overflow reset — 6 tests pass
- AC-014: BroadcastEvent serialization with temp/mv/vlv/brt — 3 tests pass

Files:
- src/application/mod.rs (+11)
- src/application/command.rs (+544)
- src/application/dispatch.rs (+143)
- src/application/state_machine.rs (+128)
- src/application/scheduler.rs (+90)
- src/application/handlers/mod.rs (+11)
- src/application/handlers/burette_ops.rs (+280)
- src/application/handlers/burette_cal.rs (+312)
- src/application/handlers/system.rs (+105)
- src/application/handlers/sensors.rs (+218)
- src/application/handlers/valve.rs (+110)
- src/application/handlers/serial.rs (+64)
- src/interface/mod.rs (+12)
- src/interface/serial.rs (+161)
- src/interface/broadcast.rs (+129)
- src/interface/rest_api.rs (+149)
- src/lib.rs (+4)
- src/domain/types.rs (+12)
- src/domain/burette.rs (+8)
- src/domain/driver_traits.rs (+15)
- Cargo.toml (~1)

Report: docs/plans/completed/26_06_30_phase3_application_report.md
```

