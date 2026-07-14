---
type: Code Review
title: Master Audit — compliance, code quality, dead code, KISS/DRY/SOC, magic numbers
description: Consolidated audit of all C++23 source code against Constitution, coding_style.md, AGENTS.md, and project.md — merges code review + magic numbers + code quality audits
tags: [review, audit, compliance, dead-code, kiss, dry, magic-numbers, constitution, raii, gpio, wdt]
timestamp: 2026-07-14
---

# Master Audit: Compliance, Code Quality, Dead Code, KISS/DRY/SOC, Magic Numbers

**Consolidated audit — 2026-07-14.** Merges three previous reports:
- `26_07_10_code_review.md` (compliance against docs)
- `26_07_14_code_quality_audit.md` (dead code, KISS, DRY, SOC)
- `26_07_14_magic_numbers_audit.md` (embedded numeric literals)

All production source files scanned (47 files, `main/` + `components/`, excluding test_support).

---

## PART I — Compliance (Constitution, AGENTS.md, coding_style.md, project.md)

### GR-1: Never Block Main Loop ✅ PASS

| Requirement | Status | Evidence |
|-------------|--------|----------|
| No `rmt_tx_wait_all_done()` in main loop | ✅ | Only in `motor_task.cpp` (motor thread) |
| No `vTaskDelay()` > 10 ms in main loop | ✅ | Uses `vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(10))` — periodic, non-blocking at `main/main.cpp:787` |
| No `xQueueReceive(portMAX_DELAY)` | ✅ | BLE queue uses `xQueueReceive(..., 0)` (poll) at `main/main.cpp:635` |
| No `std::mutex::lock()` | ✅ | No mutex usage found in any component; only atomics + queues |
| Only `try_lock()` if mutex used | ✅ (N/A) | Codebase uses atomics + queues exclusively |
| Blocking ops in dedicated threads | ✅ | `rmt_tx_wait_all_done()` in motor thread only; `vTaskDelay(800ms)` in temp_thread for DS18B20 |

### GR-2: Mandatory RMT Stop Flags ✅ PASS

| Requirement | Status | Evidence |
|-------------|--------|----------|
| `moveStepsIntervals()` accepts `std::atomic<bool>*` | ✅ | `stepper.hpp:45-47` — `stopFlag = nullptr` |
| Check flag between chunks | ✅ | `stepper.cpp:97-101` loops check `stopFlag && stopFlag->load(memory_order_acquire)` |
| Call sites pass stop flag | ✅ | `motor_task.cpp` passes `&domain::gStopFull` at both call sites: `run_homing` (line 413) and `move_to_endstop` (line 104) |

### GR-3: DRAM Init Order ✅ PASS

| Step | Actual Code | Line |
|------|-------------|------|
| 1. WiFi init | ✅ `wifiManager.init()` | `main/main.cpp:213` (in netTaskEntry) |
| 2. tryStartSTA() | ✅ `wifiManager.tryStartSTA()` | `main/main.cpp:221` |
| 3. AP fallback | ✅ `wifiManager.startAP()` (if STA failed) | `main/main.cpp:223` |
| 4. HTTP server | ✅ `HttpServer.init()` → `registerRoutes()` | `main/main.cpp:229-232` |
| 5. BLE init | ✅ `bleManager.init()` | `main/main.cpp:246` |
| 6. Log worker created | ✅ `xTaskCreate(log_worker, ...)` | `main/main.cpp:267-274` |

All init steps execute in the `net_owner` FreeRTOS task (`netTaskEntry`), confirming the documented "net_owner thread" architecture.

### GR-4: Coexistence — Never Prefer BT ⚠️ PARTIAL

No `esp_coex_preference_set(ESP_COEX_PREFER_BT)` found anywhere. Default `ESP_COEX_PREFER_BALANCE` is used. Constitution Art. V now requires dynamic switching — see Art. V section below.

### GR-5: No Raw ESP-IDF Pointers Across Tasks ✅ PASS

- WebSocket uses `HttpServer::broadcastWsEvent()` which wraps `httpd_ws_send_frame_async()`
- No stored `httpd_req_t*` or `httpd_ws_frame_t*` across threads
- BLE command data copied into `BleCmdItem` struct (fixed-size `std::array<char, 256>`)
- Serial command data handled via `SerialReader::process()` — completes within callback
- `WsSendEntry` and `WsBroadcastEntry` copy data by value to queues

### GR-6: Stack Budget Is Law ⚠️ PARTIAL (documentation mismatch)

| Thread | Constant | Value | Status |
|--------|----------|-------|--------|
| Main loop | `CONFIG_ESP_MAIN_TASK_STACK_SIZE` | 32768 (32 KB) | ✅ |
| Motor task | `domain::MOTOR_THREAD_STACK` | 16384 (16 KB) | ✅ |
| net_owner | `domain::NET_OWNER_STACK` | **20480 (20 KB)** | ⚠️ project.md and original review said 16384 |
| Temp | `domain::TEMP_THREAD_STACK` | 16384 (16 KB) | ✅ |
| BLE notify | `domain::BLE_NOTIFY_STACK` | 8192 (8 KB) | ✅ |
| HTTP server | `domain::HTTP_SERVER_STACK` | 12288 (12 KB) | ✅ |
| Log worker | `domain::LOG_WORKER_STACK` | **12288 (12 KB)** | ⚠️ project.md says "8 KB stack is sufficient" |

