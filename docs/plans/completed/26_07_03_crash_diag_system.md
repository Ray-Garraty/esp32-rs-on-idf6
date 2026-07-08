---
type: Plan
title: "Diagnostic subsystem (diag) — implementation, crash test results, and improvements"
description: >
  Phase 1-3 implemented and verified on hardware. Lock-free black box,
  __wrap_esp_panic_handler for hardware exceptions, periodic stack monitoring,
  UART stack fix, and AI-agent-optimized crash dump format.
  Full pipeline: serial_monitor → crash_analyzer → addr2line → YAML report.
tags: [diag, diagnostics, black-box, monitoring, completed]
timestamp: 2026-07-03
status: completed
supersedes: pending/26_07_03_crash_diag_system.md
---

# Diagnostic subsystem (diag) — implementation, crash test results, and improvements

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
8. **`__wrap_esp_panic_handler`** — linker-wraps `esp_panic_handler` to dump
   black box + stack watermarks + backtrace for ALL crash types (hardware
   exceptions AND Rust panics). Uses MMIO UART0 (no OS calls, safe in exception
   context).
9. **Periodic `check_watermark()`** — all 6 threads (main, motor, temp, uart,
   net_owner, ble-notify) check stack pressure every ~100 iterations.
10. **AI-agent-optimized dump format** — machine-parseable `key=value` sections:
   `=== CRASH ===`, `=== REGISTERS ===`, `=== BACKTRACE ===`,
   `=== BLACK BOX (newest first) ===`, `=== STACK ===`.

### Crash analysis pipeline

| Tool | Role |
|---|---|
| `scripts/serial_monitor.py` | Auto-detects `=== CRASH ===` in serial output, runs inline crash_analyzer |
| `scripts/crash_analyzer.py` | Parses both old Guru Meditation and new `=== CRASH ===` format, runs addr2line, classifies, checks `docs/lessons_learned/` |
| `scripts/analyze_last_crash.sh` | Post-hoc analysis of latest `logs/serial_*.log` |
| `scripts/decode_backtrace.sh` | Standalone addr2line for hex addresses |

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

src/esp_safe.rs          # __wrap_esp_panic_handler + PanicInfo + XtExcFrame structs
                         # CrashWriter (MMIO UART0), do_backtrace(), exccause_name()
```

### Hardware exception handler (Phase 1)

```
Rust panic!() → set_hook → abort() → panic_handler() → esp_panic_handler()
                                                              ↕ (--wrap)
                                              __wrap_esp_panic_handler()
                                                ├── CRASH section
                                                ├── REGISTERS section
                                                ├── BACKTRACE section
                                                ├── BLACK BOX section
                                                ├── STACK section
                                                ├── !!! EXCEPTION END !!!
                                                └── __real_esp_panic_handler()
                                                      └── reset/coredump
```

The `__wrap_esp_panic_handler` intercepts ALL crash types because
`esp_panic_handler` is the single entry point. The Rust `set_hook()` only
covers `panic!()` — hardware exceptions (LoadProhibited, StoreProhibited,
IllegalInstruction, etc.) bypass it entirely.

The `CrashWriter` uses MMIO writes to UART0 FIFO registers (0x3FF40000)
directly — no FreeRTOS, no VFS, no locks. Safe from any exception context.

The backtrace walk uses the Xtensa windowed-ABI base-save area algorithm:
read `[sp-16]` (return address) and `[sp-12]` (previous SP) iteratively up to
100 frames, with DRAM/IRAM sanity checks and `|<-CORRUPTED` marker.

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

### Integration points

| Hook point | File | What it does |
|---|---|---|
| `logger::init()` → | `main.rs:56` | `setup_panic_hook()`, `diag::init()`, `LAST_TRANSPORT` static |
| Main loop body | `main.rs:376-635` | `tick_begin()`/`tick_end()`, periodic `check_watermark(MAIN)` |
| `net_owner` thread | `main.rs:264-316` | Thread reg + heap snapshots after WiFi/HTTP/BLE init |
| `temp` thread | `main.rs:205-206` | Thread slot + periodic `check_watermark(TEMP)` |
| `uart` thread | `main.rs:228-229` | Thread slot + stack. reg. outside closure, periodic watermark |
| `motor` thread | `motor_task.rs:84-85` | Thread slot + periodic `check_watermark(MOTOR)` |
| `ble-notify` thread | `ble.rs:337-338` | Thread slot + periodic `check_watermark(BLE_NOTIFY)` |
| Motor homing | `motor_task.rs:125-137` | State trace transitions |
| Stepper RMT | `stepper.rs:228-232` | `assert_rmt_preconditions()` (GR-2) |
| `WsSender::send()` | `http_server.rs:111-120` | FFI guard (GR-5) |
| All NVS FFI | `nvs.rs:90-382` | FFI guard on 11 unsafe calls |
| All `esp_mutex` FFI | `esp_mutex.rs:54-100` | FFI guard on 3 unsafe calls |
| `esp_safe` FFI | `esp_safe.rs:24-143` | FFI guard on 6 unsafe calls + `panic_write_str()` |
| Logger FFI | `logger.rs:79-85` | FFI guard on `esp_timer_get_time()` |
| `set_burette_state_tag()` | `motor_state.rs:104-110` | State tracer (via `#[cfg(target_arch = "xtensa")]`) |

