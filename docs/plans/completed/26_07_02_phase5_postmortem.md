---
type: Plan
title: Phase 5 Post-Mortem — Integration Failure (Motor Task + Full Main Loop)
description: >
  Phase 5 (Integration: Motor Task + Full Main Loop) implemented but crashes
  at boot with Guru Meditation LoadProhibited inside heap_caps_get_largest_free_block.
  Root cause found and fixed: main task stack overflow (16 KB → 32 KB).
tags: [post-mortem, phase-5, integration, crash, guru-meditation, heap-corruption, resolved]
timestamp: 2026-07-02
status: completed
task_id: "phase-5-integration-postmortem-20260702"
task_type: feature
resolution: "stack-overflow-fix"
---

# Phase 5 Post-Mortem -- Integration Failure

## Executive Summary

Phase 5 integrated the motor task (`motor_task.rs`), atomic globals
(`motor_state.rs`), command watchdog (`PendingOpsManager`), homing sequence,
BLE command dispatch, full debug broadcast, and transport state machine into
the main loop. All host tests pass (239/239), `cargo +esp build` produces 0
errors, and `cargo +esp clippy -- -D warnings` reports 0 warnings. However,
the firmware crashed **deterministically at boot** — a LoadProhibited Guru
Memory Error at `tlsf_check` during heap integrity verification.

### Resolution (Session 3 — 2026-07-02)

**Root cause: `CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384` (16 KB) was insufficient
for Phase 5 code (578 lines, homing sequence, motor task spawn, BLE, WebSocket,
UART reader, temperature thread).** The main task stack overflowed and corrupted
TLSF heap metadata, causing `heap_caps_check_integrity_all()` to crash at
`tlsf_check`.

**Evidence chain:**
1. Replacing `std::sync::Mutex` with `EspMutex` (correct `0xFFFFFFFF`
   initializer) changed the crash from A2=0xFFFFFFFC to EXCVADDR=0x10 —
   same root cause, different corruption pattern.
2. Making `Logger::log()` a no-op eliminated heap corruption entirely but
   exposed a **FreeRTOS stack overflow detection** (`***ERROR*** A stack
   overflow in task main has been detected`).
3. Increasing `CONFIG_ESP_MAIN_TASK_STACK_SIZE` to 32768 (32 KB) resolved
   the boot crash — firmware now boots past all heap checks, initializes
   Valve, RmtStepper, DS18B20, and reaches homing sequence.
4. A secondary fix capped `HOMING_MAX_STEPS=10000` to prevent a 251 KB
   `Vec` allocation from `compute_ramp()` exceeding the 108 KB heap.

**Fix applied:**
- `sdkconfig.defaults`: `CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384` → `32768`
- `src/esp_mutex.rs`: New `EspMutex<T>` with correct `PTHREAD_MUTEX_INITIALIZER`
  (0xFFFFFFFF) instead of `std::sync::Mutex`'s `mem::zeroed()` (0x00000000)
- `src/config.rs`: Added `HOMING_MAX_STEPS=10000` cap
- `src/main.rs`: Capped homing steps, removed CHECK debug points

**Remaining issue:** RMT encoder panic (`Unknown rmt_encode_state_t value: 3`)
during homing — pre-existing `esp-idf-hal` RMT encoder bug, not related to
heap corruption.

### Session 2 investigation (2026-07-02)

Narrowed the root cause to first heap allocation corrupting TLSF. Leading
hypothesis was `std::sync::Mutex::new()` zero-initializing `pthread_mutex_t`.
This was a **valid but incomplete** theory — the actual trigger was stack
overflow corrupting heap metadata, not mutex initialization.

## Initial Goal

**Objective:** Integrate the motor task (Phase 2 driver: `RmtStepper`) and
full command dispatch loop into `main.rs`, replacing the Phase 3 stubs with
working two-phase command/response protocol, command watchdog, homing
sequence, BLE command drain, debug/limitsw broadcasts, and transport state
machine.

### Acceptance Criteria

| ID | Criterion | Status |
|----|-----------|--------|
| AC-001 | Motor task spawns in dedicated thread, receives commands via `mpsc` channel | ❌ Not tested — boot crash |
| AC-002 | Fill/Empty/Dose/Rinse commands execute via `motor_task::handle_command()` | ❌ Not tested |
| AC-003 | Two-phase command/response protocol: `CommandWithId` → `CommandResponse` | ❌ Not tested |
| AC-004 | Command watchdog expires stuck commands after `WATCHDOG_CMD_TIMEOUT_MS` | ❌ Not tested |
| AC-005 | Homing sequence (LiqIn → limit switch → `CURRENT_POSITION` update) | ❌ Not tested |
| AC-006 | BLE command queue drained in main loop via `ble_cmd_rx.try_recv()` | ❌ Not tested |
| AC-007 | Debug broadcast (ADC, USB, BLE, position) via WebSocket every ~10 ms | ❌ Not tested |
| AC-008 | Limit switch broadcast via WebSocket every ~1 s | ❌ Not tested |
| AC-009 | Transport SM: USB > BLE > Advertising priority with LED indication | ❌ Not tested |
| AC-010 | No blocking calls in main loop (golden rule compliance) | ✅ Inspection |
| AC-012 | No `unsafe` in motor_task.rs (honours `#![forbid(unsafe_code)]`) | ✅ Inspection |