**Issue:** `NET_OWNER_STACK` is 20480 in code (`domain/types.hpp:65`) but project.md does not document this increased value. `LOG_WORKER_STACK` is 12288 in code (`domain/types.hpp:66`) but project.md says "8 KB is sufficient".

### GR-7: Mandatory Diagnostic Instrumentation ⚠️ PARTIAL (1 gap)

| Instrumentation | Status | Usage |
|-----------------|--------|-------|
| `FfiGuard` at FFI boundaries | ✅ PASS | Used in wifi.cpp (7+), http_server.cpp (16+), ble.cpp (7+), motor_task.cpp (10+), temp_thread.cpp |
| `assert_rmt_preconditions()` before RMT | ⚠️ STUB | Present at `motor_task.cpp:55` but function body is empty `{}` |
| `StackMonitor::register_thread()` for new threads | ✅ PASS | `main/main.cpp:351` (main), `netTaskEntry:208`, `temp_thread.cpp:22`, `motor_task.cpp:521`, `ble_notify_thread.cpp:18` |
| `StateTracer::logBuretteTransition()` on state change | ✅ PASS | `motor_task.cpp:165,173,181,188,215, etc.` |
| `HeapSnapshot::assertCanAllocate()` for large allocs | ❌ **NEVER CALLED** | Both `canAllocate()` and `assertCanAllocate()` exist but neither is ever invoked |
| `TickWatchdog` in main loop body | ✅ PASS | `main/main.cpp:498` — RAII wrapper wraps each iteration |
| Boot progress tracking | ✅ NEW | `BootProgress` enum in `domain/types.hpp:107-120`, set at each init step |

### Constitution Art. I: Non-Blocking Main Loop ✅ PASS

(same as GR-1 — all evidence above applies)

### Constitution Art. II: Task Sovereignty ✅ PASS (with one exception — see SOC-5)

| Rule | Status | Evidence |
|------|--------|----------|
| Tasks communicate via queues or atomics | ✅ | All cross-task data flows via queues or `std::atomic` globals |
| Only net_owner touches network stack | ✅ | `wifiManager.process()`, `bleManager.process()`, `hs->broadcastWsEvent()` only called from net_owner task |
| No cross-task function calls | ✅ | Motor task never calls HTTP/BLE/WiFi functions |
| No `xTaskNotifyWait`/`xSemaphoreTake` across domains | ✅ | Not found in codebase |

**Exception (see SOC-5):** `main.cpp:706` directly reads `infrastructure::gSmResult` (motor task global) — violates task sovereignty.

### Constitution Art. III: Dual-Core Mandatory ✅ PASS

`CONFIG_FREERTOS_UNICORE=n` confirmed at `sdkconfig.defaults:31`. No task pinned to a specific core.

### Constitution Art. IV: DRAM Triangle ✅ PASS

(same as GR-3 — WiFi → HTTP → BLE order in net_owner)

### Constitution Art. V: Communication Priority ❌ NOT IMPLEMENTED

| Requirement | Status | Evidence |
|-------------|--------|----------|
| `ESP_COEX_PREFER_BT` on BLE connect + session | ❌ | `esp_coex_preference_set` is NEVER called anywhere |
| Revert to `ESP_COEX_PREFER_BALANCE` on BLE disconnect | ❌ | No coexistence preference logic exists |
| Default `ESP_COEX_PREFER_BALANCE` for captive portal | ✅ | Default (no calls) means balance is used |

Note: `esp_coex_preference_set()` is **deprecated** in ESP-IDF v6 — implementation needs investigation into the replacement API.

### Constitution Art. VI: Boundary Safety & Memory ⚠️ PARTIAL

**No Raw Pointers Across Threads:** ✅ PASS (same as GR-5)

**RAII is Law:** ❌ 2 gaps — see RAII for ESP-IDF Handles (§9.5) below

**Stack Budget:** ⚠️ Documentation mismatch (see GR-6)

### Constitution Art. VII: Hardware Protection ✅ PASS

**RMT Stop Flags:** ✅ Present in `stepper.hpp:45-47`, checked in `stepper.cpp:97-101`.

**Unsafe GPIOs (PSRAM pins 26-37):** ✅ All PSRAM pins avoided:
- `PIN_EN` = GPIO13 (moved from GPIO27 PSRAM D3)
- `PIN_DIR` = GPIO5 (moved from GPIO26 PSRAM CS1)
- `PIN_DS18B20` = GPIO6 (moved from GPIO33 PSRAM D4)
- `PIN_LIMIT_FULL` = GPIO7 (moved from GPIO34 PSRAM D5)
- `PIN_LIMIT_EMPTY` = GPIO15 (moved from GPIO35 PSRAM D6)