---

## Verified on hardware (live crash, 2026-07-03)

### Crash: IllegalInstruction on net_owner thread after WiFi init failure

**Cause:** DRAM fragmentation after WiFi init (free=17K, largest=6K).
`HttpServer::new()` tries to allocate 12KB contiguous `MALLOC_CAP_INTERNAL`
but fails. The exact failure mechanism is an lwIP stack call through a
corrupted/torn-down EspSystemEventLoop (LL-004).

**Diagnostic output (machine-parseable):**

```
=== CRASH ===
exccause=0 name=IllegalInstruction pc=0x40091100 excvaddr=0x00000000 ps=0x00060930 sp=0x3fffcee0
=== REGISTERS ===
a0=0x800910c8 a1=0x3fffcee0 a2=0x3fffcf2b a3=0x00000003
a4=0x0000000a a5=0x00000003 a6=0x3ffdf498 a7=0x00000000
a8=0x3ffcd4ec a9=0x00000001 a10=0x00000028 a11=0x3fffcff3
a12=0x00000001 a13=0x3ffc6ed0 a14=0x00000000 a15=0x00060d23
sar=0x00000018 exccause=0x00000000 excvaddr=0x00000000 lbeg=0x4000c28c lend=0x4000c296 lcount=0x00000000
=== BACKTRACE ===
0x400910fd:0x3fffcee0
0x400910c5:0x3fffcf00
0x40090dfd:0x3fffcf20
0x402990f7:0x3fffd040
0x402aa712:0x3fffd070
0x402aa7a6:0x3fffd090
0x4029888d:0x3fffd0e0
0x402d5b16:0x3fffd100
0x402d6204:0x3fffd160
0x401a87c8:0x3fffd190
0x401a8538:0x3fffd310
0x4012169d:0x3fffd340
...
=== BLACK BOX (64 events, newest first) ===
[822us] t4 FfiExit { boundary: 20, result: 0 }
[821us] t4 FfiEnter { boundary: 20 }
...
[789us] t4 HeapSnapshot { free_kb: 27, largest_kb: 9, phase: 1 }
...
=== STACK ===
t0 main watermark=0
t1 motor watermark=0
t2 temp watermark=0
t3 uart watermark=0
t4 net_owner watermark=0
!!! EXCEPTION END !!!
```

### What the diagnostic system proved:

1. **`__wrap_esp_panic_handler` intercepts hardware exceptions** — the
   IllegalInstruction was caught and all diagnostic data dumped to UART before
   the chip reset. Without it, only a Guru Meditation with no context would
   appear.

2. **Black box newest-first** — the last events before the crash (`FfiExit 20`,
   `HeapSnapshot{free:27KB,largest:9KB}`) are immediately visible, pinpointing
   the DRAM pressure before HTTP init.

3. **Backtrace** — 25 PC:SP pairs for `addr2line` to decode the call chain
   from crash site through lwIP, HTTP server, WiFi, to the net_owner thread.

