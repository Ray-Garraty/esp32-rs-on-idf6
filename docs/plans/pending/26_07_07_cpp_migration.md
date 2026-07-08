---
type: Plan
title: C++23 Migration — Step-by-Step Execution Plan
description: >
  Phased migration of legacy Rust firmware to C++23 + ESP-IDF v6 on ESP32-S3.
  Each step is self-contained with acceptance criteria gated by a hardware smoke
  test (build + flash + 30s serial monitor, no Guru/WDT/panic).
tags: [migration, cpp23, esp-idf-v6, esp32-s3, stepper, network, ble]
timestamp: 2026-07-08
status: pending
---

# C++23 Migration — Step-by-Step Execution Plan

## Summary

Complete the migration of the ecotiter laboratory burette titrator firmware from
Rust (legacy/rust/) to C++23 + ESP-IDF v6. The domain layer, diagnostics
subsystem, and all infrastructure drivers are already ported. Remaining work
spans the application layer, network stack, BLE, thread model, and final
hardening.

Each step produces a buildable, flashable binary verified by a 30-second serial
smoke test. No step moves forward unless the smoke test passes.

### Current State (2026-07-08, updated after watchdog hardening session)

| Layer | Status | Notes |
|-------|--------|-------|
| `domain/` (types, errors, burette SM, calibration) | ✅ Done | 5 header files, 1 .cpp |
| `diag/` (black box, FFI guard, stack monitor, etc.) | ✅ Done | 5 .cpp, 6 header-only (+ RtcWatchdog) |
| `infrastructure/drivers/stepper` | ✅ Done | RMT RAII + StepperMotor |
| `infrastructure/drivers/adc` | ✅ Done | ADC1_CH3, rolling avg, calibration |
| `infrastructure/drivers/onewire` | ✅ Done | DS18B20 bitbang, atomic temp |
| `infrastructure/drivers/limitswitch` | ✅ Fixed | GPIO changed 32(FULL)→34 (GPIO32 reserved for PSRAM bus on S3 DevKitC) |
| `infrastructure/drivers/valve` | ✅ Done | GPIO14, global atomic position |
| `infrastructure/drivers/rgb_led` | ✅ Done | WS2812 driver, setColor(r,g,b), setTransportMode() with color mapping |
| `infrastructure/storage/nvs` | ✅ Done | RAII NvsHandle, f32 bit-cast |
| `application/` | ✅ Done | 10 .cpp, 8 headers, 35 Command variants, 6 handlers |
| `interface/` | ✅ Done | SerialReader (UART), BroadcastEvent, REST API handlers |
| `infrastructure/network/` | ✅ Done | BLE advertising, connect, ping/pong verified. Zombie detection fixed. LED state tracking works |
| `infrastructure/motor_task` | ✅ Fixed | GPIO32→34 fix works. Both limit switches (HOME GPIO35, FULL GPIO34) init OK. Homing runs |
| Thread model / `main.cpp` | ✅ Fixed | Three-layer watchdog protection (IWDT 500ms + RWDT 6s + TWDT 10s). GPIO spinlock deadlock protection via init ordering + PHY wait. Boot progress diagnostics |
| Diagnostics infra | ✅ Enhanced | `RtcWatchdog` RAII class. `scripts/monitor.py` reports hang location via DBG markers. IWDT+TWDT+TWDT triple protection |
| AGENTS.md | ✅ Updated | §6.1a mandates scripts/build.sh. §3.1 pinout fixed GPIO32→34 |
| Tests (Catch2 + uart_test.py) | ✅ Partial | 12 Catch2 files (166 tests) + 5 Python UART tests |

### Remaining Work

| Step | Description | Files | Smoke Test |
|------|-------------|-------|------------|
| **2** | Application Layer | ~12 new files | ✅ Done (19 new files, 133/133 tests) |
| **3** | Interface Layer (Serial + Broadcast + REST) | ~4 new files | ✅ Done (6 new files, 159/159 tests) |
| **3.5** | UART Command Test | main.cpp rewrite + uart_test.py | ✅ Done (5/5 UART tests, BOOT OK) |
| **4** | Stepper via UART (motor task + homing + stop flags) | motor_task.cpp, burette_ops wiring | ✅ FIXED — GPIO32→34 resolved PSRAM conflict. Both limit switches init. Homing runs |
| **5** | Sensors + Broadcast (ADC temp thread, broadcast via serial) | temp_thread.cpp, broadcast wiring | ✅ Done (3 new files, 166/166 tests, BOOT OK) |
| **5a** | RGB LED (WS2812 GPIO 48) | rgb_led.hpp/.cpp, config.hpp, CMakeLists | ✅ Done — blue (advertising), green (connected), red (error), off (USB) verified |
| **6** | BLE Layer (NimBLE NUS GATT) | ble.hpp/.cpp wired into main.cpp | ✅ Done — advertising visible, connectable, ping/pong works. Zombie detection with 500ms debounce |
| **6a** | BLE diagnostic | scripts/ble_test.py | ✅ Done — `ble_test.py --cmd '{"cmd":"ping"}'` passes, `--scan-only` finds EcoTiter-XXXX |
| **7** | Network Layer (WiFi AP/STA + HTTP + WebUI) | ~6 new files + WebUI assets | Build + flash + AP visible on phone |
| **8** | Thread Model + Main Loop Integration | modify main.cpp + ~3 new files | Build + flash + all features concurrently |
| **9** | Tests & Hardening | ~6 test files + config changes | Build + flash + 60s stability test |

### Critical Blockers

| # | Issue | Status | Root Cause |
|---|-------|--------|------------|
| BL-001 | Motor task hang on boot (gpio_config) | ✅ FIXED 2026-07-08 | Two independent causes: (1) GPIO32 reserved for PSRAM bus → changed to GPIO34. (2) PHY RF calibration async from BT init holding gpio_spinlock → 1s delay + IWDT 500ms + RWDT 6s triple protection |
| BL-002 | BLE advertising not visible | ✅ FIXED 2026-07-08 | Two bugs: (1) missing ble_svc_gap_init() + ble_svc_gatt_init() calls, unchecked return values from gatts_count_cfg/add_svcs; (2) zombie detection used ble_gap_conn_active() (always returns 0) instead of ble_gap_conn_find(), and lacked 500ms debounce. Fixed: correct NimBLE init order, return value checks, ble_gap_conn_find() + debounce |

---

## Step 2 — Application Layer (Command Dispatch + State Machine + Handlers)

**Status: ✅ COMPLETED (2026-07-07)**

### Objective

Port the Rust application layer to C++23. Implement JSON command parsing,
a dispatch routing table, 6 handler modules, the application state machine,
and a tick scheduler. All code must be host-compilable (zero ESP-IDF imports
in the application core).

### Pre-Flight Checklist

1.  **Thread:** Main loop / host test
2.  **Blocking >10ms?** No — pure logic, no I/O
3.  **Stack impact:** No std::format/string in hot paths. Fixed buffers via
     `std::array<char,N>` + `std::format_to`.
