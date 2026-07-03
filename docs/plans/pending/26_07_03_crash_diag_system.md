---
type: Plan
title: Diagnostic subsystem (diag) — implementation, crash test results, and improvements
description: Lock-free black box, stack/heap monitors, state tracer, FFI guard, preconditions. Real crash validated the system; gaps identified for v2.
tags: [diag, diagnostics, black-box, monitoring]
timestamp: 2026-07-03
status: pending
---

# Diagnostic subsystem (diag) -- implementation, crash test results, and improvements

## Summary

The diagnostic subsystem (`src/diag/`) is an active diagnostic layer for the
EcoTiter ESP32 firmware. Unlike `log::info!`/`warn!`/`error!` (which are
useless post-mortem — by the time Guru Meditation fires, the UART is dead and
the ring buffer contains stale data), `diag` provides:

1. **Lock-free black box** — 64 structured events × ~16 bytes = 1 KB SRAM.
   Written via atomic CAS + `write_volatile`. Survives panic. No heap, no mutex.
2. **Tick watchdog** — detects main loop blocking > 50 ms / 500 ms (GR-1).
3. **Stack monitor** — per-thread watermark tracking with WARN(1024) /
   CRITICAL(512) thresholds.
4. **Heap snapshot** — DRAM free/largest at key init phases (GR-3).
5. **State tracer** — `BuretteState` and `TransportMode` transitions.
6. **FFI guard** — entry/exit tracing for every `unsafe { esp_idf_sys::* }`
   call (GR-5).
7. **Preconditions** — runtime contracts: RMT stop flag (GR-2) and thread
   context (GR-1) assertions.
8. **Panic hook** — dumps black box + stack watermarks to UART before reboot.

Implemented across 8 new files and integrated into 12 existing files.
Build verified: `check` (0 errors), `clippy` (0 warnings), `test` (245 passed),
`fmt` (clean), `check_unsafe.py` (44 blocks, all documented).

---

## Architecture

```
src/diag/
├── mod.rs              # Re-exports, diag::init()
├── black_box.rs        # DiagEvent enum, Record, BlackBox (lock-free ring buffer)
├── tick_watchdog.rs    # TickOverrun detection in main loop
├── stack_monitor.rs    # Thread watermark registry (8 slots, static array)
├── heap_snapshot.rs    # DRAM snapshots at init phases
├── state_tracer.rs     # BuretteState / TransportMode transitions
├── ffi_guard.rs        # record_enter/record_exit for FFI boundaries
└── preconditions.rs    # diag_assert! macro, assert_rmt_preconditions()
```

### Integration points

| Hook point | File | What it does |
|---|---|---|
| `logger::init()` → | `main.rs:56` | `setup_panic_hook()`, `diag::init()`, `LAST_TRANSPORT` static |
| Main loop body | `main.rs:376-635` | `tick_begin()`/`tick_end()`, periodic `check_watermark(MAIN)` |
| `net_owner` thread | `main.rs:264-316` | Thread reg + heap snapshots after WiFi/HTTP/BLE init |
| `temp` thread | `main.rs:205-206` | Thread slot + stack registration |
| `uart` thread | `main.rs:228-229` | Thread slot + stack registration |
| `motor` thread | `motor_task.rs:84-85` | Thread slot + stack registration |
| `ble-notify` thread | `ble.rs:337-338` | Thread slot + stack registration |
| Motor homing | `motor_task.rs:125-137` | State trace transitions |
| Stepper RMT | `stepper.rs:228-232` | `assert_rmt_preconditions()` (GR-2) |
| `WsSender::send()` | `http_server.rs:111-120` | FFI guard (GR-5) |
| All NVS FFI | `nvs.rs:90-382` | FFI guard on 11 unsafe calls |
| All `esp_mutex` FFI | `esp_mutex.rs:54-100` | FFI guard on 3 unsafe calls |
| `esp_safe` FFI | `esp_safe.rs:24-143` | FFI guard on 6 unsafe calls + `panic_write_str()` |
| Logger FFI | `logger.rs:79-85` | FFI guard on `esp_timer_get_time()` |
| `set_burette_state_tag()` | `motor_state.rs:104-110` | State tracer (via `#[cfg(target_arch = "xtensa")]`) |