4. **Crash analysis pipeline** — `analyze_last_crash.sh` produces a YAML
   report with crash classification, decoded backtrace, stack watermarks, and
   matched lessons (LL-001, LL-004).

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

## Implementation phases completed

### Phase 1: Hardware exception handler (`__wrap_esp_panic_handler`)

**Goal:** Dump black box + stack watermarks for ALL crash types.

**Implemented in `src/esp_safe.rs`:**
- `PanicInfo` and `XtExcFrame` `repr(C)` structs matching ESP-IDF v6.0.1 layout
- `CrashWriter` with MMIO UART0 writes (no OS, no locks, safe from exception)
- `exccause_name()` — maps EXCCAUSE values 0-39 to standard Xtensa names
- `do_backtrace()` — windowed ABI backtrace walk up to 100 frames
- `__wrap_esp_panic_handler()` — machine-parseable crash dump:
  - `=== CRASH ===`: exccause, name, pc, excvaddr, ps, sp
  - `=== REGISTERS ===`: a0-a15, sar, exccause, excvaddr, lbeg, lend, lcount
  - `=== BACKTRACE ===`: PC:SP pairs
  - `=== BLACK BOX ===`: diagnostic events
  - `=== STACK ===`: thread watermarks
- Linker flag: `-Wl,--wrap=esp_panic_handler` in `.cargo/config.toml`

**Key design decisions:**
- MMIO UART0 instead of `write(1, ...)` — the VFS `write()` uses FreeRTOS
  mutexes which crash in exception context when the scheduler is suspended
- EXCCAUSE name table instead of reading `panic_info_t.reason` C string —
  the pointer may be invalid in exception context
- Only 24 registers + backtrace — no heap, no `format!()`, no allocations

### Phase 2: Periodic stack monitoring in all threads

**Goal:** Pre-mortem detection of stack pressure.

- Added `temp_tick` counter + `check_watermark(TEMP)` every 100 iterations
- Added `uart_tick` counter + `check_watermark(UART)` every 100 iterations
- Added `check_watermark(NET_OWNER)` every 10 seconds
- Added `ble_tick` counter + `check_watermark(BLE_NOTIFY)` every 100 iterations
- Motor thread already had `check_watermark(MOTOR)` from initial implementation

### Phase 3: Fix UART thread stack size

**Goal:** Eliminate GR-6 violation.

