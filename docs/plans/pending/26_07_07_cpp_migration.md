---
type: Plan
title: C++23 Migration — Step-by-Step Execution Plan
description: >
  Phased migration of legacy Rust firmware to C++23 + ESP-IDF v6 on ESP32-S3.
  Each step is self-contained with acceptance criteria gated by a hardware smoke
  test (build + flash + 30s serial monitor, no Guru/WDT/panic).
tags: [migration, cpp23, esp-idf-v6, esp32-s3, stepper, network, ble]
timestamp: 2026-07-07
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

### Current State (2026-07-07)

| Layer | Status | Notes |
|-------|--------|-------|
| `domain/` (types, errors, burette SM, calibration) | ✅ Done | 5 header files, 1 .cpp |
| `diag/` (black box, FFI guard, stack monitor, etc.) | ✅ Done | 4 .cpp, 5 header-only |
| `infrastructure/drivers/stepper` | ✅ Done | RMT RAII + StepperMotor |
| `infrastructure/drivers/adc` | ✅ Done | ADC1_CH3, rolling avg, calibration |
| `infrastructure/drivers/onewire` | ✅ Done | DS18B20 bitbang, atomic temp |
| `infrastructure/drivers/limitswitch` | ✅ Done | GPIO32/35 ISR, IRAM-safe |
| `infrastructure/drivers/valve` | ✅ Done | GPIO14, global atomic position |
| `infrastructure/drivers/led` | ✅ Done | Blink SM (3 transport modes) |
| `infrastructure/storage/nvs` | ✅ Done | RAII NvsHandle, f32 bit-cast |
| `application/` | ⬜ Stubs | command_dispatch + state_machine stubs |
| `interface/` | ⬜ Stubs | serial stubs |
| `infrastructure/network/` | ⬜ Missing | WiFi, HTTP, BLE not started |
| Thread model / `main.cpp` | ⬜ Minimal | Only diag init + pacer |
| Tests (Catch2) | ⬜ Partial | 4 test files, 15+ test cases |

### Remaining Work (7 Steps)

| Step | Description | Files | Smoke Test |
|------|-------------|-------|------------|
| **2** | Application Layer | ~12 new files | Build + flash + monitor, no crash |
| **3** | Interface Layer (Serial + Broadcast + REST) | ~4 new files | Build + flash + `curl /api/ping` |
| **4** | Network Layer (WiFi + HTTP + WebUI) | ~6 new files + WebUI assets | Build + flash + AP visible on phone |
| **5** | BLE Layer (NimBLE NUS GATT) | ~2 new files | Build + flash + BLE advertising visible |
| **6** | Thread Model + Main Loop Integration | modify main.cpp + ~3 new files | Build + flash + all features concurrently |
| **7** | Tests & Hardening | ~6 test files + config changes | Build + flash + 60s stability test |

---

## Step 2 — Application Layer (Command Dispatch + State Machine + Handlers)

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
- `shouldBroadcast()` with modular arithmetic at 300ms interval

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

- `idf.py build` — 0 errors, 0 warnings
- `cd build-tests && cmake ../tests && cmake --build . && ctest` — all pass
- `clang-tidy -p build/ components/application/**/*.cpp` — 0 new warnings
- 30-second serial smoke test: no Guru Meditation, no WDT, no panic

---

## Step 3 — Interface Layer (Serial UART + Broadcast + REST API Handlers)

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

- `idf.py build` — 0 errors, 0 warnings
- All host tests pass (Catch2)
- 30-second serial smoke test: device boots, serial output visible
- No new clang-tidy warnings

---

## Step 4 — Network Layer (WiFi AP/STA + HTTP Server + WebUI)

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

#### 4.1 Implement WifiManager

- `infrastructure/network/include/infrastructure/network/wifi.hpp`
- `infrastructure/network/src/wifi.cpp`
- AP mode: `esp_netif_create_default_wifi_ap()` on 192.168.4.1/24
- STA mode: NVS-backed credential persistence, auto-reconnect at 10s
- UDP DNS responder on port 53 (AP_IP:53) for captive portal detection
- mDNS: `mdns_init()` only after IP obtained
- Non-blocking `process()` for main-loop DNS polling
- `startAP()`, `startSTA(ssid, pass)`, `stop()`, `isConnected()`,
  `getIP()`

#### 4.2 Implement HttpServer

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

#### 4.3 Implement DNS responder

- Pure function in `domain/dns.hpp` (already planned in domain layer):
  `buildDnsResponse()` — constructs UDP DNS response packet
- 4 host-compilable tests for DNS packet structure

#### 4.4 Port WebUI assets