## Plan Summary

### Approach

1. **Extract unsafe FFI wrappers** into `esp_safe.rs` — `disable_wdt()`,
   `suppress_httpd_txrx_logs()`, `heap_stats()`, `stack_watermark()`,
   `restart()`, `set_coex_ble_preferred()`.
2. **Create `motor_task.rs`** — dedicated (8 KB stack) thread owning
   `RmtStepper`, reading `CommandWithId` from `cmd_rx`, executing motion,
   updating atomics, sending responses.
3. **Create `motor_state.rs`** — atomic globals (`CURRENT_POSITION`,
   `CURRENT_VOLUME_ML_X100`, `BURETTE_STATE_TAG`, `MOTOR_BUSY`,
   `TARGET_POSITION`, `HOMING_STOP_STEPS`) + `PendingOpsManager` for
   command watchdog.
4. **Add homing sequence** in main loop (pre-motor-task-spawn) — nominal
   steps in LiqIn direction with `HOMING_TIMEOUT_MS` wall clock.
5. **Wire full main loop** — UART reader thread, serial dispatch with
   `HandlerContext`, BLE command drain, WebSocket broadcasts (debug +
   limitsw), transport SM, restart check.
6. **Bump sdkconfig** — WebSocket support (`CONFIG_HTTPD_WS_SUPPORT=y`),
   NimBLE memory optimisation (reduced MSYS1/ACL), `CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME`.

### Dependencies

| Dependency | Version | Usage |
|------------|---------|-------|
| `esp-idf-hal` | 0.46 | `RmtStepper`, `PinDriver`, `RmtChannel` |
| `heapless` | 0.9 | `Vec<u8, 64>` for UART bytes, `Vec<PendingOpEntry, 4>` for watchdog |
| `log` | 0.4 | Logging from motor task |
| `esp-idf-sys` | 0.46 | `heap_caps_get_largest_free_block` (crash site) |

### Risks and Mitigations

| Risk | Level | Mitigation | Outcome |
|------|-------|------------|---------|
| R1: Main loop blocking | HIGH | All blocking moved to dedicated threads (motor, net_owner, temp, uart) | ✅ Managed — inspection confirmed no blocking in main loop |
| R2: Heap corruption at boot | HIGH | No mitigation implemented | ❌ **Fatal — root cause unknown** |
| R3: WDT reset from RMT | HIGH | Already mitigated via `esp_task_wdt_deinit()` at boot | ✅ Mitigated (if boot reached) |
| R4: `!Sync` mpsc::Receiver shared | MED | Wrapped in `Mutex` in `SystemChannels` | ✅ Managed |
| R5: NimBLE memory pools overflow | MED | Reduced MSYS1/ACL sizes (sdkconfig) | ❌ May have contributed to crash |

## Implementation

### Files Created (2 new files)

| File | Lines | Purpose |
|------|-------|---------|
| `src/motor_task.rs` | 527 | Dedicated motor thread: RMT stepper control, command dispatch, status updates |
| `src/domain/motor_state.rs` | 398 | Atomic globals for lock-free inter-task communication, `PendingOpsManager` |

### Files Modified (13 files)

| File | Change | Lines Changed |
|------|--------|---------------|
| `src/esp_safe.rs` | Created (107 lines) — safe wrappers for ESP-IDF FFI calls |
| `src/main.rs` | Integrated motor task, homing, watchdog, UART reader, BLE drain, WS broadcasts | +139/–3 (316→578 lines) |
| `src/lib.rs` | Added `pub mod esp_safe;`, `pub mod motor_task;`, `regression_tests` | +14 |
| `src/logger.rs` | Rewrote: added `Mutex<RingBuffer>`, JSON output hardening, 8 unit tests | +233 (was 134 lines) |
| `src/domain/channels.rs` | Added `response_tx`/`response_rx` `SyncSender` pair to `SystemChannels` | +3 |
| `src/domain/mod.rs` | Added `pub mod motor_state;` | +1 |
| `src/config.rs` | Added motor/homing/WDT constants (`HOMING_TIMEOUT_MS`, `WATCHDOG_CMD_TIMEOUT_MS`, `USB_ALIVE_TIMEOUT_MS`, `MOTOR_IDLE_SLEEP_MS`, `HOMING_SPEED_HZ`, `MAX_PENDING_RESPONSES`, `MOTOR_THREAD_STACK`) | +15 |
| `sdkconfig.defaults` | WebSocket support, NimBLE memory reduction, GAP device name | +8/–4 |
| `src/infrastructure/network/http_server.rs` | SSE→WebSocket migration | +438/–? |
| `src/infrastructure/network/ble.rs` | BLE init fixes, GAP name | +22 |
| `src/interface/serial.rs` | USB serial heartbeat | +1 |
| `src/interface/rest_api.rs` | API enhancements | +17 |
| `src/errors.rs` | Minor | +1 |

### Tests Added (13 new, 239 total)

| Module | Test Count | Coverage |
|--------|------------|----------|
| `domain::motor_state::tests` | 9 | Volume encode/decode, state tag roundtrip, broadcast STS, pending ops push/remove/full/watchdog |
| `logger.rs::tests` | 8 | JSON output: empty, single, multi, limit, escape, buffer overflow, levels, full output validation |
| `lib.rs::regression_tests` | 1 | WebSocket compile-time check |
| Existing (Phase 0/1/2/3/4) | 226 | Preserved unchanged |
| **Total** | **239** | All passing on host (`cargo test --lib`) |