### Constitution Art. VIII: Memory Philosophy ✅ PASS

| Requirement | Status | Evidence |
|-------------|--------|----------|
| `CONFIG_SPIRAM_USE_CAPS_ALLOC=y` | ✅ | `sdkconfig.defaults:75` |
| Explicit `MALLOC_CAP_SPIRAM` for bulk | ✅ | `PsramResource` PMR allocator uses `heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM \| MALLOC_CAP_8BIT)` |
| `PsramBuffer` RAII template | ✅ | `psram_buffer.hpp:17-54` provides typed PSRAM buffer wrapper |
| No PSRAM for task stacks | ✅ | `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=n` |
| No PSRAM for ISR data | ✅ | Atomics are in DRAM via `std::atomic` |
| No PSRAM for RMT DMA buffers | ✅ | RMT allocates from internal memory via ESP-IDF |
| `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` | ✅ | `sdkconfig.defaults:88` |
| `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024` | ✅ | `sdkconfig.defaults:77` — ≤1KB allocs stay in DRAM |

### RAII for ESP-IDF Handles (§9.5) ⚠️ PARTIAL (2 gaps)

| Handle | Wrapped? | File |
|--------|----------|------|
| `rmt_channel_handle_t` (stepper) | ✅ `RmtChannel` class | `stepper.hpp:18-33` |
| `rmt_channel_handle_t` (RGB LED) | ❌ **`void*` cast** | `rgb_led.hpp:31`: `void* channel_{nullptr}` |
| `rmt_encoder_handle_t` (stepper) | ❌ **Raw pointer** | `stepper.hpp:63`: `rmt_encoder_handle_t encoder_ = nullptr` |
| `rmt_encoder_handle_t` (RGB LED) | ❌ **`void*` cast** | `rgb_led.hpp:32`: `void* encoder_{nullptr}` |
| `httpd_handle_t` | ✅ `HttpServer` class | `http_server.hpp` |
| `adc_oneshot_unit_handle_t` | ✅ destructor calls `adc_oneshot_del_unit()` | `adc.hpp` |
| `nvs_handle_t` | ✅ `NvsHandle` class | `nvs.hpp` |

**Issue 1:** `StepperMotor::encoder_` (`stepper.hpp:63`) is a raw `rmt_encoder_handle_t`. The destructor at `stepper.cpp:78-81` does call `rmt_del_encoder(encoder_)`, but the handle is not wrapped in a standalone RAII class. Violates §9.5.

**Issue 2:** `RgbLed` stores RMT handles as `void*` (`rgb_led.hpp:31-32`), casting to/from `rmt_channel_handle_t` and `rmt_encoder_handle_t`. Defeats type safety — 11 `reinterpret_cast` calls.

### Mutex Safety ✅ PASS (stronger than required)

No `std::mutex::lock()` calls. Only `std::atomic` and FreeRTOS queues for synchronization.

### sdkconfig / Brownout ✅ FIXED

`CONFIG_BROWNOUT_DET=n` at `sdkconfig.defaults:20`. Verified via smoke test.

### WDT Configuration ✅ PASS

| Watchdog | State | Config source |
|----------|-------|---------------|
| TWDT (Task WDT) | ✅ Enabled, 10 s | `CONFIG_ESP_TASK_WDT_INIT=y`, `CONFIG_ESP_TASK_WDT_TIMEOUT_S=10` |
| IWDT (Interrupt WDT) | ✅ Enabled, 500 ms | `CONFIG_ESP_INT_WDT=y`, `CONFIG_ESP_INT_WDT_TIMEOUT_MS=500` |
| RWDT (RTC WDT) | ✅ Configured 6 s | `RtcWatchdog` RAII at `main/main.cpp:372` |

No `esp_task_wdt_deinit()` calls. Bootloader RWDT disabled: `CONFIG_BOOTLOADER_WDT_DISABLE_IN_USER_CODE=y`.

### Error Hierarchy ✅ PASS

| Enum | Variants | Status |
|------|----------|--------|
| `StepperError` | `InitFailed`, `Rmt`, `LimitSwitchTriggered`, `LimitSwitchReached`, `Timeout` | ✅ |
| `SensorError` | `AdcReadFailed`, `TempSensorNotDetected`, `TempReadGlitch` | ✅ |
| `NetworkError` | `WifiConnectionFailed` | ✅ |
| `HardwareError` | `StepperMotor`, `Sensor`, `Network` | ✅ |
| `ProtocolError` | `InvalidJson`, `UnknownCommand`, `MissingParam` | ✅ |
| `StateError` | `Busy`, `InvalidTransition`, `AlreadyRunning` | ✅ |
| `ResourceError` | `NvsOpenFailed`, `OutOfMemory` | ✅ |
| `AppError` | `Hardware`, `Protocol`, `State`, `Resource` | ✅ |

`using Result = std::expected<T, E>` — ✅ present in `errors.hpp:57`. All error-returning functions use `[[nodiscard]]`.