- Copy from `legacy/rust/src/webui/` to `main/webui/` or embed via
  `include..` in a `webui.hpp` component
- HTML dashboard (Bootstrap 5.3), 7 JS modules (state, ws, ui-update,
  logs, stepper, calibration, init), CSS
- Captive portal HTML page (`captive.html`)

#### 4.5 Update CMakeLists.txt

- `components/infrastructure/CMakeLists.txt` — add network/ sources,
  `esp_wifi`, `esp_http_server`, `mdns`, `lwip` to REQUIRES
- `components/interface/CMakeLists.txt` — add `webui.hpp` if separate
  component

#### 4.6 Update sdkconfig.defaults

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

## Step 5 — BLE Layer (NimBLE GATT NUS)

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

#### 5.1 Implement BleManager

- `infrastructure/network/include/infrastructure/network/ble.hpp`
- `infrastructure/network/src/ble.cpp`
- NUS GATT service with RX (write) and TX (notify) characteristics
- Bounded command queue: `xQueueCreate(8, sizeof(CommandEnvelope))`
- Pre-init guard: `bool initialized_` — `process()` / `isConnected()`
  return immediately if false
- 3-level zombie defense:
  - L1: 5 consecutive notify failures → force disconnect
  - L2: `ble_gap_conn_active() == 0` but `connected_ == true` → cleanup
  - L3: immediate kill on notify with 0 connections but flag set

#### 5.2 Implement BLE notify thread

- Dedicated `std::thread` with 8 KB stack (`BLE_NOTIFY_STACK`)
- Receives `StatusUpdate` messages from queue, serializes to JSON,
  calls `ble_gattc_notify()` / `esp_ble_gatts_send_indicate()`
- No `std::format`/`nlohmann::json::dump()` in hot path — use
  `std::format_to` into fixed buffer

#### 5.3 Add heap pre-check to init sequence

- Before `nimble_port_init()`, call `heap_caps_get_largest_free_block()`
  with `MALLOC_CAP_INTERNAL`
- If < 30 KB, log warning and skip BLE init (device runs in WiFi-only mode)
- Guards against NimBLE's uncatchable `assert()` crash (LL-007)

#### 5.4 Update CMakeLists.txt and sdkconfig

- Add `bt` component to `infrastructure/CMakeLists.txt` REQUIRES
- `sdkconfig.defaults`:
  - `CONFIG_BT_ENABLED=y`
  - `CONFIG_BT_NIMBLE_ENABLED=y`
  - `CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=12288`
  - `CONFIG_BT_NIMBLE_ACL_BUF_COUNT=12`
  - `CONFIG_BT_NIMBLE_MSYS1_BLOCK_COUNT=12`
  - `CONFIG_BT_NIMBLE_MAX_CCCD=4`
- `idf.py reconfigure` after changes

### Acceptance Criteria

- `idf.py build` — 0 errors, 0 warnings
- Flash + monitor: boot completes, "EcoTiter-XXXX" visible on phone BLE scan
- nRF Connect / phone app can connect and send `{"cmd":"serial.ping"}`
- BLE notify thread sends status updates
- 30-second stability test with concurrent WiFi + BLE: no Guru Meditation,
  no heap crash, no zombie connection

---

## Step 6 — Thread Model + Main Loop Integration

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

#### 6.1 Create motor task

- `infrastructure/src/motor_task.cpp`
- `motorTaskEntry()` — FreeRTOS task with 16 KB stack
- Homing sequence at start (sets `HOMING_DONE` flag)
- Command loop: receives `MotorCommand` via queue, executes
  `stepper.moveStepsIntervals()` with stop flag, sends result back
- Stop flag polling: checks `gStopFull` / `gStopHome` between RMT chunks

#### 6.2 Create temperature thread

- `infrastructure/src/temp_thread.cpp`
- `tempThreadEntry()` — `std::thread` with 16 KB stack
- Every 1 second: call `readSensor()` on `OneWireBus`, store result in
  `gTempCX100`
- Blocking: `vTaskDelay(pdMS_TO_TICKS(1000))`

#### 6.3 Create net_owner thread

- `infrastructure/src/net_owner.cpp`
- 16 KB stack, created at boot
- GR-3 init order:
  1.  `WifiManager::initAP()` / `WifiManager::startSTAfromNVS()`
  2.  `WifiManager::waitForIP(5s)`
  3.  `HttpServer::create()` with `stack_size = 12288`
  4.  `BleManager::init()` (if heap >= 30 KB)
- Posts initialized handles to main loop via queue

#### 6.4 Implement full app_main()