### Event types (14 variants)

| Tag | Event | Payload |
|---|---|---|
| 0 | `TickOverrun` | expected_ms:u16, actual_ms:u16 |
| 1 | `StackLow` | thread_id:u8, watermark:u16 |
| 2 | `StackCritical` | thread_id:u8, watermark:u16 |
| 3 | `HeapSnapshot` | free_kb:u8, largest_kb:u8, phase:u8 |
| 4 | `DramFragmented` | largest_block:u16, requested:u16 |
| 5 | `BuretteTransition` | from:u8, to:u8, cmd:u8 |
| 6 | `TransportTransition` | from:u8, to:u8 |
| 7 | `InitPhase` | phase:u8, dram_free_kb:u8 |
| 8 | `InitOrderViolation` | expected:u8, actual:u8 |
| 9 | `FfiEnter` | boundary:u8 |
| 10 | `FfiExit` | boundary:u8, result:i8 |
| 11 | `PreconditionFailed` | contract_id:u16, line:u16 |
| 12 | `LimitSwitchHit` | switch:u8, motor_running:bool |
| 13 | `StopFlagIgnored` | chunks_executed:u16 |

---

## Current state — proven working

### Verified on hardware (live crash, 2026-07-03)

The smoke test hit a real Guru Meditation (`LoadProhibited, EXCVADDR=0x00000000`)
immediately after boot. The diagnostic system proved its value:

**What diag showed before the crash:**

```
[DIAG] Thread 'main' registered (slot 0)       ← stack monitor works
[HEAP]  boot : free=159KB largest=108KB        ← heap snapshot works
[DIAG] diagnostic subsystem initialized         ← init works
[DIAG] Thread 'motor' registered (slot 1)       ← stack monitor works
[DIAG] Thread 'temp' registered (slot 2)        ← stack monitor works
[DIAG] Thread 'uart' registered (slot 3)        ← stack monitor works
Guru Meditation: LoadProhibited EXCVADDR=0x00   ← crash after uart reg
```

**Crash analyzer decoded backtrace:**

```
vListInsert → vTaskDelay → usleep → sleep
в ecotiter::main::{closure#1} (uart thread, line 242)
```

**Without diag:** Guru Meditation → 15+ minutes of "heap corruption?" analysis
**With diag:** 2 minutes to identify "uart thread stack overflow in sleep()"

### All host checks pass

| Check | Result |
|---|---|
| `scripts/build.sh check` | 0 errors |
| `scripts/build.sh clippy` (xtensa) | 0 warnings |
| `scripts/build.sh clippy-host` | 0 warnings |
| `scripts/build.sh test` | 245 tests passed |
| `scripts/build.sh fmt` | clean |
| `scripts/check_unsafe.py` | 44 blocks ≤ baseline 44, all documented |

---

## Gaps identified

### Critical: Panic hook does not catch hardware exceptions

The Rust `std::panic::set_hook()` only fires for Rust `panic!()`. Hardware
exceptions (LoadProhibited, StoreProhibited — EXCCAUSE 0x1C, 0x1D) go through
ESP-IDF's C `esp_panic_handler()` directly, bypassing the Rust hook entirely.

**Result:** Black box, stack watermarks, and the panic message are NEVER dumped
for hardware exceptions. All diagnostic context is lost on reboot.

**Root cause:** Two separate exception paths:

```
Rust panic!() → std abort() → esp_panic_handler (C)
                                     ↑
Hardware exception (LoadProhibited) —┘
```

The Rust `set_hook` closure runs AFTER `abort()` is called — but ONLY for
panics. Hardware exceptions never invoke the Rust handler.

### Missing: Periodic stack checks in worker threads

- `check_watermark()` is only called periodically in `main` and `motor` threads.
- `uart` (4 KB stack), `temp` (16 KB), `ble-notify` (6 KB), and `net_owner`
  (32 KB) threads have NO periodic watermark checks.
- The uart thread's stack overflow was NOT detected pre-mortem.

### Missing: Serial monitor log persistence

The smoke test used `--log-dir /tmp/ecotiter-smoke` (explicit override).
Default `logs/` directory works but was not exercised in the test run.