4.  **Init order dep:** None — pure domain layer
5.  **FFI boundary:** N/A — no ESP-IDF headers in application/
6.  **Stop flag:** N/A
7.  **DRAM:** N/A — no hardware alloc

### Detailed Tasks

#### 2.1 Add nlohmann_json dependency

- Add `nlohmann_json` to the IDF component registry or as a FetchContent
  dependency in the top-level `CMakeLists.txt`.
- Create `components/json/` component or reuse `idf_component.yml`.

#### 2.2 Implement Command enum + envelope

- `application/include/application/command.hpp`
- `Command` enum with variants matching the legacy C++ wire protocol (fill,
  empty, dose, rinse, stop, emergencyStop, getStatus, setDirection, cal.*,
  temperature.read, adc.cal.*, valve.*, system.*, serial.ping, etc.)
- `CommandResponse` with 4 variants (Single, Error, AckThen, NoResponse)
- Serialization via `nlohmann::json` into fixed `std::array<char, MAX_RSP_SIZE>`

#### 2.3 Implement central dispatch

- `application/src/dispatch.cpp`
- Single `dispatch()` function routing each command to the correct handler
  module. Returns `std::expected<CommandResponse, AppError>`.

#### 2.4 Implement 6 handler modules

- `application/include/application/handlers/`:
  - `burette_ops.hpp/.cpp` — fill, empty, doseVolume, rinse, stop,
    emergencyStop, getStatus, moveSteps, setDirection
  - `burette_cal.hpp/.cpp` — cal.* commands (get, calcVolume, calcSpeed,
    save, reset, run, getResult)
  - `sensors.hpp/.cpp` — temperature.read, adc.cal.*, stallGuard.*
  - `system.hpp/.cpp` — getStatus, getFormattedLogs, readLog
  - `valve.hpp/.cpp` — setPosition, getState
  - `serial.hpp/.cpp` — serial.ping

#### 2.5 Implement ApplicationStateMachine

- `application/include/application/state_machine.hpp`
- `application/src/state_machine.cpp`
- Combines `BuretteState` (domain) + `TransportState` (UsbActive,
  BleDisconnected, BleConnected)
- `PendingOperation` tracking for long-running commands

#### 2.6 Implement tick scheduler

- `application/include/application/scheduler.hpp`
- `application/src/scheduler.cpp`
- Global `std::atomic<uint32_t>` tick counter
- `shouldBroadcast()` with modular arithmetic at 2s interval (BROADCAST_INTERVAL=2000)
- Named constants for all intervals (BROADCAST_INTERVAL, SAMPLE_INTERVAL, etc.)

#### 2.7 Wire application layer into build system

- Update `components/application/CMakeLists.txt` to register new sources
- Add `nlohmann_json` to REQUIRES
- No changes to `main/CMakeLists.txt` yet (application is not invoked from
  main loop until Step 6)

#### 2.8 Add host tests

- `tests/src/test_command.cpp` — Command enum serde round-trip
- `tests/src/test_dispatch.cpp` — dispatch routing integration
- `tests/src/test_handlers.cpp` — handler logic per module
- `tests/src/test_state_machine.cpp` — state machine transitions
- `tests/CMakeLists.txt` — register new test sources

### Acceptance Criteria

- ✅ `idf.py build` — 0 errors, 0 warnings
- ✅ `cmake -B build-tests tests && cmake --build build-tests && ctest --test-dir build-tests` — 133/133 pass
- ✅ 30-second serial smoke test: BOOT_OK, no Guru/WDT/panic

### Results