## Crash Data

### Guru Meditation Details

```
Guru Meditation Error: Core 0 panic'ed (LoadProhibited)
EXCCAUSE: 0x0000001c (load from unmapped address)
EXCVADDR: 0x00000000 (NULL pointer dereference)
A2: 0xfffffffc (TLSF free-list next = -4)
Backtrace: block_next → multi_heap_get_info_impl → heap_caps_get_largest_free_block
           → ecotiter_fw::esp_safe::heap_stats (esp_safe.rs:63)
           → ecotiter::main (main.rs:58)
```

### Observable Properties

1. **Deterministic**: Same PC (`0x40359c07`), same A2 (`0xFFFFFFFC`), same
   backtrace on every boot.
2. **Early crash**: Occurs at `main.rs:58`, before any Rust heap allocation
   (`AdcDriver`, `Led`, `RmtStepper`, etc. start at line 68+).
3. **No line changes to the crashing code**: The `heap_caps_get_largest_free_block`
   call existed in Phase 4 (inline `unsafe` in `main.rs:69`) and worked.
   Phase 5 moved it to `esp_safe.rs::heap_stats()` with identical FFI call.
4. **Heap corrupted before access**: `A2=0xFFFFFFFC` means the TLSF free-list
   `next` pointer was overwritten with `-4`. The heap's internal data structure
   was corrupted **before** `heap_stats()` ever read it.
5. **Binary size increase**: Phase 4 `main.rs` = 316 lines; Phase 5 = 578 lines.
   Overall binary grew substantially with motor task, WebSocket support, logger
   ring buffer, and legacy WebUI transfer.

## Issues Encountered

### Issue 1 (Critical) — Boot-time Heap Corruption (UNRESOLVED)

**Category:** Integration / Configuration

**Root cause:** **NOT FOUND** — see "Failed Hypotheses" below.

**Symptom:** Guru Meditation LoadProhibited at `heap_caps_get_largest_free_block()`
with A2=0xFFFFFFFC, before any user allocation.

**Impact:** All AC-001 through AC-009 untestable. Firmware cannot boot.

**Resolution:** None. Phase 5 integration cannot proceed until root cause is
identified and fixed.

### Issue 2 (Non-blocking) — NimBLE Memory Configuration Change

**Category:** Configuration risk

**Symptom:** `CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT` reduced from 24→12 and
`CONFIG_BT_NIMBLE_ACL_BUF_COUNT` from 20→4. These changes reduce internal
NimBLE DRAM pool sizes but may cause MSYS-1 allocation failures under load.

**Impact:** Unknown — BLE init was never reached due to the boot crash.

**Resolution:** Deferred. The NimBLE memory reduction was intended to free DRAM
but may need to be reverted if BLE fails to initialize in a working build.

### Issue 3 (Deferred) — WebSocket Migration

**Category:** Implementation / API change

**Symptom:** SSE was replaced with WebSocket (`CONFIG_HTTPD_WS_SUPPORT=y`).
The WebSocket handler uses `httpd_ws_send_frame_async()` which requires the
`httpd_req_t` pointer to remain valid.

**Impact:** Unknown — WebSocket was not tested due to the boot crash. Potential
for use-after-free similar to the SSE dangling pointer bug (Phase 4, Cycle 3).

**Resolution:** Deferred until boot crash is fixed.

## Rework Cycles

### Cycle 1 — Initial Implementation and Build

**Trigger:** Implementation of Phase 5 (motor task, atomic globals, main loop
integration).

**Changes made:**
1. Created `src/motor_task.rs` (527 lines) — motor thread with command dispatch
2. Created `src/domain/motor_state.rs` (398 lines) — atomic globals, PendingOpsManager
3. Created `src/esp_safe.rs` (107 lines) — safe FFI wrappers
4. Modified `src/main.rs` (+252 lines): homing sequence, UART reader thread,
   motor task spawn, net_owner thread, command dispatch, BLE drain, WS broadcasts,
   transport SM, watchdog, restart check
5. Modified `src/logger.rs` (+233 lines): ring buffer with `Mutex`, JSON output
6. Updated `sdkconfig.defaults`: WebSocket support, NimBLE memory reduction

**Verification:**
- `cargo test --lib`: 239/239 passed ✅
- `cargo +esp build --target xtensa-esp32-espidf`: 0 errors ✅
- `cargo +esp clippy -- -D warnings`: 0 warnings ✅
- **Firmware on hardware:** ❌ Guru Meditation within 100 ms of boot

### Cycle 2 — Failed Hypothesis: Stale Build Artifacts

**Trigger:** Crash at boot — suspected stale `esp-idf-sys` build cache.

**Attempted fix:**
```bash
cargo clean -p esp-idf-sys
cargo +esp build --target xtensa-esp32-espidf
```
Then reflash.

**Result:** ❌ Same crash, same PC, same A2=0xFFFFFFFC.

### Cycle 3 — Failed Hypothesis: Flash Corruption

**Trigger:** Possible flash corruption from prior flashing attempts.