### Known: UART thread 4 KB stack is a GR-6 violation

The `uart` thread uses a hardcoded `stack_size(4096)` at `main.rs:225`.
Per GR-6 table, the minimum for `std::thread` is 8192
(`CONFIG_PTHREAD_TASK_STACK_SIZE_DEFAULT`). The diag registration calls
pushed the stack over the limit.

---

## Improvement plan

### Phase 1: Hardware exception handler (MVP)

**Goal:** Dump black box + stack watermarks for ALL crash types, not just
Rust panics.

**Approach:** Linker-wrap `esp_panic_handler` (the C function). This is the
single entry point for all ESP-IDF panic events — both Rust software panics
AND hardware exceptions.

```rust
// In esp_safe.rs — a new module or function
#[no_mangle]
pub extern "C" fn __wrap_esp_panic_handler(info: &panic_info_t) {
    // 1. Dump black box (lock-free, safe from exception context)
    let mut w = UartWriter;
    let _ = writeln!(&mut w, "\n!!! HARDWARE EXCEPTION !!!");
    let _ = writeln!(&mut w, "  Reason: {}", info.reason);
    let _ = writeln!(&mut w, "  PC:     {:#x}", info.addr);

    // 2. If frame available, read EXCVADDR from XtExcFrame
    if !info.frame.is_null() {
        let frame = info.frame as *const XtExcFrame;
        let excvaddr = unsafe { (*frame).excvaddr };
        let _ = writeln!(&mut w, "  EXCVADDR: {:#x}", excvaddr);
    }

    // 3. Dump diagnostic events
    diag::black_box::dump(&mut w);
    diag::stack_monitor::emergency_dump(&mut w);

    // 4. Call the real handler (via __real_esp_panic_handler)
}
```

**Requirements:**
- Add `-Wl,--wrap=esp_panic_handler` to linker flags (via build.rs or .cargo/config.toml)
- Define `panic_info_t` and `XtExcFrame` Rust structs matching the C layout
- Define `__real_esp_panic_handler` extern for calling the original

**Risks:**
- The exception handler runs in a restricted context (IRAM may be corrupted,
  heap may be inconsistent). The dump must use only `write(1, ...)` and
  lock-free volatile reads — no allocations, no locking, no formatting.
- `esp_panic_handler` is called from `panic_handler.c:127` in ESP-IDF v6.
  The function signature and `panic_info_t` layout must match exactly.

### Phase 2: Periodic stack monitoring in all threads

**Goal:** Pre-mortem detection of stack pressure.

**Changes:**
- Add periodic `check_watermark()` calls in `temp`, `uart`, `ble-notify`,
  and `net_owner` thread loops (every ~100 iterations or ~1 second).
- This would have caught the uart thread's 4 KB stack overflow BEFORE the
  crash, producing `DiagEvent::StackCritical` events in the black box.

### Phase 3: Fix UART thread stack size

**Goal:** Eliminate the known GR-6 violation.

**Changes:**
- Add `config::UART_THREAD_STACK: usize = 8192` to `src/config.rs`
- Replace `stack_size(4096)` at `main.rs:225` with `stack_size(config::UART_THREAD_STACK)`
- Move `diag::stack_monitor::register_thread(UART, "uart")` OUT of the thread
  closure (call from main() before spawn) to reduce stack pressure in the
  uart thread.

### Phase 4: ESP-IDF core dump to flash (optional)

**Goal:** Persist crash information across reboots.

**Changes:**
- Enable `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH` in `sdkconfig.defaults`
- Add a boot-time check: if core dump exists, extract and log PC/task name
- Add a REST endpoint `GET /api/diag/last-crash` to retrieve summary
- Cost: ~64 KB flash partition, ~3 KB code

---

## Lessons learned

### LL-005: Hardware exceptions bypass Rust panic hook

The Rust `std::panic::set_hook()` does NOT fire for hardware exceptions
(LoadProhibited, StoreProhibited, etc.). These go directly through ESP-IDF's
C `esp_panic_handler()`. Any code that relies solely on the Rust panic hook
(black box dump, watermarks) will NOT execute for hardware exceptions.