### Memory Budget ✅ PASS (updated values)

| Buffer | Size | Status |
|--------|------|--------|
| `CommandBuffer` | 256 B | ✅ `memory.hpp:14` |
| `ResponseBuffer` | **2048 B** (was 512) | ✅ `memory.hpp:15` |
| `DnsBuf` | 512 B | ✅ `memory.hpp:17` |
| `AdcBuf` | 64×uint16_t | ✅ `memory.hpp:16` |
| `LOG_BUF_ENTRIES` | **1000** (was 100) | ✅ `memory.hpp:10` |

No `std::vector`/`std::string` in hot paths.

### No Forbidden Patterns

| Forbidden Pattern | Found? |
|-------------------|--------|
| `rmt_tx_wait_all_done()` in main loop | ❌ Not found ✅ |
| `std::mutex::lock()` in main loop | ❌ Not found ✅ |
| `std::format()` in hot paths | ❌ Not used at all ✅ |
| `httpd_req_t*` across threads | ❌ Not found ✅ |
| `esp_coex_preference_set(PREFER_BT)` | ❌ Not called ✅ (but now required by Art. V) |
| `python -c "..."` inline | ❌ Not found ✅ |
| `WRITE_PERI_REG` for brownout | ❌ Not found ✅ |
| `mdns_init()` before IP | ❌ Not found ✅ (called in `IP_EVENT_STA_GOT_IP` handler) |
| Naked `httpd_handle_t` | ❌ Not found ✅ |
| PSRAM pins 26-37 for `gpio_set_direction` | ❌ Not found ✅ |

---

## PART II — Magic Numbers Audit

**Summary:** 46 magic numbers found (HIGH: 8, MEDIUM: 26, LOW: 12).

### HIGH

| File | Line | Value | Code | Likely meaning |
|------|------|-------|------|----------------|
| `main/main.cpp` | 456 | `ADC_UNIT_1` / `ADC_CHANNEL_3` | `AdcDriver adc(ADC_UNIT_1, ADC_CHANNEL_3)` | Hardcoded ADC unit/channel — should be from `config.hpp` |
| `components/infrastructure/src/motor_task.cpp` | 272 | `15.0f` | `if (hz < ml_min_to_hz(15.0f)) hz = ml_min_to_hz(15.0f)` | Minimum test speed for speed calibration — used in 2 places |
| `components/infrastructure/src/motor_task.cpp` | 82 | `50.0f` | `{kDefaultStepsPerMl, 50.0f, ...}` | Hardcoded nominal volume in `ml_min_to_hz()` fallback |
| `components/domain/include/domain/cal_run_planner.hpp` | 43, 52 | `15.0f` | `if (calSpeed < 15.0f)` / `if (speedMlMin < 15.0f)` | Minimum calibration speed threshold (magic in 2 places) |
| `components/domain/include/domain/cal_speed_sm.hpp` | 106 | `50.0f` | `(50.0f / (static_cast<float>(diff) / 60000.0f))` | Hardcoded nominal volume (50ml) — should use calibration parameter |
| `components/domain/include/domain/cal_speed_sm.hpp` | 106 | `60000.0f` | `(50.0f / ... / 60000.0f)` | ms-to-minutes conversion (non-obvious inline) |
| `components/application/src/handlers/sensors.cpp` | 52, 54 | `-99999`, `100.0f` | `(tempCX100 > -99999) ? ... / 100.0f` | Temperature sentinel — repeated as literal in 5+ files |
| `components/application/src/handlers/burette_ops.cpp` | 51 | `10` | `if (steps < 10)` | Minimum step threshold for empty fallback |

### MEDIUM (abridged — top items)

| File | Line | Value | Likely meaning |
|------|------|-------|----------------|
| `main/main.cpp` | 80, 104, 128 | `384` | WS buffer size (repeated 3×) |
| `main/main.cpp` | 256 | `16` | ws_send_queue depth |
| `main/main.cpp` | 261 | `4` | ws_broadcast_queue depth |
| `main/main.cpp` | 455 | `50` | FfiGuard boundary ID (many 50-85 undocumented) |
| `components/infrastructure/network/src/http_server.cpp` | 283, 290 | `20`, `100` | Default/max log fetch limit |
| `components/infrastructure/src/drivers/rgb_led.cpp` | 133-143 | `255, 0, 0` | RGB color values (should be named constants) |
| `components/interface/src/serial.cpp` | 40 | `1024` | UART ringbuffer TX size |
| `components/infrastructure/network/src/ble.cpp` | 153 | `256` | BLE preferred MTU |
| `components/application/src/handlers/burette_ops.cpp` | 118 | `50.0f` | Rinse default speed |
| `components/application/src/handlers/burette_cal.cpp` | 178 | `20.0f`, `3` | Default fill speed, max cal sequence points |

### LOW (abridged)

TMC2209 register values (datasheet), OneWire timing delays (datasheet), WS2812 tick counts, RWDT timeout, ADC coefficient scaling factor — inherently magical but acceptable at driver level.