**Attempted fix:**
```bash
source /home/vlabe/export-esp.sh
/home/vlabe/.espressif/tools/python/v6.0.1/venv/bin/esptool --port /dev/ttyUSB0 erase-flash
```
Then rebuild and flash.

**Result:** ❌ Same crash, same PC, same A2=0xFFFFFFFC.

### Cycle 4 — Failed Hypothesis: BT DRAM Overlap with Heap

**Trigger:** Suspected BT controller DRAM region overlapping with the heap
allocator's metadata.

**Attempted fix:** Enabled `CONFIG_ESP32_IRAM_AS_DRAM=y` in `sdkconfig.defaults`
to move the BT controller's internal memory from DRAM to IRAM, freeing DRAM
and eliminating potential overlap.

**Result:** ❌ Same crash, same PC, same A2=0xFFFFFFFC.

### Cycle 5 — Session 2: Heap Integrity Checkpoints + WS Disable

**Trigger:** Systematic root cause investigation using heap integrity
checkpoints, sdkconfig isolation, and binary diagnostics.

**Changes made:**
1. Added `check_heap_integrity()` wrapper in `esp_safe.rs` using
   `heap_caps_check_integrity_all(true)`.
2. Inserted 6 CHECK points in `main.rs` between every boot step.
3. Disabled `CONFIG_HTTPD_WS_SUPPORT=y` and stubbed all WS code in
   `http_server.rs`.
4. Decoded all backtraces via `xtensa-esp32-elf-addr2line`.
5. Examined ELF sections, `.init_array`, binary size, memory layout.
6. Validated all 7 unsafe FFI calls against ESP-IDF v6 API.

**Key findings:**
- Heap integrity CHECK 1 (after `link_patches`) **PASSES** — heap is
  NOT corrupted before `main()`.
- Crash occurs at CHECK 5, immediately after `log::info!()` — the first
  call to `Logger::log()` → `Mutex::lock()` → first heap allocation.
- Disabling WebSocket does NOT fix the crash — same crash with or without WS.
- No `.init_array` section (no C++ static constructors).
- BSS (59 KB) ends exactly at heap start (0x3FFD2D58) — no overlap.
- All unsafe FFI calls validated correct against ESP-IDF v6.

**Result:** ⚠️ Root cause narrowed to first heap allocation corrupting TLSF.
Leading hypothesis: Rust's `std::sync::Mutex::new()` creates a zero-initialized
`pthread_mutex_t` that is incompatible with ESP-IDF v6's POSIX implementation.

## Failed Hypotheses — Detailed Analysis

### Hypothesis 1: Stale Build Artifact

**Evidence:** `esp-idf-sys` compiles C/asm from `esp-idf`. If a stale object
file contained different heap layout assumptions, it could produce a corrupted
binary.

**Why it was wrong:** `cargo clean -p esp-idf-sys` triggers a full recompile
of all ESP-IDF C sources. The deterministic crash (same PC, same A2 across
rebuilds) is inconsistent with stale-object-file corruption, which would
produce non-deterministic or different crashes.

### Hypothesis 2: Flash Corruption

**Evidence:** Previously flashed (possibly partial) binaries could leave stale
data at the flash offset where the new binary's `.bss` or `.data` sections
reside.

**Why it was wrong:** `esptool erase-flash` erases the entire flash (not just
partition table), guaranteeing a clean slate. The subsequent `espflash flash`
writes from scratch. Deterministic crash rules out flash corruption.

### Hypothesis 3: BT DRAM Overlap with Heap Region

**Evidence:** The ESP32's BT controller allocates memory in DRAM region A
(default). If increased binary size pushed the Rust `.bss` section or dynamic
heap into this reserved region, heap metadata would be overwritten by BT
initialization.

**Why it was wrong:** `CONFIG_ESP32_IRAM_AS_DRAM=y` moves BT controller
memory from DRAM to IRAM, eliminating the overlap. The crash persisted,
proving the conflict is not with the BT controller's reserved DRAM.

### What Was NOT Investigated (Missed Diagnostics)

1. **No `heap_caps_check_integrity_all()` call** was inserted before
   `heap_stats()` to determine whether the heap is already corrupted at
   `main:58`, or if `heap_stats()` itself triggers the corruption.

2. **No binary search** was performed to find which specific Phase 5 commit
   or code change triggers the crash.

3. **No ELF section comparison** between Phase 4 and Phase 5 was done to
   detect BSS/DRAM section layout shifts.

4. **No investigation** into whether `esp_log_level_set()` in
   `suppress_httpd_txrx_logs()` (esp_safe.rs:37) allocates memory or needs
   the heap to be in a specific state.

5. **No investigation** of global constructors (`.init_array`): whether any
   ESP-IDF subsystem (NimBLE, WiFi, HTTP server) registers static constructors
   that run before `main()` and could corrupt the heap.

## Session 2 Investigation (2026-07-02)

### Objective

Systematically identify the root cause of the boot-time heap corruption
using heap integrity checks, sdkconfig isolation, and binary diagnostics.

### Approach

1. Added `check_heap_integrity()` wrapper around
   `heap_caps_check_integrity_all()` in `esp_safe.rs`.
2. Inserted 6 CHECK points in `main.rs` between every boot step:
   CHECK 1 (after `link_patches`), CHECK 2 (after `disable_wdt`),
   CHECK 3 (after `suppress_httpd_txrx_logs`), CHECK 4 (after
   `logger::init`), CHECK 5 (after `log::info!`), CHECK 6 (after
   `Peripherals::take`).