| Metric | Value |
|--------|-------|
| New files | 19 (10 .cpp, 7 headers, 1 CMakeLists.txt, 1 json.hpp) |
| Modified files | 6 (application CMakeLists, tests CMakeLists, domain/*.hpp, test_adc.cpp) |
| Command variants | 35 |
| Test cases | 48 new (133 total with pre-existing) |
| Build warnings | 0 (1 pre-existing deprecation from nlohmann/json) |

---

## Step 3 — Interface Layer (Serial UART + Broadcast + REST API Handlers)

**Status: ✅ COMPLETED (2026-07-07)**

### Objective

Port the interface layer: real UART init via `uart_vfs_dev_register`, a
non-blocking newline-split SerialReader, BroadcastEvent serialization for
WebSocket/BLE, and REST API handler registration for the HTTP server (routes
registered but server not yet created — that is Step 4).

### Pre-Flight Checklist

1.  **Thread:** Main loop (SerialReader::process) + HTTP server
2.  **Blocking >10ms?** No — UART reads use `VFS` non-blocking fd
3.  **Stack impact:** Fixed `std::array<char, MAX_CMD_SIZE>` line buffer. No
    heap allocation in read path.
4.  **Init order dep:** UART init before WiFi/HTTP (serial is the baseline
    transport)
5.  **FFI boundary:** UART driver install uses ESP-IDF C API. Store fd (int),
    not C pointers.
6.  **Stop flag:** N/A
7.  **DRAM:** UART buffer (256 bytes) allocated once at init.

### Detailed Tasks

#### 3.1 Implement SerialReader

- `interface/include/interface/serial.hpp`
- `interface/src/serial.cpp`
- `uart_vfs_dev_register()` + `uart_driver_install()` at init
- Non-blocking `poll()` reading into `std::array<char, MAX_CMD_SIZE>` buffer
- Newline-split output (complete lines returned to caller)
- CR ignoring, overflow reset
- `G_SERIAL_SILENT` atomic flag for output suppression

#### 3.2 Implement BroadcastEvent

- `interface/include/interface/broadcast.hpp`
- `interface/src/broadcast.cpp`
- `BroadcastEvent` struct (ts, temp, mv, vlv, brt sub-objects)
- `serializeBroadcast()` → `std::array<char, MAX_RSP_SIZE>` JSON via
  `nlohmann::json`

#### 3.3 Implement REST API handlers

- `interface/include/interface/rest_api.hpp`
- `interface/src/rest_api.cpp`
- Handler functions matching `esp_http_server` `esp_http_server.h` signature:
  - `GET /api/ping` → `{"status":"ok"}`
  - `GET /api/status` → full device state
  - `POST /api/command` → parse JSON, dispatch, return response
  - `GET /api/valve` → valve state
  - `POST /api/valve` → valve set position
- All handlers are pure functions taking `httpd_req_t*` — they copy request
  data into stack buffers before processing, never store the C pointer
  (GR-5 compliance).

#### 3.4 Update CMakeLists.txt

- `components/interface/CMakeLists.txt` — register new sources, add
  esp_http_server, nlohmann_json to REQUIRES

#### 3.5 Add host tests

- `tests/src/test_serial.cpp` — SerialReader line splitting, CR ignoring,
  overflow reset (host-compilable, no UART hardware)
- `tests/src/test_broadcast.cpp` — BroadcastEvent JSON serialization
- `tests/src/test_rest_api.cpp` — handler logic (parse JSON, validate params)

### Acceptance Criteria

- ✅ `idf.py build` — 0 errors, 0 warnings
- ✅ Host tests — 159/159 passed (448 assertions)
- ✅ 30-second serial smoke test: BOOT_OK, no Guru/WDT/panic
- ✅ `./scripts/lint.sh` — clean

### Results

| Metric | Value |
|--------|-------|
| New files | 6 (broadcast.hpp/.cpp, rest_api.hpp/.cpp, test_serial, test_broadcast, test_rest_api) |
| Modified files | 5 (domain/types.hpp, domain/burette.hpp, interface/CMakeLists, serial.hpp, serial.cpp) |
| Global hardware atoms | 11 added to domain/types.hpp (gTempCX100, gValvePosition, gBuretteState, etc.) |
| Test cases | 26 new (159 total) |

---

## Step 3.5 — UART Command Test (SerialReader → parseCommand → dispatch → respond)

**Status: ✅ COMPLETED (2026-07-07)**

### Objective

Wire `SerialReader` + `parseCommand` + `dispatch` + `serializeToBuffer` into
`app_main()` so the ESP32-S3 can receive JSON commands via UART and respond
in real time. Create a Python test script to validate the exchange.

### Pre-Flight Checklist

1.  **Thread:** Main loop (32 KB, CONFIG_ESP_MAIN_TASK_STACK_SIZE=32768)
2.  **Blocking >10ms?** No — `select()` + non-blocking `read()`
3.  **Stack impact:** `std::array<char, 256>` on stack per iteration
4.  **Init order dep:** `SerialReader::init()` before main loop
5.  **FFI boundary:** UART fd stored as `int`
6.  **Stop flag:** N/A
7.  **DRAM:** UART buffer (256 bytes) allocated once at init

### Detailed Tasks

#### 3.5.1 Update main/CMakeLists.txt

- Add `interface`, `application` to REQUIRES

#### 3.5.2 Rewrite main/main.cpp

- `app_main()` boot sequence: `nvs_flash_init()` → `BlackBox::init()` →
  `StackMonitor::registerMainTask()` → `SerialReader::init()` → `Led::init()`
- Main loop (10ms tick):
  1. `TickScheduler::tick()`
  2. `SerialReader::process()` → `parseCommand()` → `dispatch()` →
     `serializeToBuffer()` → `SerialReader::write()`
  3. Error handling: parse failures → `{"error":"parse failed"}`,
     dispatch failures → `{"error":"dispatch failed"}`
  4. `vTaskDelayUntil(&lastWake, PACING_TICK)`

#### 3.5.3 Create scripts/uart_test.py

- PySerial-based test script with 5 tests:
  1. `serial.ping` → `{"cmd":"serial.ping","result":"pong"}`
  2. `system.firmwareVersion` → `{"version":"0.1.0"}`
  3. `getStatus` → JSON with `"state"` field
  4. Invalid JSON → `{"error":"parse failed"}`
  5. Unknown command → `{"error":"parse failed"}`
- Auto-detect port via `find_port.py` or `-p` flag
- Raw log saved to `logs/uart_test_<timestamp>.log`

#### 3.5.4 Update scripts/build.sh

- Add `uart` command: runs `scripts/uart_test.py -p <port>`

### Acceptance Criteria

- ✅ `idf.py build` — 0 errors, 0 warnings
- ✅ `scripts/uart_test.py` — 5/5 tests pass
- ✅ 30-second serial smoke test: BOOT_OK, no Guru/WDT/panic
- ✅ Raw UART log saved to `logs/uart_test_<timestamp>.log`

---

## Step 4 — Stepper via UART (Motor Task + Homing + Stop Flags)

**Status: ✅ COMPLETED (2026-07-07)**

### Objective

Wire the stepper motor into the UART command pipeline. Create a dedicated
FreeRTOS motor task (16 KB stack, GR-6), implement `moveSteps`, `stop`,
`emergencyStop` with mandatory stop flags (GR-2), homing sequence at boot,
and limit switch integration.

### Pre-Flight Checklist

1.  **Thread:** Motor task (16 KB, GR-6) — created at boot; main loop sends
    commands via FreeRTOS queue
2.  **Blocking >10ms?** Yes — `rmt_tx_wait_all_done()` blocks for the
    duration of each RMT chunk. OK because it's in the dedicated motor task,
    NOT main loop (GR-1).
3.  **Stack impact:** Motor 16 KB. No `std::format` in hot path. No stack-local
    arrays > 256 bytes.
4.  **Init order dep:** Motor is independent — init after NVS, before network.
5.  **FFI boundary:** RAII `RmtChannel` wrapper handles `rmt_channel_handle_t`.
    No raw pointers cross task boundaries.
6.  **Stop flag:** **REQUIRED (GR-2).** Every `moveStepsIntervals()` call gets
    `&gStopFull` or `&gStopHome` as the stop flag.
7.  **DRAM:** Motor task stack (16 KB) allocated at boot. RMT channel + encoder
    allocated once in constructor.

### Detailed Tasks

#### 4.1 Create motor task

- `infrastructure/src/motor_task.cpp`
- `infrastructure/include/infrastructure/motor_task.hpp`
- `MotorCommand` struct — command type (MoveSteps, Stop, EmergencyStop, Home,
  SetDirection, SetSpeed, SetAccel) + optional parameters
- `motorTaskEntry(void*)` — FreeRTOS task function, 16 KB stack
- Creates `StepperMotor` + `LimitSwitch` instances at start
- Homing sequence: move CW until HOME limit switch triggers, set position=0
- Command loop: `xQueueReceive()` with 100ms timeout
  - Execute motor commands via `StepperMotor::moveStepsIntervals()` with
    `&gStopFull` / `&gStopHome` stop flag
  - Check `gStopFull` between RMT chunks (already in `moveStepsIntervals`)
  - On `LimitSwitchTriggered`: log, set `gBuretteState=Error`, reset flag
- Direction changes: `gpio_set_level(dirPin, ...)` before move
- Speed/accel: stored as config, used to compute RMT interval arrays

#### 4.2 Define MotorCommand queue

- FreeRTOS `QueueHandle_t` with `xQueueCreate(4, sizeof(MotorCommand))`
- Extern global `gMotorCmdQueue` declared in `motor_task.hpp`
- RAII queue creation in motor task init

#### 4.3 Wire motor commands in burette_ops handlers

- Modify `burette_ops.cpp` handlers to send commands via `gMotorCmdQueue`
  instead of returning stubs:
  - `handleMoveSteps(steps)` → send `MotorCommand::MoveSteps` to queue
  - `handleStop()` → send `MotorCommand::Stop`, set `gBuretteState=Stopping`
  - `handleEmergencyStop()` → set `gStopFull=true`, send `MotorCommand::EmergencyStop`
  - `handleSetDirection(dir)` → send `MotorCommand::SetDirection`
  - `handleSetSpeed(speed)` → store in `gSpeed`
  - `handleSetAccel(accel)` → store in `gAccel`
- Return `AckThen` for all queued commands (acknowledge before execution)

#### 4.4 Wire homing + limit switch globals

- `domain/types.hpp` already has `gStopFull`, `gStopHome`, `gBuretteState`
- Connect limit switch ISRs (GPIO 34 FULL, GPIO 35 HOME) to these atoms
  (ISR already exists in `limitswitch.cpp` — verify it sets the correct atoms)

#### 4.5 Update CMakeLists.txt

- `components/infrastructure/CMakeLists.txt` — add `src/motor_task.cpp`
- No changes to `main/CMakeLists.txt` (main loop just sends to queue)

#### 4.6 Update scripts/uart_test.py

- Add stepper test cases:
  - `moveSteps` with 200 steps → see motor move
  - `stop` → motor stops mid-move  
  - `emergencyStop` → immediate stop
  - `setDirection` + `moveSteps` → motor moves opposite direction

#### 4.7 Add host tests (optional, if motor_task logic is testable)

- If `MotorCommand` queue send/receive can be isolated, add tests

### Acceptance Criteria

- `idf.py build` — 0 errors, 0 warnings
- Flash + UART: `{"cmd":"moveSteps","steps":200}` → motor rotates 200 steps
- `{"cmd":"stop"}` → motor stops before completing
- `{"cmd":"emergencyStop"}` → immediate stop via `gStopFull=true`
- HOME limit switch triggered → homing completes
- 30-second smoke test: BOOT OK, no Guru/WDT/panic, `uxTaskGetStackHighWaterMark` > 20%

### Results

| Metric | Value |
|--------|-------|
| New files | 4 (motor_task.hpp/.cpp, ffi_guard.cpp, state_tracer.cpp) |
| Modified files | 6 (burette_ops.cpp, stepper.hpp, main.cpp, crash_handler.cpp, stack_monitor.cpp, tests/CMakeLists.txt) |
| Motor task stack | 16384 bytes (16 KB, GR-6) |
| Stop flags | GR-2: `&gStopFull` / `&gStopHome` on all `moveStepsIntervals()` calls |
| Host tests | 159/159 pass (442 assertions) |
| Lint | Clean (0 warnings, 0 errors) |
| Smoke test | BOOT OK, no Guru/WDT/panic |
| Crash reports | `docs/crash_reports/2026-07-07_step4_boot_crash.md` |
| Lessons learned | LL-023 (UNICORE core ID), LL-024 (xTaskGetHandle nullptr), LL-025 (.iram1 flash calls) |

### Notable Issues

1. **Boot crash masked by broken panic handler**: `xTaskCreatePinnedToCore(..., 1)`
   with `CONFIG_FREERTOS_UNICORE=y` caused assert. The assert was invisible because
   the panic handler itself crashed twice (xTaskGetHandle(nullptr) in LL-024,
   .iram1 flash call in LL-025), producing only "Panic handler entered multiple
   times". Fixed by using `xTaskCreate()` and fixing the panic handler chain.

2. **Host test build broken**: `burette_ops.cpp` now includes
   `infrastructure/motor_task.hpp` which pulls in `freertos/FreeRTOS.h` and
   `freertos/queue.h`. Fixed by adding FreeRTOS stubs in `tests/stubs/` and
   infrastructure include path to `tests/CMakeLists.txt`.

---

## Step 5 — Sensors + Broadcast (ADC, Temperature, Periodic Broadcast)

**Status: ✅ COMPLETED (2026-07-08)**

### Objective

Wire real sensor readings (ADC pH electrode, DS18B20 temperature) into the
main loop. Create a dedicated temperature thread (16 KB stack, GR-6) for
blocking OneWire reads. Publish periodic BroadcastEvent JSON via Serial
every 2s using `TickScheduler::shouldBroadcast()`.

### Pre-Flight Checklist

1.  **Thread:** Temperature thread (16 KB) — blocking reads OK; main loop
    for ADC + broadcast
2.  **Blocking >10ms?** Temperature thread: `vTaskDelay(1000ms)` + blocking
    OneWire bitbang (OK — dedicated thread). Main loop: no blocking.
3.  **Stack impact:** Temp thread 16 KB. ADC read uses stack-local
    `std::array<uint16_t, 64>`. Broadcast uses `std::snprintf` into fixed
    512-byte buffer.
4.  **Init order dep:** None — sensors independent of network/BLE.
5.  **FFI boundary:** ADC uses `adc_oneshot_read()` (ESP-IDF C API wrapped
    in RAII). No C pointers stored.
6.  **Stop flag:** N/A
7.  **DRAM:** ADC calibration struct (~32 bytes). Temperature atomic int.

### Detailed Tasks

#### 5.1 Create temperature thread

- `infrastructure/src/temp_thread.cpp`
- `infrastructure/include/infrastructure/temp_thread.hpp`
- `tempThreadEntry(void*)` — FreeRTOS task, 16 KB stack
- Creates `OneWireBus` on GPIO 33 (DS18B20)
- Every 1 second: call `readSensor()`, store result in `gTempCX100`
- On read failure: store sentinel `-99999`, log warning
- Blocking: `vTaskDelay(pdMS_TO_TICKS(1000))`
- Diagnostics: `StackMonitor::registerThread("temp")`, `FfiGuard(40)`

#### 5.2 Wire ADC periodic sampling

- In main loop, every `scheduler.shouldSample()` (100ms):
  - Call `adc_oneshot_read()` on ADC1_CH3 (GPIO 4)
  - Apply rolling average (existing in `adc.cpp`)
  - Convert to mV via calibration, store in `gLastMv`

#### 5.3 Wire BroadcastEvent to Serial

- In main loop, every `scheduler.shouldBroadcast()` (2s):
  - Build `BroadcastEvent` from current atomic state
  - Call `serializeBroadcast()` into `ResponseBuffer`
  - Write to serial via `SerialReader::write()`

#### 5.4 Add host tests

- `tests/src/test_broadcast.cpp` — expanded with domain-atom JSON serialization test
- `tests/src/test_scheduler.cpp` — 6 tests: tick increment, 30-tick broadcast interval,
  10-tick sample interval, 100-tick watermark interval, 6000-tick maintenance interval,
  32-bit unsigned tick wrapping

#### 5.5 Update CMakeLists.txt

- `components/infrastructure/CMakeLists.txt` — add `src/temp_thread.cpp`

### Acceptance Criteria

- ✅ `idf.py build` — 0 errors, 0 warnings
- ✅ Flash + UART: every ~2s a JSON broadcast line appears on serial
- ✅ `{"cmd":"temperature.read"}` returns current temperature
- ✅ `{"cmd":"adc.cal.get"}` returns ADC calibration
- ✅ 30-second smoke test: BOOT OK, no Guru/WDT/panic

### Results

| Metric | Value |
|--------|-------|
| New files | 3 (`temp_thread.hpp`, `temp_thread.cpp`, `test_scheduler.cpp`) |
| Modified files | 5 (`main/main.cpp`, `infrastructure/CMakeLists.txt`, `tests/CMakeLists.txt`, `test_broadcast.cpp`, `test_scheduler.cpp`) |
| Temp thread stack | 16384 bytes (16 KB, GR-6) |
| FfiGuard boundaries | 40 (OneWire in temp thread), 50 (ADC read in main loop) |
| Host tests | 166/166 pass (6632 assertions) |
| Lint | Clean (0 warnings, 0 errors) |
| Smoke test | BOOT OK, no Guru/WDT/panic |
| ADC clip | Negative mV values clamped to 0 for `uint16_t gLastMv` |
| Broadcast rate | Every 2s via `TickScheduler::shouldBroadcast()` (changed from 300ms) |
| ADC sample rate | Every 100ms via `TickScheduler::shouldSample()` |
| Temp read rate | Every 1s via `vTaskDelay(1000ms)` in temp thread |

---

## Step 5a — RGB LED Driver (WS2812 GPIO 48)

**Status: ✅ COMPLETED (2026-07-08)**

### Objective

Replace the old single-color `Led` (GPIO 2, unused on standard ESP32-S3-DevKitC-1)
with a WS2812/SK68XX RGB LED driver on GPIO 48. Implement color state mapping:
- BLE advertising → solid blue
- BLE connected → solid green
- BLE error → solid red (via `gBleError` atomic)
- USB handshake received → off (via `gUsbHandshakeReceived` atomic)

### Pre-Flight Checklist

1.  **Thread:** Main loop — `RgbLed::setTransportMode()` calls `rmt_transmit()` +
    `rmt_tx_wait_all_done(10ms)` which blocks < 10ms → passes GR-1
2.  **Blocking >10ms?** LED transmit = ~29 us for 1 WS2812 pixel → well under 10ms
3.  **Stack impact:** 25 RMT symbols (24 data + 1 reset) on stack, ~100 bytes
4.  **Init order dep:** Created after PHY wait (step 7) but before motor task (step 8)
5.  **FFI boundary:** RMT C API wrapped in constructor/destructor. No stored pointers.
6.  **Stop flag:** N/A
7.  **DRAM:** RMT channel + encoder (~few hundred bytes). Shared RMT memory: 64 symbols.

### Detailed Tasks

#### 5a.1 Create RgbLed class

- `infrastructure/include/infrastructure/drivers/rgb_led.hpp`
- `infrastructure/src/drivers/rgb_led.cpp`
- RMT TX channel at 10 MHz resolution for precise WS2812 bit timing
- Bit encoding: T0H=0.3us, T0L=0.9us, T1H=0.7us, T1L=0.5us
- GRB byte order per WS2812 protocol
- Reset pulse: 60 us low after data

#### 5a.2 Update config

- `config.hpp`: `PIN_LED_RGB = GPIO_NUM_48`, `LED_RMT_RES_HZ = 10'000'000`

#### 5a.3 Wire into main loop

- `RgbLed` replaces old `Led` in `main/main.cpp`
- LED state derived from `gUsbHandshakeReceived`, `gBleError`, `BleManager::isConnected()`
- State compared on each 10ms tick; RMT refresh only on change

#### 5a.4 Add domain atoms

- `domain/types.hpp`: `gUsbHandshakeReceived` (bool), `gBleError` (bool)

### Files Created

| File | Purpose |
|------|---------|
| `components/infrastructure/include/infrastructure/drivers/rgb_led.hpp` | RgbLed class (setColor, setTransportMode, refresh) |
| `components/infrastructure/src/drivers/rgb_led.cpp` | WS2812 RMT implementation |

### Files Modified

| File | Change |
|------|--------|
| `components/infrastructure/include/infrastructure/config.hpp` | Added `PIN_LED_RGB=GPIO_NUM_48`, `LED_RMT_RES_HZ=10MHz` |
| `components/domain/include/domain/types.hpp` | Added `gUsbHandshakeReceived`, `gBleError` |
| `components/infrastructure/CMakeLists.txt` | Replaced `led.cpp` with `rgb_led.cpp` |
| `main/main.cpp` | RgbLed creation + state-driven color updates |

### Results

| Metric | Value |
|--------|-------|
| Blue (advertising) | ✅ Verified via `ble_test.py --scan-only`: device visible, LED blue |
| Green (connected) | ✅ Verified: `ble_test.py --cmd '{"cmd":"ping"}'` → LED turns green, connection stable |
| Red (error) | ✅ Verified: zombie detection no longer leaves red state; disconnect handler clears gBleError |
| Off (USB active) | ✅ Via `gUsbHandshakeReceived` atomic |
| RMT coexistence | ✅ Both stepper (GPIO21) and LED (GPIO48) RMT channels work simultaneously |

### Acceptance Criteria

- ✅ `idf.py build` — 0 errors, 0 warnings
- ✅ BOOT OK with motor task + RGB LED simultaneously
- ✅ Blue LED during BLE advertising, green on connect, red on error, off on USB handshake

---

## Step 6 — BLE Layer (NimBLE NUS GATT)

**Status: ✅ COMPLETED (2026-07-08)**

### Objective

Port the BLE/NimBLE subsystem. Implement a `BleManager` with NUS (Nordic UART
Service) GATT for JSON command transport, a bounded command queue, a dedicated
notify thread (8 KB stack, GR-6), and 3-level zombie defense.

### Pre-Flight Checklist

1.  **Thread:** BLE notify (8 KB) + NimBLE host (12 KB, internal FreeRTOS
    task) + main loop for command drain
2.  **Blocking >10ms?** No — `xQueueReceive` with 10ms timeout in notify
    thread; `try_recv()` in main loop
3.  **Stack impact:** No `std::format`/`std::print` in notify thread (8 KB
    budget). Use `std::array<char,N>` + `std::format_to`.
4.  **Init order dep:** GR-3 — WiFi → HTTP → BLE. BLE init LAST. Heap
    pre-check: largest free block >= 30 KB before attempting.
5.  **FFI boundary:** NimBLE C API calls wrapped in `FfiGuard` (GR-7).
    No raw NimBLE pointers stored across boundaries.
6.  **Stop flag:** N/A
7.  **DRAM:** Critical — BLE NimBLE host needs ~12 KB contiguous
    `MALLOC_CAP_INTERNAL`. Must check `largestFreeBlock()` before init.

### Detailed Tasks

#### 6.1 Implement BleManager

- `infrastructure/network/include/infrastructure/network/ble.hpp`
- `infrastructure/network/src/ble.cpp`
- NUS GATT service with RX (write) and TX (notify) characteristics
- Bounded command queue: `xQueueCreate(8, sizeof(BleCmdItem))`
- Pre-init guard: `bool initialized_` — `process()` / `isConnected()`
  return immediately if false
- 3-level zombie defense:
  - L1: 5 consecutive notify failures → force disconnect
  - L2: `ble_gap_conn_active() == 0` but `connected_ == true` → cleanup
  - L3: immediate kill on notify with 0 connections but flag set

#### 6.2 Implement BLE notify thread

- Dedicated `std::thread` with 8 KB stack (`BLE_NOTIFY_STACK`)
- Receives `BleNotifyItem` from queue, calls `ble_gatts_notify_custom()`
- No `std::format`/`nlohmann::json::dump()` in hot path — use
  `std::format_to` into fixed buffer

#### 6.3 Add heap pre-check to init sequence

- Before `nimble_port_init()`, call `heap_caps_get_largest_free_block()`
  with `MALLOC_CAP_INTERNAL`
- If < 30 KB, log warning and skip BLE init (device runs in WiFi-only mode)
- Guards against NimBLE's uncatchable `assert()` crash (LL-007)

#### 6.4 Update CMakeLists.txt and sdkconfig

- Add `bt` component to `infrastructure/CMakeLists.txt` REQUIRES
- `sdkconfig.defaults`:
  - `CONFIG_BT_ENABLED=y`
  - `CONFIG_BT_NIMBLE_ENABLED=y`
  - `CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=12288`
  - `CONFIG_BT_NIMBLE_ACL_BUF_COUNT=6`
  - `CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT=12`
  - `CONFIG_BT_NIMBLE_MAX_CCCDS=2`

### Issues Encountered (Resolved)

1. ✅ **Advertising not visible** — fixed: added ble_svc_gap_init() + ble_svc_gatt_init(),
   checked return values of gatts_count_cfg/add_svcs
2. ✅ **Zombie detection race** — fixed: replaced ble_gap_conn_active() with
   ble_gap_conn_find() + 500ms debounce; clear gBleError on disconnect
3. ✅ **Green LED not lighting** — fixed: zombie detection no longer clears connected_
   immediately after connect
4. ✅ **sdkconfig.defaults misspelling** — fixed: MSYS1 → MSYS_1

### Results

| Metric | Value |
|--------|-------|
| BLE init fix | Added ble_svc_gap_init(), ble_svc_gatt_init(), return value checks |
| Zombie debounce | 500ms + ble_gap_conn_find() instead of conn_active() |
| Scan response | NUS service UUID in scan response data |
| LED integration | RgbLed wired: blue (adv), green (connected), red (error), off (USB) |
| Build | 0 errors, 0 warnings |
| ble_test.py --scan-only | ✅ EcoTiter-FCA2 found at RSSI -46 |
| ble_test.py --cmd ping | ✅ Connected, ping → pong |
| Phone BLE | ✅ Device visible, connectable, LED green |
| Stability | No zombie detections, no crashes during testing |

---

## Step 7 — Network Layer (WiFi AP/STA + HTTP Server + WebUI)

**Status: ⬜ Pending**

### Objective

Port the network subsystem: `WifiManager` with AP fallback + STA reconnect +
UDP DNS responder for captive portal, `EspHttpServer` with 25+ routes
(captive portal, REST API, WebSocket, WebUI), and the embedded WebUI
dashboard (HTML/CSS/JS). HTTP server stack must be 12 KB (GR-6).

### Pre-Flight Checklist

1.  **Thread:** net_owner (16 KB stack) for WiFi/HTTP lifecycle; main loop
    for DNS polling
2.  **Blocking >10ms?** WiFi init blocks 3-5s (expected in net_owner thread,
    not main loop). DNS `recvfrom()` is non-blocking in main loop.
3.  **Stack impact:** HTTP handler stack = 12 KB (GR-6 mandatory). JSON
    serialization in handlers uses stack-allocated buffers.
4.  **Init order dep:** GR-3 — WiFi init first, then HTTP server
5.  **FFI boundary:** `httpd_req_t*` NEVER stored across handler return.
    WebSocket via `httpd_ws_send_frame_async()`.
6.  **Stop flag:** N/A
7.  **DRAM:** Critical — WiFi + HTTP + BLE triangle (GR-3). HTTP server
    needs 12 KB contiguous `MALLOC_CAP_INTERNAL`. Monitor heap after HTTP
    init before attempting BLE.

### Detailed Tasks

#### 7.1 Implement WifiManager

- `infrastructure/network/include/infrastructure/network/wifi.hpp`
- `infrastructure/network/src/wifi.cpp`
- AP mode: `esp_netif_create_default_wifi_ap()` on 192.168.4.1/24
- STA mode: NVS-backed credential persistence, auto-reconnect at 10s
- UDP DNS responder on port 53 (AP_IP:53) for captive portal detection
- mDNS: `mdns_init()` only after IP obtained
- Non-blocking `process()` for main-loop DNS polling
- `startAP()`, `startSTA(ssid, pass)`, `stop()`, `isConnected()`,
  `getIP()`

#### 7.2 Implement HttpServer

- `infrastructure/network/include/infrastructure/network/http_server.hpp`
- `infrastructure/network/src/http_server.cpp`
- `EspHttpServer` with `stack_size = 12288` (GR-6)
- Route groups:
  - Captive portal (8 routes): `/wifi`, `/wifi/connect`, `/wifi/status`,
    5 probe redirects (generate_204, hotspot-detect.html, etc.) → 302 `/wifi`
  - REST API (7 routes): ping, status, command, valve state, valve set,
    logs, logs/download
  - WebSocket (1 route): `/ws/stream` with `WS_SESSIONS` BTreeMap broadcast
  - WebUI (9 routes): index.html, style.css, 7 JS modules
- `broadcastWebsocketEvent()`: iterate sessions, send JSON via
  `httpd_ws_send_frame_async()`, remove stale sessions via `is_closed()`

#### 7.3 Implement DNS responder

- Pure function in `domain/dns.hpp` (already planned in domain layer):
  `buildDnsResponse()` — constructs UDP DNS response packet
- 4 host-compilable tests for DNS packet structure

#### 7.4 Port WebUI assets

- Copy from `legacy/rust/src/webui/` to `main/webui/` or embed via
  `include..` in a `webui.hpp` component
- HTML dashboard (Bootstrap 5.3), 7 JS modules (state, ws, ui-update,
  logs, stepper, calibration, init), CSS
- Captive portal HTML page (`captive.html`)

#### 7.5 Update CMakeLists.txt

- `components/infrastructure/CMakeLists.txt` — add network/ sources,
  `esp_wifi`, `esp_http_server`, `mdns`, `lwip` to REQUIRES
- `components/interface/CMakeLists.txt` — add `webui.hpp` if separate
  component

#### 7.6 Update sdkconfig.defaults

- `CONFIG_LWIP_MAX_SOCKETS=5` (reduce from 8 to save DRAM per LL-016)
- `CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=4`
- `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=4`
- `CONFIG_HTTPD_LOG_LEVEL=1` (suppress noisy HTTPD logs)
- `CONFIG_MDNS_MAX_SERVICES=1`

### Acceptance Criteria

- `idf.py build` — 0 errors, 0 warnings
- Flash + monitor: boot completes, "EcoTiter-AP" visible on phone WiFi scan
- Phone connects to AP, captive portal triggers
- `curl http://192.168.4.1/api/ping` returns `{"status":"ok"}`
- WebUI loads in browser at `http://192.168.4.1/`
- WebSocket connects at `ws://192.168.4.1/ws/stream`
- 30-second stability test: no Guru Meditation, no WDT, heap stable

---

## Step 8 — Thread Model + Main Loop Integration

**Status: ⬜ Pending**

### Objective

Wire all components together. Create the thread model: motor task (16 KB
stack), temperature thread (16 KB), BLE notify (8 KB), net_owner (16 KB).
Implement the full `app_main()` with GR-3 init order, GR-1 non-blocking main
loop, transport state machine, command dispatch from serial + BLE + HTTP, and
WebSocket broadcast.

### Pre-Flight Checklist

1.  **Thread:** Main (32 KB), motor (16 KB), temp (16 KB), BLE notify (8 KB),
    net_owner (16 KB) — all GR-6 compliant
2.  **Blocking >10ms?** Main loop: NONE (GR-1). Motor thread: `rmt_tx_wait_all_done`
    blocks (OK — dedicated thread). Temp thread: `vTaskDelay(800ms)` (OK —
    dedicated thread). net_owner: `wait_for_ip()` blocks 5s (OK — dedicated
    thread).
3.  **Stack impact:** Main loop 32 KB (safe for ESP_LOGI, JSON lightweight).
    Motor 16 KB (no std::format in hot path). BLE notify 8 KB (no heap alloc).
4.  **Init order dep:** GR-3 — WiFi → HTTP → BLE in net_owner thread
5.  **FFI boundary:** `httpd_req_t*` never stored across handler return (GR-5).
    Motor thread uses RAII RmtChannel (no raw handles across threads).
6.  **Stop flag:** GR-2 — every `moveStepsIntervals()` call gets a stop flag
7.  **DRAM:** Allocate net_owner, motor, temp thread stacks at boot. Measure
    largest free block after each allocation.

### Detailed Tasks

#### 8.1 Create motor task

- `infrastructure/src/motor_task.cpp`
- `motorTaskEntry()` — FreeRTOS task with 16 KB stack
- Homing sequence at start (sets `HOMING_DONE` flag)
- Command loop: receives `MotorCommand` via queue, executes
  `stepper.moveStepsIntervals()` with stop flag, sends result back
- Stop flag polling: checks `gStopFull` / `gStopHome` between RMT chunks

#### 8.2 Create temperature thread

- `infrastructure/src/temp_thread.cpp`
- `tempThreadEntry()` — `std::thread` with 16 KB stack
- Every 1 second: call `readSensor()` on `OneWireBus`, store result in
  `gTempCX100`
- Blocking: `vTaskDelay(pdMS_TO_TICKS(1000))`

#### 8.3 Create net_owner thread

- `infrastructure/src/net_owner.cpp`
- 16 KB stack, created at boot
- GR-3 init order:
  1.  `WifiManager::initAP()` / `WifiManager::startSTAfromNVS()`
  2.  `WifiManager::waitForIP(5s)`
  3.  `HttpServer::create()` with `stack_size = 12288`
  4.  `BleManager::init()` (if heap >= 30 KB)
- Posts initialized handles to main loop via queue

#### 8.4 Implement full app_main()

- `main/main.cpp` — rewrite from minimal loop to full application
- Boot sequence:
  1.  `nvs_flash_init()`
  2.  `BlackBox::instance().init()`
  3.  `StackMonitor::registerMainTask()`
  4.  `RtcWatchdog` init
  5.  `esp_log_level_set("*", ESP_LOG_WARN)`
  6.  Create net_owner thread (posts handles back)
  7.  Create motor task
  8.  Create temp thread
- Main loop (pacing tick = 10ms):
  1.  `TickWatchdog` RAII
  2.  `RtcWatchdog::feed()` — every iteration
  3.  `StackMonitor::checkWatermarks()` every 100 ticks
  4.  `WifiManager::process()` — DNS polling
  5.  `HttpServer::broadcastWebsocketEvent()` — status broadcast at 2s
  6.  `BleManager::process()` — zombie defense, command drain
  7.  `SerialReader::process()` — line read, parse, dispatch
  8.  `Led::process()` — blink state machine
  9.  Transport SM — USB alive check, mode transitions
  10. `vTaskDelayUntil(&lastWake, PACING_TICK)`

#### 8.5 Add cross-thread communication

- FreeRTOS queues (wrapped in RAII `Queue<T>` template):
  - `motor_cmd_queue` — MotorCommand from dispatch → motor task
  - `ble_cmd_queue` — BLECommand from BLE callback → main loop
  - `ble_notify_queue` — StatusUpdate from main loop → BLE notify thread
  - `init_result_queue` — net_owner → main loop (handles + status)

#### 8.6 Wire command dispatch

- `main.cpp` main loop calls `dispatch()` for each input source:
  - Serial lines from `SerialReader::process()`
  - BLE commands from `ble_cmd_queue`
  - HTTP POST /api/command from REST API handler
- Responses routed back to the originating transport

### Acceptance Criteria

- `idf.py build` — 0 errors, 0 warnings
- Flash + monitor: boot completes, all 5+ threads spawn
- Serial output shows motor homing, temperature reads, WiFi init, HTTP start
- `curl http://192.168.4.1/api/status` returns full device state
- Phone connects to WebSocket at `ws://192.168.4.1/ws/stream`, receives live
  status updates
- BLE advertising visible, connectable
- 60-second stability test: no Guru Meditation, no WDT, no heap exhaustion
- `uxTaskGetStackHighWaterMark()` > 20% for all threads

---

## Step 9 — Tests & Hardening

**Status: ⬜ Pending**

### Objective

Final hardening pass: comprehensive unit tests, clang-tidy 0 warnings,
sdkconfig.defaults audit, clang-format compliance, and a final 60-second
smoke test on hardware.

### Pre-Flight Checklist

1.  **Thread:** All — watermarks verified in Step 6
2.  **Blocking >10ms?** Already ensured by GR-1 compliance
3.  **Stack impact:** Verified via `uxTaskGetStackHighWaterMark()` logging
4.  **Init order dep:** Verified in Step 6
5.  **FFI boundary:** All C pointers handled per GR-5
6.  **Stop flag:** All RMT calls have stop flag per GR-2
7.  **DRAM:** Monitor `heap_caps_get_largest_free_block()` at each init stage

### Detailed Tasks

#### 9.1 Write remaining unit tests

- `tests/src/test_adc.cpp` — 6 tests ✅ (existing)
- `tests/src/test_valve.cpp` — 2 tests ✅ (existing)
- `tests/src/test_nvs.cpp` — f32 bit-cast round-trip, string encoding (host,
  no NVS hardware)
- `tests/src/test_dns.cpp` — DNS response packet structure, 4 test cases
- `tests/src/test_scheduler.cpp` — tick wrapping, broadcast interval
- `tests/src/test_broadcast.cpp` — event JSON serialization edge cases
- `tests/src/test_calibration.cpp` — `mlToSteps()`, `stepsToMl()`,
  `computeRamp()` ramp generation

#### 9.2 Audit sdkconfig.defaults

- Ensure all of the following are set:

```
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESP_MAIN_TASK_STACK_SIZE=32768
CONFIG_BROWNOUT_DET=n
CONFIG_ESP_INT_WDT=y
CONFIG_ESP_INT_WDT_TIMEOUT_MS=500
CONFIG_ESP_TASK_WDT_INIT=y
CONFIG_ESP_TASK_WDT_PANIC=y
CONFIG_ESP_TASK_WDT_TIMEOUT_S=10
CONFIG_BOOTLOADER_WDT_DISABLE_IN_USER_CODE=y
CONFIG_FREERTOS_HZ=1000
CONFIG_FREERTOS_UNICORE=y
CONFIG_LOG_DEFAULT_LEVEL_WARN=y
CONFIG_BOOTLOADER_LOG_LEVEL_ERROR=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESP_CONSOLE_UART_DEFAULT=y
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=12288
CONFIG_BT_NIMBLE_ACL_BUF_COUNT=6
CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT=12
CONFIG_BT_NIMBLE_MAX_CCCDS=2
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
CONFIG_BT_NIMBLE_MAX_BONDS=1
CONFIG_LWIP_MAX_SOCKETS=5
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=4
CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=4
CONFIG_HTTPD_LOG_LEVEL=1
CONFIG_MDNS_MAX_SERVICES=1
CONFIG_PTHREAD_TASK_STACK_SIZE_DEFAULT=8192
```

#### 9.3 Create partitions.csv (if needed)

- Verify the default partition layout works for the firmware image
- Create custom `partitions.csv` only if OTA or specific NVS sizing is
  required:
  ```
  nvs,      data, nvs,     0x9000,  0x6000,
  phy_init, data, phy,     0xf000,  0x1000,
  factory,  app,  factory, 0x10000, 1M,
  ```

#### 9.4 Run linter

- `clang-tidy -p build/ components/main components/**/*.cpp` — 0 warnings
- `clang-format -i -n components/main components/**/*.cpp` — 0 differences
- `python docs/validate_okf.py` — all docs pass

#### 9.5 Final commit checklist (from AGENTS.md §10)

- [ ] `idf.py build` — 0 errors, 0 warnings
- [ ] `clang-tidy` — 0 warnings
- [ ] `cd tests && ctest --output-on-failure` — all pass
- [ ] No `std::abort()` / `std::terminate()` / `assert()` in production
- [ ] No `std::string` / `std::vector` allocation in hot paths
- [ ] Every `// NOLINT` has an English `// CONTRACT:` comment
- [ ] No ESP-IDF headers in `domain/` layer
- [ ] Main loop has NO blocking operations (GR-1)
- [ ] Every RMT motion has a stop flag (GR-2)
- [ ] Init order follows GR-3 triangle (WiFi → HTTP → BLE)
- [ ] Pre-Flight Checklist was filled out before each code generation step

### Acceptance Criteria

- `idf.py build` — 0 errors, 0 warnings
- `clang-tidy` — 0 warnings
- All Catch2 host tests pass
- `clang-format --check` — 0 differences
- `docs/validate_okf.py` — passes
- 60-second hardware smoke test: no Guru Meditation, no WDT, no panic,
  all features (WiFi AP, HTTP, WebSocket, BLE, stepper, temp, valve, LED)
  operational concurrently

---

## Verification

### Smoke Test Procedure (same for every step)

```bash
# From project root:
python scripts/pipeline.py
```

This runs: `idf.py build` → auto-detect port → `idf.py -p PORT flash` →
`timeout 30 python3 scripts/monitor.py`.

Pass criteria:
- Build: 0 errors, 0 warnings
- Flash: "Flashing has completed!" message
- Monitor: no `=== CRASH ===` or Guru Meditation in 30 seconds
- Exit code 0 (BOOT OK) or 0 (No boot marker) — both acceptable before
  Step 6 adds boot output

### Final Acceptance (Step 7)

- 60-second stability test on hardware
- All 7 steps produce green checkmarks on the commit checklist (AGENTS.md §10)
- Firmware binary: `build/ecotiter.bin`
- Version: `PROJECT_VER` from `CMakeLists.txt`

## Lessons Learned (2026-07-08)

| ID | Finding |
|----|---------|
| LL-026 | BLE not visible: missing ble_svc_gap_init() + ble_svc_gatt_init(), ignored return values of gatts_count_cfg/add_svcs. Fix: correct NimBLE init order. |
| LL-027 | ESP32-S3 has exactly 2 RMT TX channels (`SOC_RMT_TX_CANDIDATES_PER_GROUP=2`). Stepper uses 1, WS2812 LED uses 1 — both channels consumed. No third channel for fallback. |
| LL-028 | `led_strip` component is NOT bundled in ESP-IDF v6.0.1 — must implement WS2812 manually via RMT copy encoder or add via IDF component registry. |
| LL-029 | BLE advertising as "EcoTiter-XXXX" not visible — ✅ FIXED. Root cause: missing gap/gatt init + ble_gap_conn_active() returning 0 always. |
| LL-030 | **GPIO32 on ESP32-S3-DevKitC-1 is reserved for PSRAM/Flash bus.** `gpio_config(GPIO32)` hangs because the IOMUX for pins 26-32 is owned by the internal PSRAM/flash controller. Full endstop must use a different pin (GPIO34, input-only). |
| LL-031 | **GPIO spinlock deadlock with PHY calibration.** Enabling BT (`CONFIG_BT_ENABLED=y`) triggers asynchronous PHY RF calibration. PHY calibration runs in a background context and holds `gpio_spinlock` for extended periods (10-200ms). Any `gpio_config()`, `gpio_set_direction()`, or `gpio_set_level()` call during this period deadlocks. **Fix**: ensure all GPIO configuration completes before BT init, or add `vTaskDelay(>=500ms)` between BT init and GPIO ops. |
| LL-032 | **TWDT cannot catch spinlock deadlocks.** The ESP-IDF Task Watchdog relies on interrupt delivery to check task timestamps. When a task enters a critical section (spinlock with interrupts disabled), the TWDT interrupt cannot fire. Spinlock deadlocks appear as silent hangs that only timeout via serial monitor. Use manual diagnostic markers (`puts("DBG: ...")`) to bisect. |
| LL-033 | **`scripts/build.sh` must be used instead of ad-hoc `idf.py`.** Multiple agent sessions wasted time debugging ESP-IDF environment sourcing when `scripts/build.sh` already handles it. Recorded as AGENTS.md §6.1a. |
| LL-034 | **Triple watchdog stack (IWDT + RWDT + TWDT) eliminates silent hangs.** IWDT (500ms, MWDT1 in Timer Group 1) catches spinlock deadlocks via interrupt-disabled detection. RWDT (6s, RTC slow clock) catches complete system freezes where even IWDT's panic handler can't run. TWDT (10s, MWDT0 in Timer Group 0) catches task-level non-yielding. Each covers a failure mode the others cannot: IWDT fails if panic handler crashes, RWDT catches that; TWDT catches task spinning without interrupts disabled, which IWDT/RWDT don't detect by design. |

## Related Documentation

- AGENTS.md — Golden Rules (GR-1 through GR-7), pre-flight checklist
- `docs/refs/project.md` — Architecture reference, pinout, thread model
- `docs/refs/coding_style.md` — C++23 conventions, error handling
- `docs/lessons_learned/` — Crash patterns (LL-001 through LL-026)
- `docs/guides/testing.md` — 3-tier testing strategy
- `legacy/rust/src/` — Original Rust implementation for reference