---

## PART III — Code Quality: Dead Code, KISS, DRY, SOC

**Summary:** 37 findings total (Dead: 17, KISS: 6, DRY: 8, SOC: 6).

### Dead Code (17 findings)

| # | File | Line | Finding | Severity |
|---|------|------|---------|----------|
| 1 | `domain/include/domain/errors.hpp` | 27-31 | `HardwareError` enum never referenced | MED |
| 2 | `domain/include/domain/types.hpp` | 39 | `LimitSwitchId` enum never used | LOW |
| 3 | `application/include/application/command_dispatch.hpp` + `.cpp` | all | `CommandDispatch` class never instantiated (Phase 1 stub) | MED |
| 4 | `application/include/application/command.hpp` + `.cpp` | 130, 346-354 | `appendCmdField()` declared+defined but never called | MED |
| 5 | `infrastructure/include/infrastructure/drivers/led.hpp` + `.cpp` | 8-55 | `Led` class fully defined but never instantiated — only `RgbLed` is used | **HIGH** |
| 6 | `infrastructure/include/infrastructure/drivers/valve.hpp` + `.cpp` | 14-15, 8-16 | `getGlobalValvePosition()` / `setGlobalValvePosition()` never called | MED |
| 7 | `infrastructure/include/infrastructure/drivers/valve.hpp` + `.cpp` | 17-38 | `Valve` class never instantiated — valve control uses raw `gpio_set_level` in motor_task | MED |
| 8 | `infrastructure/include/infrastructure/drivers/onewire.hpp` + `.cpp` | 15-16, 13-23 | `tempCelsius()` / `clearTemp()` never called — callers use `gTempCX100` | LOW |
| 9 | `domain/include/domain/motion.hpp` + `.cpp` | 16, 6-50 | `computeRamp()` never called in production (only in tests) | MED |
| 10 | `infrastructure/network/src/wifi.cpp` | 567-600, 181 | `startDnsServer()` exists but call site is commented out | MED |
| 11 | `infrastructure/src/motor_task.cpp` | 55-56 | `assert_rmt_preconditions()` — empty body, called 3× as no-op | LOW |
| 12 | `infrastructure/src/motor_task.cpp` | 603 | `SmResult::Type::None` reused as generic "done" (type abuse) | MED |
| 13 | `domain/include/domain/types.hpp` + `valve.hpp` | 78 | Duplicate `gValvePosition` global — domain one canonical, infra one dead | MED |
| 14 | `application/src/scheduler.cpp` | 18-34 | `shouldSample()`, `shouldCheckWatermarks()`, `shouldMaintain()` never called | LOW |
| 15 | `infrastructure/include/infrastructure/drivers/adc.hpp` + `.cpp` | 32, 62-74 | `readAvg()` / `resetAvg()` never called | LOW |

### KISS Violations (6 findings)

| # | File | Line | Finding | Severity |
|---|------|------|---------|----------|
| 1 | `infrastructure/src/drivers/rgb_led.cpp` | 23-43 | `void*` members with 11 `reinterpret_cast` instead of proper RAII | **HIGH** |
| 2 | `application/src/command.cpp` | 261-305 | Complex JSON id injection (memcpy into buffer) — simpler snprintf approach exists | MED |
| 3 | `application/src/dispatch.cpp` | 16-29 | Function pointer injection pattern over-engineered for single-binary embedded | MED |
| 4 | `application/src/command.cpp` | 362-393 | `serializeStatusJson()` lambda wrapper for snprintf — less readable than direct | LOW |
| 5 | `infrastructure/src/motor_task.cpp` | 116-128 | `move_fill()` / `move_empty()` 2-line wrappers — could be inlined | LOW |
| 6 | `infrastructure/network/src/http_server.cpp` | 300-310 | Two lambda helpers for JSON building where single snprintf suffices | LOW |

### DRY Violations (8 findings)

| # | File | Line | Finding | Severity |
|---|------|------|---------|----------|
| 1 | `main/main.cpp`:703-783 + `interface/src/rest_api.cpp`:225-270 | 703 | SmResult JSON formatting duplicated (~50 lines each) — **rest_api.cpp copy has hardcoded `7730.0f` bug** | **HIGH** |
| 2 | `infrastructure/src/motor_task.cpp` + `application/src/command.cpp` + `http_server.cpp` | 58-70, 370-380, 211-220 | Burette state-to-string mapping duplicated 3× with subtle inconsistencies | **HIGH** |
| 3 | `main/main.cpp` | 477-483, 545-554, 770-776 | BLE `BleNotifyItem` fill-and-send pattern duplicated 3× | MED |
| 4 | `infrastructure/network/src/http_server.cpp` + `application/src/command.cpp` | 211-220, 370-380 | State-to-string for HTTP duplicates serial mapping — 3 places to update | MED |
| 5 | `infrastructure/src/motor_task.cpp` | 79-85, 149, 253, 272, 301, 332 | `ml_min_to_hz()` recalculates speedCoeff/minFreq/maxFreq every time instead of cached cal values | MED |
| 6 | `interface/src/rest_api.cpp` | 241 | Magic `7730.0f` hardcoded as steps-per-ml divisor — ignores `CalibrationData` | MED |
| 7 | `infrastructure/src/motor_task.cpp` | 93-94, 379-382, 470-474 | Interval array init loop duplicated 3× | LOW |
| 8 | `main/main.cpp` | 151-156, 567-568 | `WsSendEntry` / `WsBroadcastEntry` identical layout — could be a template | LOW |