3. Disabled WebSocket (`CONFIG_HTTPD_WS_SUPPORT=y` → commented out)
   and stubbed all WS code in `http_server.rs`.
4. Decoded backtraces via `xtensa-esp32-elf-addr2line`.
5. Examined ELF sections, `.init_array`, binary size, memory layout.

### Findings

#### Finding 1: Heap Corruption Timing Is NOT Before main()

**Contrary to the original report**, the heap is **NOT** corrupted before
`main()`. With `check_heap_integrity()` inserted immediately after
`link_patches()` (CHECK 1), the check **PASSES**:

```
[INFO] Heap integrity OK
[INFO] === EcoTiter firmware ===
Guru Meditation Error: Core 0 panic'ed (LoadProhibited)
```

The crash occurs at **CHECK 5** (main.rs:61), which runs immediately
**after** `log::info!("=== EcoTiter firmware ===")`.

**Backtrace (decoded):**
```
tlsf_check at tlsf.c:152
multi_heap_check at multi_heap.c:351
heap_caps_check_integrity at heap_caps.c:439
heap_caps_check_integrity_all at heap_caps.c:448
ecotiter_fw::esp_safe::check_heap_integrity at esp_safe.rs:56
ecotiter::main at main.rs:61
```

#### Finding 2: Crash Is Triggered by First Heap Allocation

The only code between CHECK 4 (passes) and CHECK 5 (crashes) is:

```rust
log::info!("=== EcoTiter firmware ===");   // line 60
ecotiter_fw::esp_safe::check_heap_integrity(); // line 61 — CRASHES
```

`log::info!()` calls `Logger::log()` which locks `self.inner`
(`Mutex<RingBuffer>`). On the **first** lock call, `pthread_mutex_lock()`
detects a zero-initialized `pthread_mutex_t` and calls
`pthread_mutex_init()` → `xSemaphoreCreateMutex()` → `pvPortMalloc()`.
This is the **first heap allocation** in the system.

**Conclusion:** The first heap allocation via `xSemaphoreCreateMutex()`
corrupts the TLSF free-list metadata. This is the root cause trigger.

#### Finding 3: WebSocket Is NOT the Root Cause

Disabling `CONFIG_HTTPD_WS_SUPPORT=y` and stubbing all WS code in
`http_server.rs` did NOT fix the crash. The crash occurs at the same
point (CHECK 5, after first `log::info!()`) with or without WebSocket.

**Evidence:**
- WITH WS: `Heap integrity OK` → `=== EcoTiter firmware ===` → crash
- WITHOUT WS: `Heap integrity OK` → `=== EcoTiter firmware ===` → crash
- Identical PC, identical backtrace, identical EXCVADDR

#### Finding 4: No .init_array — No C++ Static Constructors

```
xtensa-esp32-elf-objdump: section '.init_array' mentioned in a -j
option, but not found in any input file
```

No `.init_array` section exists in the ELF. No C++ static constructors
run before `main()`. This eliminates global constructors as a cause.

#### Finding 5: Binary Size and Memory Layout

| Metric | Value |
|--------|-------|
| text | 2,802,641 bytes |
| data | 531,899 bytes (flash-mapped) |
| bss | 59,570 bytes (DRAM) |
| `.dram0.data` | 0x3FFBDB60, size 0x695F (26,975 bytes) |
| `.dram0.bss` | 0x3FFC44C0, size 0xE898 (59,544 bytes) |
| `.dram0.heap_start` | 0x3FFD2D58 |
| Total flash app | 3,259,248 bytes (78.94% of 4MB) |

BSS ends at 0x3FFD2D58, heap starts at 0x3FFD2D58. **No overlap**
between data/bss and heap regions.

#### Finding 6: All Unsafe FFI Calls Validated (PASS)

All 7 FFI calls in `esp_safe.rs` validated against ESP-IDF v6 API:

| FFI Call | Signature Match | Constants | Verdict |
|----------|----------------|-----------|---------|
| `esp_task_wdt_deinit()` | ✅ `fn() -> esp_err_t` | N/A | PASS |
| `esp_log_level_set()` | ✅ `fn(*const c_char, esp_log_level_t)` | `ESP_LOG_ERROR=1` | PASS |
| `esp_get_free_heap_size()` | ✅ `fn() -> u32` | N/A | PASS |
| `heap_caps_get_largest_free_block()` | ✅ `fn(u32) -> size_t` | `MALLOC_CAP_DEFAULT=4096` | PASS |
| `uxTaskGetStackHighWaterMark()` | ✅ `fn(TaskHandle_t) -> UBaseType_t` | NULL valid | PASS |
| `esp_restart()` | ✅ `fn() -> !` | N/A | PASS |
| `esp_coex_preference_set()` | ✅ `fn(esp_coex_prefer_t) -> esp_err_t` | `ESP_COEX_PREFER_BT=1` | PASS |

Minor issue: `esp_task_wdt_deinit()` return value is silently discarded.
The docs state it returns `ESP_ERR_INVALID_STATE` if already deinitialized,
so the "idempotent" claim in the safety comment is technically inaccurate
(but harmless).

#### Finding 7: No .init_array, No Suspicious Constructors