- `main/main.cpp` — rewrite from minimal loop to full application
- Boot sequence:
  1.  `nvs_flash_init()`
  2.  `BlackBox::instance().init()`
  3.  `StackMonitor::registerMainTask()`
  4.  `esp_safe::disable_wdt()`
  5.  `esp_log_level_set("*", ESP_LOG_WARN)`
  6.  Create net_owner thread (posts handles back)
  7.  Create motor task
  8.  Create temp thread
- Main loop (pacing tick = 10ms):
  1.  `TickWatchdog` RAII
  2.  `StackMonitor::checkWatermarks()` every 100 ticks
  3.  `WifiManager::process()` — DNS polling
  4.  `HttpServer::broadcastWebsocketEvent()` — status broadcast at 300ms
  5.  `BleManager::process()` — zombie defense, command drain
  6.  `SerialReader::process()` — line read, parse, dispatch
  7.  `Led::process()` — blink state machine
  8.  Transport SM — USB alive check, mode transitions
  9.  `vTaskDelayUntil(&lastWake, PACING_TICK)`

#### 6.5 Add cross-thread communication

- FreeRTOS queues (wrapped in RAII `Queue<T>` template):
  - `motor_cmd_queue` — MotorCommand from dispatch → motor task
  - `ble_cmd_queue` — BLECommand from BLE callback → main loop
  - `ble_notify_queue` — StatusUpdate from main loop → BLE notify thread
  - `init_result_queue` — net_owner → main loop (handles + status)

#### 6.6 Wire command dispatch

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

## Step 7 — Tests & Hardening

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

#### 7.1 Write remaining unit tests

- `tests/src/test_adc.cpp` — 6 tests ✅ (existing)
- `tests/src/test_valve.cpp` — 2 tests ✅ (existing)
- `tests/src/test_nvs.cpp` — f32 bit-cast round-trip, string encoding (host,
  no NVS hardware)
- `tests/src/test_dns.cpp` — DNS response packet structure, 4 test cases
- `tests/src/test_scheduler.cpp` — tick wrapping, broadcast interval
- `tests/src/test_broadcast.cpp` — event JSON serialization edge cases
- `tests/src/test_calibration.cpp` — `mlToSteps()`, `stepsToMl()`,
  `computeRamp()` ramp generation

#### 7.2 Audit sdkconfig.defaults

- Ensure all of the following are set:

```
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESP_MAIN_TASK_STACK_SIZE=32768
CONFIG_BROWNOUT_DET=n
CONFIG_ESP_INT_WDT=n
CONFIG_ESP_TASK_WDT_INIT=n
CONFIG_FREERTOS_HZ=1000
CONFIG_FREERTOS_UNICORE=y
CONFIG_LOG_DEFAULT_LEVEL_WARN=y
CONFIG_BOOTLOADER_LOG_LEVEL_ERROR=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESP_CONSOLE_UART_DEFAULT=y
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=12288
CONFIG_BT_NIMBLE_ACL_BUF_COUNT=12
CONFIG_BT_NIMBLE_MSYS1_BLOCK_COUNT=12
CONFIG_BT_NIMBLE_MAX_CCCD=4
CONFIG_LWIP_MAX_SOCKETS=5
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=4
CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=4
CONFIG_HTTPD_LOG_LEVEL=1
CONFIG_MDNS_MAX_SERVICES=1
CONFIG_PTHREAD_TASK_STACK_SIZE_DEFAULT=8192
```

#### 7.3 Create partitions.csv (if needed)

- Verify the default partition layout works for the firmware image
- Create custom `partitions.csv` only if OTA or specific NVS sizing is
  required:
  ```
  nvs,      data, nvs,     0x9000,  0x6000,
  phy_init, data, phy,     0xf000,  0x1000,
  factory,  app,  factory, 0x10000, 1M,
  ```

#### 7.4 Run linter

- `clang-tidy -p build/ components/main components/**/*.cpp` — 0 warnings
- `clang-format -i -n components/main components/**/*.cpp` — 0 differences
- `python docs/validate_okf.py` — all docs pass

#### 7.5 Final commit checklist (from AGENTS.md §10)

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

## Related Documentation

- AGENTS.md — Golden Rules (GR-1 through GR-7), pre-flight checklist
- `docs/refs/project.md` — Architecture reference, pinout, thread model
- `docs/refs/coding_style.md` — C++23 conventions, error handling
- `docs/lessons_learned.yaml` — Crash patterns (LL-001 through LL-022)
- `docs/guides/testing.md` — 3-tier testing strategy
- `legacy/rust/src/` — Original Rust implementation for reference