### Separation of Concerns Violations (6 findings)

| # | File | Line | Finding | Severity |
|---|------|------|---------|----------|
| 1 | `main/main.cpp` | 1-790 | **God file (790 lines):** init, main loop, hardware, domain state, app logic, interface — touches every layer | **HIGH** |
| 2 | `infrastructure/src/motor_task.cpp` | 1-713 | **God file (713 lines):** GPIO, RMT, TMC UART + domain state machines + coordination — domain logic should drive infrastructure, not reverse | **HIGH** |
| 3 | `infrastructure/network/src/wifi.cpp` | 1-707 | God file (707 lines): WiFi, AP, DNS, mDNS, NVS creds, event handling | MED |
| 4 | `infrastructure/network/src/http_server.cpp` | 1-657 | God file (657 lines): HTTP, REST API, WebSocket, captive portal, log formatting | MED |
| 5 | `main/main.cpp` | 706, 782 | **Cross-task coupling:** main loop directly reads `infrastructure::gSmResult` (motor task global) — violates Constitution Art. II | **HIGH** |
| 6 | `infrastructure/src/motor_task.cpp` | 72-74 | `set_valve()` writes GPIO directly inside motor_task — should be in dedicated driver | MED |

---

## PART IV — Consolidated Issues & Implementation Steps

### All Issues Summary

| # | Severity | Issue | Applies to | Status |
|---|----------|-------|------------|--------|
| 1 | HIGH | `CONFIG_BROWNOUT_DET=n` not set in sdkconfig | sdkconfig | ✅ FIXED |
| 2 | HIGH | SmResult JSON formatting duplicated — hardcoded `7730.0f` bug in rest_api | DRY | ✅ FIXED |
| 3 | HIGH | Burette state-to-string mapping in 3 places with inconsistencies | DRY | ✅ FIXED |
| 4 | HIGH | `main.cpp` god file (790 lines) — SOC-1 | SOC | ⏸️ DEFERRED |
| 5 | HIGH | `motor_task.cpp` god file (713 lines) — SOC-2 | SOC | ⏸️ DEFERRED |
| 6 | HIGH | Main loop directly reads `gSmResult` (cross-task coupling) — SOC-5 | SOC | ✅ FIXED |
| 7 | HIGH | `Led` class dead code (entire class never instantiated) | Dead | ✅ FIXED |
| 8 | MEDIUM | `rmt_encoder_handle_t` raw pointer in stepper (§9.5) — RAII-1 | RAII | ✅ FIXED |
| 9 | MEDIUM | `RgbLed` `void*` casts for RMT handles (§9.5) — RAII-2 | RAII | ✅ FIXED |
| 10 | MEDIUM | RGB LED `void*` with 11 `reinterpret_cast` (KISS-1) | KISS | ✅ FIXED |
| 11 | MEDIUM | `HardwareError` enum dead | Dead | ✅ FIXED |
| 12 | MEDIUM | `CommandDispatch` class stub dead | Dead | ✅ FIXED |
| 13 | MEDIUM | `appendCmdField()` dead | Dead | ✅ FIXED |
| 14 | MEDIUM | Valve-related dead code (`Valve` class, free functions) | Dead | ✅ FIXED |
| 15 | MEDIUM | `computeRamp()` dead (only in tests) | Dead | ✅ FIXED |
| 16 | MEDIUM | `startDnsServer()` dead (call commented out) | Dead | ✅ FIXED |
| 17 | MEDIUM | `assert_rmt_preconditions()` empty stub | Dead | ✅ FIXED |
| 18 | MEDIUM | Duplicate `gValvePosition` global | Dead | ✅ FIXED |
| 19 | LOW | `HeapSnapshot::assertCanAllocate()` never called (GR-7) | Diag | ✅ FIXED |
| 20 | LOW | Art. V coexistence preference not implemented | BLE | ✅ FIXED |
| 21 | LOW | Stack budget docs out of sync (NET_OWNER=20480, LOG=12288) | project.md | ✅ FIXED — numeric values removed, source of truth is `domain/types.hpp` |
| 22 | LOW | `onewire.cpp` calls `gpio_config()` outside centralized init | onewire | ✅ FIXED — moved to `configureGpioPins()` in main.cpp |
| 23 | LOW | 46 magic numbers (8 HIGH, 26 MED, 12 LOW) | code-wide | ✅ FIXED |
| 24 | LOW | `LimitSwitchId`, `tempCelsius`/`clearTemp`, scheduler stubs, `readAvg`/`resetAvg` dead | Dead | ✅ FIXED |