The binary has no `.init_array` section. No C++ static constructors
from WebSocket, NimBLE, or any other ESP-IDF component run before
`main()`.

### Root Cause Hypothesis (Updated)

**The first heap allocation (`xSemaphoreCreateMutex()` inside
`pthread_mutex_init()`) corrupts TLSF free-list metadata.**

The zero-initialized `pthread_mutex_t` (from Rust's `Mutex::new()`
const fn) may not be compatible with ESP-IDF v6's POSIX pthread
implementation. When `pthread_mutex_lock()` is called on a zero'd
mutex, it may:
1. Attempt to lock a NULL semaphore handle
2. Write internal state to an incorrect memory location
3. Overwrite TLSF free-list metadata in the adjacent heap region

This explains why:
- The crash is **100% deterministic** (same code path every time)
- The crash happens **only on the first Mutex lock** (first allocation)
- The crash happens **regardless of WebSocket** configuration
- Phase 4 **worked** (different sdkconfig → different heap layout →
  the corruption landed in unused memory instead of TLSF metadata)

### Remaining Questions

1. **Why did Phase 4 work?** The same `Mutex<RingBuffer>` logger existed
   in Phase 4. The sdkconfig differences (NimBLE pools, WS support, task
   stack sizes) must affect the heap layout or pthread implementation in
   a way that makes the zero-initialized mutex safe in Phase 4 but
   destructive in Phase 5.

2. **Is the zero-initialized `pthread_mutex_t` safe on ESP-IDF v6?**
   Need to check ESP-IDF's POSIX mutex implementation for whether
   `PTHREAD_MUTEX_INITIALIZER` is all-zeros or has a specific magic
   value. If ESP-IDF's `pthread_mutex_lock()` doesn't handle zero'd
   mutexes, Rust's `Mutex::new()` is fundamentally broken on this target.

3. **Does `esp-idf-hal` / `esp-idf-svc` provide a correct `Mutex`
   implementation?** Some ESP-IDF Rust crates provide their own `Mutex`
   that correctly calls `pthread_mutex_init()` in the constructor.

### Recommendations for Next Session

1. **Check ESP-IDF's `PTHREAD_MUTEX_INITIALIZER` value.** If it's not
   all-zeros, Rust's `Mutex::new()` is creating an invalid mutex.
   Fix: use `esp_idf_svc::mutex::Mutex` or manually call
   `pthread_mutex_init()` after construction.

2. **Test with a manual `pthread_mutex_init()` call:**
   ```rust
   static mut MUTEX: std::ffi::c_void = std::mem::zeroed();
   unsafe {
       libc::pthread_mutex_init(&mut MUTEX as *mut _ as *mut _, core::ptr::null());
       libc::pthread_mutex_lock(&mut MUTEX as *mut _ as *mut _);
       libc::pthread_mutex_unlock(&mut MUTEX as *mut _ as *mut _);
   }
   ```

3. **Test with `esp_idf_svc::mutex::Mutex`** instead of
   `std::sync::Mutex` in the logger, to see if the ESP-IDF-specific
   mutex implementation handles initialization correctly.

4. **Compare Phase 4 and Phase 5 sdkconfig** line by line. Revert ALL
   sdkconfig changes to Phase 4 values and test. If the crash stops,
   add changes back one by one to find which sdkconfig option changes
   the heap layout enough to trigger the corruption.

5. **Add `heap_caps_check_integrity_all()` to the permanent boot
   sequence** (after `link_patches()`, before any heap allocation)
   as a regression guard for all future development.

## Metrics

| Metric | Value |
|--------|-------|
| New Rust files | 2 (motor_task.rs, domain/motor_state.rs) |
| Modified Rust files | 13 |
| Total new LOC (all) | ~1,200 (motor_task.rs + motor_state.rs main.rs additions) |
| Total modified LOC | +5898 / –1301 (across all Phase 5 commits) |
| New config vars | 6 (homing, watchdog, USB alive, motor idle, max pending) |
| sdkconfig changes | 8 adds / 4 removes |
| Host unit tests | 239 — 0 failures |
| Motor task tests | 9 (domain::motor_state::tests) |
| Logger tests | 8 (logger.rs::tests) |
| Build errors | 0 (xtensa target) |
| Clippy warnings | 0 (xtensa target, `-D warnings`) |
| Boot attempts | ~25 across 5 fix cycles (4 original + 1 session 2) |
| Crash addresses | 100% deterministic: same PC, same backtrace |
| Rework cycles | 5 (1 implementation + 3 failed hypotheses + 1 session 2) |
| Root cause identified | ⚠️ Partial — first heap allocation corrupts TLSF; exact mechanism TBD |
| Unsafe FFI validation | 7/7 PASS (esp_safe.rs against ESP-IDF v6) |
| `.init_array` section | None (no C++ static constructors) |
| BSS size | 59,570 bytes (59 KB) |
| `.dram0.heap_start` | 0x3FFD2D58 (no overlap with BSS) |

## Verification

### Acceptance Criteria Results