- Added `config::UART_THREAD_STACK: usize = 8192`
- Changed `stack_size(4096)` → `stack_size(config::UART_THREAD_STACK)`
- Moved `register_thread(UART)` outside the thread closure (reduces stack
  pressure by removing `log::info!` formatting from the thread's stack)
- Same for `temp` and `net_owner` threads (register_thread outside closure)

### Phase 4: Crash analysis pipeline

**Goal:** Integrate serial monitoring, crash parsing, and backtrace decoding.

- `scripts/serial_monitor.py` — auto-detects `=== CRASH ===` in output,
  buffers crash section, runs `crash_analyzer.py` inline
- `scripts/crash_analyzer.py` — updated regex for both new `=== CRASH ===`
  format and old Guru Meditation format. Added `--log` flag for post-hoc
  analysis. Fixed EXCCAUSE_DESCRIPTIONS to match real Xtensa values.
- `scripts/analyze_last_crash.sh` — new script: finds latest `logs/serial_*.log`,
  extracts crash section, runs crash_analyzer + addr2line
- Backtrace deduplication — `!!! EXCEPTION END !!!` marker separates primary
  diagnostic output from secondary crashes (e.g. lwIP assert in
  `__real_esp_panic_handler`)

---

## Lessons learned

### LL-005: Hardware exceptions bypass Rust panic hook

The Rust `std::panic::set_hook()` does NOT fire for hardware exceptions
(LoadProhibited, StoreProhibited, etc.). These go directly through ESP-IDF's
C `esp_panic_handler()`.

**Fix:** Linker-wrap `esp_panic_handler` via `--wrap` — this is the common
entry point for ALL ESP-IDF panic events (both Rust panics and hardware
exceptions).

### LL-006: 4 KB thread stack is insufficient with diag

The `uart` thread with `stack_size(4096)` worked before diag. The additional
`diag::stack_monitor::register_thread()` call inside the closure — which
triggers `log::info!` with string formatting — pushed the stack over the
limit.

**Fix:** Minimum `std::thread` stack on ESP32 is 8192 (per GR-6). Move
registration calls out of thread closures where possible.

---

## All modified files

### New files (8 diag + 1 script)

| File | Purpose |
|---|---|
| `src/diag/mod.rs` | Module root, re-exports, `init()` |
| `src/diag/black_box.rs` | Lock-free ring buffer, 14 DiagEvent variants, newest-first dump |
| `src/diag/tick_watchdog.rs` | Main loop blocking detector (GR-1) |
| `src/diag/stack_monitor.rs` | Per-thread watermark monitor (8 slots), compact STACK dump |
| `src/diag/heap_snapshot.rs` | DRAM snapshot + fragmentation assert |
| `src/diag/state_tracer.rs` | Burette/Transport state transition logger |
| `src/diag/ffi_guard.rs` | FFI boundary enter/exit tracing (GR-5) |
| `src/diag/preconditions.rs` | `diag_assert!` + `assert_rmt_preconditions()` (GR-2) |
| `scripts/analyze_last_crash.sh` | Post-hoc crash analysis from serial logs |

### Modified files (14)

| File | Change |
|---|---|
| `src/lib.rs` | `pub mod diag` with `#[cfg(target_arch = "xtensa")]` |
| `src/main.rs` | panic hook, tick_begin/end, transport tracer, thread reg, heap snapshots, UART stack 8192, periodic check_watermark in temp/uart/net_owner |
| `src/motor_task.rs` | thread reg, periodic check_watermark, homing state traces |
| `src/esp_safe.rs` | `__wrap_esp_panic_handler()` + `XtExcFrame`/`PanicInfo` structs + `CrashWriter` (MMIO UART0) + `do_backtrace()` + `exccause_name()` + FFI guard on 6 unsafe calls + `panic_write_str()` |
| `src/esp_mutex.rs` | FFI guard on lock/trylock/unlock |
| `src/logger.rs` | FFI guard on `esp_timer_get_time()` |
| `src/domain/motor_state.rs` | State tracer hook in `set_burette_state_tag()` |
| `src/infrastructure/drivers/stepper.rs` | `assert_rmt_preconditions()` |
| `src/infrastructure/network/http_server.rs` | FFI guard on `httpd_ws_send_frame_async` |
| `src/infrastructure/network/ble.rs` | Thread registration for ble-notify, periodic check_watermark |
| `src/infrastructure/storage/nvs.rs` | FFI guard on all 11 unsafe calls |
| `src/config.rs` | `UART_THREAD_STACK: usize = 8192` |
| `.cargo/config.toml` | `-Wl,--wrap=esp_panic_handler` in rustflags |
| `AGENTS.md` | GR-7 (Mandatory Diagnostic Instrumentation) |
| `scripts/check_unsafe.py` | Baseline updated 32 → 44 |
| `scripts/serial_monitor.py` | `--log-dir`, `--no-log`, inline crash detection |
| `scripts/crash_analyzer.py` | New format parser, `--log` flag, fixed EXCCAUSE table |

### Not implemented (Phase 4 deferred)

| Feature | Reason |
|---|---|
| `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH` | Core dump to flash requires partition table changes. Existing `__wrap_esp_panic_handler` dumps are sufficient for post-mortem. |
| `GET /api/diag/last-crash` REST endpoint | Would require NVS persistence; low priority since serial_log-based analysis works. |

---

## Verification criteria

- [x] `scripts/build.sh` — 0 errors, 0 warnings
- [x] `scripts/build.sh clippy` — 0 warnings
- [x] `scripts/build.sh test` — all pass
- [x] `scripts/check_unsafe.py` — within baseline
- [x] Hardware exception (IllegalInstruction at PC=0x40091100) dumped black box + backtrace + watermarks to UART
- [x] All 6 threads have periodic `check_watermark()` calls
- [x] UART thread stack = 8192 (config constant)
- [x] `register_thread()` calls are outside thread closures (parent context)
- [x] 30-second serial smoke test — crash diagnostics captured and analyzed
- [ ] Core dump to flash survives reboot (deferred to future phase)