### Execution Protocol

Each step is atomic: **apply → `scripts/idf.sh build` → `scripts/idf.sh smoke` → proceed or `git revert`**.  
Smoke (`scripts/idf.sh smoke`) is the sole acceptance gate — build-only is insufficient.

### Atomic Steps (ordered by risk/dependency)

| # | Step | Risk | Smoke | Done |
|---|------|------|-------|------|
| 1 | **Shared `formatSmResult()`** — DRY-1 + 7730.0f bug | Low | ✅ | ✅ |
| 2 | **Single `buretteStateStr()`** — DRY-2 | Low | ✅ | ✅ |
| 3 | **RAII for RgbLed** — RAII-2 + KISS-1 | Low | ✅ | ✅ |
| 4 | **RAII for stepper encoder** — RAII-1 | Low | ✅ | ✅ |
| 5 | **Queue-based SmResult** — SOC-5, Art. II | Medium | ✅ | ✅ |
| 6 | **Remove dead `Led` class** | Low | ✅ | ✅ |
| 7 | **Batch dead code removal** — HardwareError, CommandDispatch, appendCmdField, Valve fns, computeRamp, startDnsServer, scheduler stubs, readAvg/resetAvg, dup gValvePosition, LimitSwitchId, tempCelsius/clearTemp | Low | ✅ | ✅ |
| 8 | **`HeapSnapshot::assertCanAllocate()`** — GR-7 gap | None | ✅ | ✅ |
| 9 | **Art. V coexistence** — BLE priority switching | Medium | ✅ | ✅ |
| 10 | **Magic numbers → named constants** | Low | ✅ | ✅ |
| 11 | **Update project.md stack sizes** — remove numeric values, reference constants | None | ✅ | ✅ |
| 12 | **clang-tidy low-effort** — NOLINT reserved identifiers, default OneWireBus dtor | Low | ✅ | ✅ |
| 13 | **Document FfiGuard boundaries** — numbering convention comment in header | None | ✅ | ✅ |
| — | **Deferred** (architectural): split main.cpp / motor_task.cpp god files | High | — | ⏸️ |

**Rollback:** Each step is a single `git add -A && git commit -m "step N: ..."`. If smoke fails — `git reset --hard HEAD~1`.

**Post-audit infra changes:**
- `scripts/idf.sh` — semgrep enforcement gate removed (2026-07-14, per user request during Step 1).

---

## PART V — Linter (clang-tidy) Findings

**Date:** 2026-07-14. Two profiles configured:
- `scripts/idf.sh tidy` — fast (~2 min), bug-catching checks from `.clang-tidy`
- `scripts/idf.sh full-tidy` — full review (~30-40 min), all check groups via `--checks=` override

**Before this audit:** linter was silently useless — `scripts/lint.sh:77` passed `--checks=-readability-*,-cppcoreguidelines-*,-modernize-*,...` on CLI, overriding `.clang-tidy` and reducing effective checks to only `clang-diagnostic`. Fixed 2026-07-14.

### Fast Profile Results (113 warnings, 1 error)

**Error (1):**
| File | Line | Finding |
|------|------|---------|
| `stepper.hpp` | 64 | Private field `dirPin_` is not used (`clang-diagnostic-unused-private-field`) |

**Warnings by category:**

| Category | Count | Severity | Description |
|----------|-------|----------|-------------|
| Cognitive complexity > 25 | ~30 | **HIGH** | God functions — see table below |
| `bugprone-incorrect-roundings` | 16 | MED | `(double + 0.5)` cast instead of `std::lround()` |
| `performance-move-const-arg` | 14 | MED | `std::move` of trivially-copyable `MotorCommand` |
| `readability-make-member-function-const` | 12 | LOW | Methods that can be `const` (NvsHandle, TMC, wifi::process, serial::write) |
| `cppcoreguidelines-owning-memory` | 7 | MED | `new`/`delete` without `gsl::owner<>` annotation |
| `bugprone-narrowing-conversions` | 4 | LOW | `size_t` → `int` in `heap_snapshot.cpp` |
| `bugprone-reserved-identifier` | 2 | LOW | `__wrap_esp_panic_handler` / `__real_esp_panic_handler` |
| `bugprone-suspicious-stringview-data-usage` | 2 | MED | `std::string_view::data()` not null-terminated |
| `clang-diagnostic-unused-but-set-variable` | 2 | LOW | `sent`/`skipped` in `http_server.cpp:598-599` |
| `performance-trivially-destructible` | 1 | LOW | `OneWireBus` destructor can be defaulted |

**Cognitive complexity hotspots (threshold 25, sorted descending):**