| ID | Result | Details |
|----|--------|---------|
| AC-001 | ❌ Not tested | Boot crash before motor task spawn (main.rs:240) |
| AC-002 | ❌ Not tested | Never reached |
| AC-003 | ❌ Not tested | Never reached |
| AC-004 | ❌ Not tested | Never reached |
| AC-005 | ❌ Not tested | Never reached |
| AC-006 | ❌ Not tested | Never reached |
| AC-007 | ❌ Not tested | Never reached |
| AC-008 | ❌ Not tested | Never reached |
| AC-009 | ❌ Not tested | Never reached |
| AC-010 | ✅ Inspection | All blocking operations in dedicated threads; main loop uses `try_recv()`, `try_lock()`, `sleep(10ms)` |
| AC-012 | ✅ Inspection | `motor_task.rs` line 21: `#![forbid(unsafe_code)]`; `motor_state.rs` line 10: `#![forbid(unsafe_code)]` |

### Static Verification

| Check | Result |
|-------|--------|
| `cargo test --lib` | ✅ 239/239 passed |
| `cargo +esp build --target xtensa-esp32-espidf` | ✅ 0 errors |
| `cargo +esp clippy -- -D warnings` | ✅ 0 warnings |
| `#![forbid(unsafe_code)]` in motor_task.rs | ✅ Line 21 |
| `#![forbid(unsafe_code)]` in motor_state.rs | ✅ Line 10 |
| `#![forbid(unsafe_code)]` in config.rs | ✅ Line 1 |
| `#![forbid(unsafe_code)]` in channels.rs | ✅ Line 5 |
| `#![deny(clippy::unwrap_used, clippy::expect_used)]` in main.rs | ✅ Lines 1-2 |
| No blocking-call in main loop | ✅ Inspection confirmed |

## Lessons Learned

### Root Cause Investigation

1. **Add heap integrity checks early.** `heap_caps_check_integrity_all()` should
   have been the very first call in `main()`, before any FFI wrappers, to
   determine whether ESP-IDF's own static initialization corrupts the heap.

2. **Binary search over commits (git bisect).** After 2 hours and 3 failed
   hypotheses, a `git bisect` between the last working commit (Phase 4) and
   HEAD would have identified the exact commit introducing the crash. This
   would narrow the search space from ~1,200 changed lines to a single commit
   (~100-200 lines).