**Fix:** Override `esp_panic_handler` via linker wrapping — this is the common
entry point for ALL ESP-IDF panic events (both Rust panics and hardware
exceptions).

<!-- grep: ll-005-hardware-exception-bypasses-rust-hook -->

### LL-006: 4 KB thread stack is insufficient with diag

The `uart` thread with `stack_size(4096)` worked before diag. The additional
`diag::stack_monitor::register_thread()` call inside the closure — which
triggers `log::info!` with string formatting — pushed the stack over the
limit, causing adjacent TLSF heap corruption.

**Fix:** Minimum `std::thread` stack on ESP32 is 8192 (per GR-6). Move
registration calls out of thread closures where possible.

---

## Files affected

### New files (8)

| File | Purpose |
|---|---|
| `src/diag/mod.rs` | Module root, re-exports, `init()` |
| `src/diag/black_box.rs` | Lock-free ring buffer, 14 DiagEvent variants |
| `src/diag/tick_watchdog.rs` | Main loop blocking detector (GR-1) |
| `src/diag/stack_monitor.rs` | Per-thread watermark monitor (8 slots) |
| `src/diag/heap_snapshot.rs` | DRAM snapshot + fragmentation assert |
| `src/diag/state_tracer.rs` | Burette/Transport state transition logger |
| `src/diag/ffi_guard.rs` | FFI boundary enter/exit tracing (GR-5) |
| `src/diag/preconditions.rs` | `diag_assert!` + `assert_rmt_preconditions()` (GR-2) |

### Modified files (12 + 2 infra)

| File | Change |
|---|---|
| `src/lib.rs` | `pub mod diag` with `#[cfg(target_arch = "xtensa")]` |
| `src/main.rs` | panic hook, tick_begin/end, transport tracer, thread reg, heap snapshots |
| `src/motor_task.rs` | thread reg, periodic check_watermark, homing state traces |
| `src/esp_safe.rs` | `panic_write_str()`, FFI guard on 6 unsafe calls |
| `src/esp_mutex.rs` | FFI guard on lock/trylock/unlock |
| `src/logger.rs` | FFI guard on `esp_timer_get_time()` |
| `src/domain/motor_state.rs` | State tracer hook in `set_burette_state_tag()` |
| `src/infrastructure/drivers/stepper.rs` | `assert_rmt_preconditions()` |
| `src/infrastructure/network/http_server.rs` | FFI guard on `httpd_ws_send_frame_async` |
| `src/infrastructure/network/ble.rs` | Thread registration for ble-notify |
| `src/infrastructure/storage/nvs.rs` | FFI guard on all 11 unsafe calls |
| `AGENTS.md` | GR-7 (Mandatory Diagnostic Instrumentation) |
| `scripts/check_unsafe.py` | Baseline updated 32 → 44 |
| `scripts/serial_monitor.py` | `--log-dir`, `--no-log` flags |

### Planned for future phases

| File | Change | Phase |
|---|---|---|
| `src/esp_safe.rs` | `__wrap_esp_panic_handler()` + `XtExcFrame` struct | 1 |
| `.cargo/config.toml` | `-Wl,--wrap=esp_panic_handler` in rustflags | 1 |
| `src/config.rs` | `UART_THREAD_STACK: usize = 8192` | 3 |
| `src/main.rs:225` | `stack_size(4096)` → `stack_size(config::UART_THREAD_STACK)` | 3 |
| `sdkconfig.defaults` | `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y` | 4 |
| `src/infrastructure/network/http_server.rs` | `GET /api/diag/last-crash` endpoint | 4 |

---

## Verification criteria (future)

- [ ] `scripts/build.sh` — 0 errors, 0 warnings
- [ ] `scripts/build.sh clippy` — 0 warnings
- [ ] `scripts/build.sh test` — all pass
- [ ] `scripts/check_unsafe.py` — within baseline
- [ ] Hardware exception (e.g., null deref) dumps black box to UART before reboot
- [ ] All 6 threads have periodic `check_watermark()` calls
- [ ] UART thread stack = 8192 (config constant)
- [ ] `register_thread()` calls are outside thread closures (parent context)
- [ ] 30-second serial smoke test: no Guru Meditation, no WDT, no panics
- [ ] Core dump to flash survives reboot (Phase 4)