| File | Function | Complexity |
|------|----------|-----------|
| `wifi.cpp:258` | `tryStartSTA` | 349 |
| `motor_task.cpp:516` | `motorTaskEntry` | 343 |
| `wifi.cpp:644` | `handleEvent` | 229 |
| `main.cpp:301` | `app_main` | 225 |
| `wifi.cpp:185` | `connectSTA` | 219 |
| `ble.cpp:265` | `onHostSync` | 234 |
| `main.cpp:203` | `netTaskEntry` | 241 |
| `wifi.cpp:33` | `wifi::init` | 240 |
| `motor_task.cpp:144` | `run_rinse_sm` | 146 |
| `motor_task.cpp:362` | `run_homing` | 123 |
| `motor_task.cpp:296` | `run_cal_speed_seq_sm` | 122 |
| `wifi.cpp:101` | `startAP` | 122 |
| `serial.cpp:26` | `serial::init` | 119 |
| `http_server.cpp:495` | `root_handler` | 114 |
| `http_server.cpp:46` | `http_server::init` | 113 |
| `http_server.cpp:107` | `captive_wifi_connect_handler` | 111 |
| `motor_task.cpp:458` | `execute_move_steps` | 115 |
| `nvs.cpp:152` | `nvsInit` | 99 |
| `rgb_led.cpp:13` | `RgbLed::RgbLed` | 94 |
| `wifi.cpp:388` | `saveCredentials` | 94 |
| `wifi.cpp:610` | `startMdns` | 95 |
| `tmc_uart.cpp:35` | `tmc::init` | 95 |
| `motor_task.cpp:249` | `run_cal_speed_sm` | 78 |
| `motor_task.cpp:205` | `run_cal_dose_sm` | 76 |
| `onewire.cpp:116` | `readSensor` | 76 |
| `tmc_uart.cpp:176` | `testConnection` | 75 |
| `wifi.cpp:525` | `wifi::process` | 73 |
| `wifi.cpp:567` | `startDnsServer` | 70 |
| `http_server.cpp:416` | `ws_handler` | 67 |
| `ble.cpp:190` | `ble::process` | 60 |
| `stack_monitor.cpp:57` | `logAllWatermarks` | 58 |
| `command.cpp:98` | `parseCommand` | 56 |
| `stepper.cpp:11` | `RmtChannel::RmtChannel` | 50 |

**Suppressed noise (ESP-IDF inherent + stylistic):**
- `cppcoreguidelines-pro-type-vararg` — ESP-IDF `ESP_LOGI`/`ESP_LOGE` API
- `cppcoreguidelines-pro-type-union-access` — ESP-IDF logging unions
- `cppcoreguidelines-pro-bounds-*` — pointer/array arithmetic (embedded idiom)
- `cppcoreguidelines-avoid-c-arrays` — fixed-size buffers (embedded norm)
- `cppcoreguidelines-avoid-non-const-global-variables` — pervasive in embedded
- `readability-braces-around-statements` — style preference
- `readability-identifier-naming` — style preference

**Actionable items (by effort):**
1. **Low effort:** remove unused `dirPin_` from `stepper.hpp`; `NOLINT` the reserved identifiers in `crash_handler.cpp`; default `OneWireBus` destructor
2. **Medium effort:** replace `(double+0.5)` with `std::lround()` (16 places); remove `std::move` from trivially-copyable types (14 places); add `const` to NvsHandle/TMC methods (12 places); annotate `new`/`delete` with `gsl::owner<>` (7 places)
3. **High effort:** reduce cognitive complexity of god functions (30 functions, direct consequence of SOC god files in Part III)

### Full Profile Additional Findings

`scripts/idf.sh full-tidy` adds ~800+ extra warnings from `readability-*`, `modernize-*`, `cppcoreguidelines-*` (full), `misc-*`, `cert-*`, `portability-*` — all suppressed in fast profile as stylistic or ESP-IDF inherent noise. Full profile is only useful for code-review deep dives.

### Linter Configuration Changes (2026-07-14)

| File | Change |
|------|--------|
| `.clang-tidy` | Reduced to bug-catching checks + cognitive complexity; added suppressions for ESP-IDF noise and embedded style |
| `scripts/lint.sh:77-78` | Removed `--checks=-readability-*,-cppcoreguidelines-*,...` — was silently overriding `.clang-tidy` to nothing |
| `scripts/lint.sh:6-9` | Added `--full` flag for full review mode (overrides with all check groups via CLI) |
| `scripts/lint.sh:171-173` | Error filter: only show errors from project source tree, not ESP-IDF/toolchain headers |


- GR-1, GR-2, GR-3 compliance is strong
- Constitution Art. I, II (mostly), III, IV, VII, VIII pass
- GPIO pin migration from PSRAM-adjacent pins (26-37) complete
- `configureGpioPins()` centralization at `main/main.cpp:166`
- PSRAM allocation strategy via `PsramResource` + `PsramBuffer` clean and auditable
- Diagnostic instrumentation thorough (FfiGuard, StackMonitor, StateTracer, TickWatchdog, BootProgress)
- Error hierarchy matches documented types exactly
- mDNS init correctly deferred to `IP_EVENT_STA_GOT_IP`
- No forbidden patterns found
- Codebase free of `std::mutex::lock()` entirely