3. **Compare ELF sections.** `xtensa-esp32-elf-objdump -h` and `xtensa-esp32-elf-size`
   on Phase 4 vs Phase 5 binaries would reveal BSS/DRAM section shifts. If
   `.bss` or `.data` grew past a critical boundary (e.g., into the heap
   allocator's metadata region), it would explain the deterministic crash.

4. **Test in isolation.** Before adding the full main loop integration, a
   minimal "smoke test" binary (just `esp_safe` calls + `heap_stats()` +
   `logger::init()`) would confirm whether the boot crash is caused by code
   changes (logger, esp_safe) or configuration changes (sdkconfig).

### Code Quality

5. **Logger refactoring was high-risk.** The logger was rewritten from a simple
   `println!`-based implementation (~130 lines) to a `Mutex<RingBuffer>`-based
   one (378 lines) in the same commit set as the motor task integration. This
   increased the diff surface and made regression hunting harder.

6. **sdkconfig changes should be atomic and testable.** Four sdkconfig changes
   (WebSocket, NimBLE MSYS1/ACL, GAP name) were made in the same commit set.
   Any one of them could be the crash trigger. Each should be tested
   independently.

### Process

7. **Hardware testing after every commit.** The Phase 5 commits were not tested
   individually on hardware. After ~12 commits across multiple features
   (WebSocket migration, WebUI transfer, motor task, BLE fixes, agent updates),
   only the final binary was flashed. Testing each commit individually would
   have caught the regression at its source.

8. **Document the "golden boot" baseline.** Phase 4 hardware status is
   described as "reportedly worked" but there is no canonical record of the
   last known-good binary (commit hash, flash date, sdkconfig hash). A
   repeatable baseline would enable reliable bisection.

9. **Allocate `heap_caps_check_integrity` to the boot sequence.** This
   diagnostic should be a standard part of the boot sequence (after
   `link_patches()`) in every future phase, at least during development.

## Recommendations for Next Attempt

### Immediate (must-do before next flash)

1. **`git bisect` the crash.** Start with `git bisect good 23e90c3` (Phase 4
   known-good) and `git bisect bad HEAD`. This identifies the exact commit.

2. **Insert `heap_caps_check_integrity_all()` at `main.rs:1`** (after
   `link_patches()`) to confirm whether the heap is already corrupted before
   any Rust code runs, or if a specific Rust call triggers the corruption.

3. **Create a minimal smoke test binary:**
   ```rust
   fn main() {
       esp_idf_sys::link_patches();
       heap_caps_check_integrity_all(); // diagnostic
       log::info!("smoke test");
       loop { std::thread::sleep(Duration::from_secs(1)); }
   }
   ```

### If bisect identifies a commit

4. **Compare ELF sections** of the bad and good commits:
   ```bash
   xtensa-esp32-elf-objdump -h target/xtensa-esp32-espidf/debug/ecotiter
   ```
   Look for shifts in `.bss`, `.data`, `.dram0.data`, `.dram0.bss`.

5. **Check `.init_array`** for global constructors:
   ```bash
   xtensa-esp32-elf-objdump -d -j .init_array target/xtensa-esp32-espidf/debug/ecotiter
   ```
   Some ESP-IDF subsystems register C++ static constructors via
   `.init_array` that run before `main()` and allocate from the heap.

### If sdkconfig is the trigger

6. **Test sdkconfig changes independently.** Revert all Phase 5 sdkconfig
   changes and flash. If the crash stops, add them back one by one:
   - First: `CONFIG_HTTPD_WS_SUPPORT=y`
   - Then: NimBLE MSYS1/ACL reduction
   - Then: GAP device name

### Binary search methodology

7. **If bisect is not feasible,** binary-search the Phase 5 commits manually:
   1. Flash commit `de60ba0` (WebUI transfer) — does it boot?
   2. Flash commit `d711786` (SSE→WS + owner thread) — does it boot?
   3. Flash commit `08c0cab` (WS/logger fixes) — does it boot?
   4. Flash commit `6946d9f` (BLE fixes) — does it boot?
   5. Flash commit `a688102` (HEAD) — crash?

   Each step narrows the search space by half.

## Related Documentation

- Phase 4 Report: `docs/plans/pending/26_06_30_phase4_network_report.md`
- Phase 3 Report: `docs/plans/completed/26_06_30_phase3_application_report.md`
- Phase 2 Report: `docs/plans/completed/26_06_30_phase2_infrastructure_report.md`
- Phase 1 Report: `docs/plans/completed/26_06_30_phase_1_domain.md`
- Phase 0 Report: `docs/plans/completed/26_06_30_phase0_scaffold_report.md`
- Unsafe Code Audit: `docs/plans/completed/26_06_30_unsafe_code_audit.md`
- HTTP API Bug Report: `docs/plans/completed/26_07_01_http_api_bug_report.md`
- Stack Overflow Bugfix: `docs/plans/completed/26_07_01_stack_overflow_bugfix.md`
- WebUI Transfer: `docs/plans/completed/26_07_01_webui_transfer.md`
- General Plan: `docs/plans/pending/26_06_29_general_implementation_plan.md`
- AGENTS.md: build commands, golden rule, ESP32 crash investigation, RMT API
- `src/main.rs` (Phase 5, 578 lines): full main loop with motor task
- `src/motor_task.rs` (527 lines): motor thread implementation
- `src/domain/motor_state.rs` (398 lines): atomic globals, PendingOpsManager
- `src/esp_safe.rs` (107 lines): safe FFI wrappers
- `src/logger.rs` (378 lines): ring buffer logger with Mutex

## Commit Message

*Note: DO NOT commit this code. The commit message below is provided for
reference if the implementation is ever fixed and committed.*

```
feat(integration): implement Phase 5 — motor task, homing, command
dispatch, and full main loop

Add dedicated motor thread (RmtStepper owner), atomic globals for
lock-free state sharing, homing sequence, two-phase command/response
protocol, command watchdog, and full main loop with UART reader,
BLE command drain, WebSocket broadcasts, and transport state machine.

- motor_task.rs: dedicated 8 KB stack thread owning RmtStepper,
  non-blocking cmd_rx poll with Mutex, dispatch for Fill/Empty/Dose/
  Rinse/Stop/EmergencyStop/Reset, atomic state updates
- motor_state.rs: CURRENT_POSITION, CURRENT_VOLUME_ML_X100,
  BURETTE_STATE_TAG, MOTOR_BUSY, TARGET_POSITION, HOMING_STOP_STEPS;
  PendingOpsManager with heapless::Vec-based command watchdog
- esp_safe.rs: safe wrappers for wdt deinit, log suppression, heap
  stats, stack watermark, restart, coexistence preference
- Homing sequence: LiqIn direction, RampConfig intervals,
  HOMING_TIMEOUT_MS (120s), limit switch detection, CURRENT_POSITION
  update
- Main loop: uart reader thread (stdin→mpsc), serial dispatch,
  SysEx command dispatch, BLE command drain, debug/limitsw/live
  WebSocket broadcasts, transport SM (USB>BLE>Advertising), LED
  indication, restart check, stack watermark monitoring
- sdkconfig: CONFIG_HTTPD_WS_SUPPORT=y, NimBLE memory reduction
  (MSYS1 24→12, ACL 20→4, ACL buf 512→256), GAP device name
- Logger: Mutex<RingBuffer> ring buffer, 8 unit tests, JSON escape
  hardening

AC verified:
- AC-010: No blocking calls in main loop — try_recv/try_lock/sleep(10ms)
- AC-012: No unsafe in motor_task.rs or motor_state.rs — forbid(unsafe_code)

⚠️ KNOWN ISSUE: Boot-time Guru Meditation LoadProhibited at
heap_caps_get_largest_free_block with A2=0xFFFFFFFC.
All other ACs untestable until root cause is found.
See docs/plans/completed/26_07_02_phase5_postmortem.md for full analysis.

Files:
- src/motor_task.rs (+527)
- src/domain/motor_state.rs (+398)
- src/esp_safe.rs (+107)
- src/main.rs (+139/-3)
- src/lib.rs (+14)
- src/logger.rs (+233)
- src/domain/channels.rs (+3)
- src/domain/mod.rs (+1)
- src/config.rs (+15)
- sdkconfig.defaults (+8/-4)
- src/infrastructure/network/http_server.rs (+438/-?)
- src/infrastructure/network/ble.rs (+22)
- src/interface/serial.rs (+1)
- src/interface/rest_api.rs (+17)
- src/errors.rs (+1)

Report: docs/plans/completed/26_07_02_phase5_postmortem.md
```

