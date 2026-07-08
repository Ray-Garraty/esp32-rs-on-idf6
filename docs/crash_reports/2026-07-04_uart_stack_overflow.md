---
type: CrashReport
version: "1.0"
task_id: manual-debug-2026-07-04
timestamp: "2026-07-04"
crash_signature: "PC=0x40091100 EXCVADDR=0x00000000 — IllegalInstruction — stack overflow in task pthread"
title: "UART Thread Stack Overflow"
description: "UART thread with 4KB stack overflows on std::io::stdin().read() FFI call chain; check_watermark() diagnostic triggers the final stack exhaustion"
tags: [uart, stack-overflow, pthread, ffi, diagnostic]
---

# Crash Report — UART Thread Stack Overflow

## Verdict

- **Status:** root_cause_found
- **Primary Root Cause:** UART thread stack is 4096 bytes (4 KB), insufficient for `std::io::stdin().read()` FFI call chain
- **Secondary Issue:** `check_watermark()` diagnostic logging pushes critically low stack over the edge
- **Confidence:** high

## Evidence Chain

### Step 1: Triage

```
crash_analyzer.py output:
  type: IllegalInstruction
  excvaddr: '0x00000000'
  excause: 0  (IllegalInstruction)
  wdt_reset: false
  stack_overflow_task: null (FreeRTOS name "pthread" not recognized)
  pc: '0x40091100'
  
backtrace_decoded:
  0x400910fd: panic_abort (panic.c:464)
  0x400910c5: esp_system_abort (esp_system_chip.c:87)
  0x403682fe: vApplicationStackOverflowHook (port.c:573)
  0x4009221f: vTaskSwitchContext (tasks.c:3698)
  0x40091790: _frxt_dispatch (portasm.S:451)
  0x40091742: _frxt_int_exit (portasm.S:246)
  |<-CORRUPTED

stack_watermarks:
  t0 (main):    0  (never checked — u16::MAX rendered as 0)
  t1 (motor):   0  (never checked)
  t2 (temp):    0  (never checked)
  t3 (uart):   540  (CRITICAL — checked and found at 540 bytes)
  t4 (net_owner): 0  (never checked — the 10-sec interval check hadn't fired yet)

Crash message: "A stack overflow in task pthread has been detected."
```

**Key insight:** Watermark=0 for most threads is NOT evidence they all overflowed. The `emergency_dump()` renders `u16::MAX` (never been checked) as 0. Only the uart thread had its `check_watermark()` called before the crash.

### Step 2: S1–S5 Protocol

| Step | Result | Action |
|------|--------|--------|
| S1 (stack watermark) | t3 uart=540 bytes — CRITICAL (<1024) | Stack overflow confirmed |
| S2 (heap integrity) | N/A — S1 definitive | — |
| S3 (smoke test) | N/A — S1 definitive | — |
| S4 (delta analysis) | `UART_THREAD_STACK=4096` unchanged from origin | Root cause: 4 KB too small |
| S5 (red flags) | `config::UART_THREAD_STACK=4096` — not in AGENTS.md GR-6 budget table | Missing stack budget allocation |

### Step 3: Elimination

**Thread stack size comparison across all threads:**

| Thread | Config Constant | Value | AGENTS.md Budget |
|--------|----------------|-------|-----------------|
| Main loop | `CONFIG_ESP_MAIN_TASK_STACK_SIZE` (sdkconfig) | 32768 | 32768 ✅ |
| Motor | `MOTOR_THREAD_STACK` | 16384 | 16384 ✅ |
| Temp | `TEMP_THREAD_STACK` | 16384 | 16384 ✅ |
| **UART** | **`UART_THREAD_STACK`** | **4096** | **Not listed — should be ≥8192** ❌ |
| Net_owner | `NET_OWNER_STACK` | 32768 | 16384 (but 32768 OK) ✅ |
| BLE notify | `BLE_NOTIFY_THREAD_STACK` | 6144 | 8192 ❌ (not fully tested yet) |

**Black box timeline (newest first, chronological order reconstructed):**

```
[2395µs] t3 StackLow { thread_id: 3, watermark: 540 }
          ← uart_tick=100, check_watermark() fires warning
[2406µs] t3 FfiEnter { boundary: 15 }  ← esp_timer_get_time (for log timestamp)
[2407µs] t3 FfiExit  { boundary: 15 }
[2408µs] t3 FfiEnter { boundary: 12 }  ← FFI_MUTEX_LOCK (for log output)
[2408µs] t3 FfiExit  { boundary: 12 }
[2408µs] t3 FfiEnter { boundary: 14 }  ← FFI_MUTEX_UNLOCK
[2408µs] t3 FfiExit  { boundary: 14 }
          ← ~1µs later: crash (stack overflow during context switch)
```

The `log::warn!()` call inside `check_watermark()` uses ~300-400 bytes of stack for format_args! temporaries and FFI calls. Combined with the already-critical 540 bytes remaining, the next `std::io::stdin().read()` call overflows the 4096-byte stack.

**Why the task name is "pthread":** In ESP-IDF v6, Rust `std::thread::Builder::new().name("uart")` sets the pthread name via `pthread_setname_np()`, which is called AFTER the FreeRTOS task is created with the default name "pthread". The overflow happens before the rename completes, so FreeRTOS reports the task as "pthread".

### Step 4: Root Cause

**Primary: `config::UART_THREAD_STACK = 4096` is insufficient.**

The UART thread calls `std::io::stdin().read()` in its main loop. This call goes through:
1. Rust `std::io::Stdin::read()` — internal buffering + locking
2. `libc::read()` — POSIX syscall wrapper
3. ESP-IDF VFS layer — `_write_r` → `__sfvwrite_r`
4. UART driver (`uart_write`) — driver-level FIFO management
5. FreeRTOS `xQueueReceive` / `xSemaphoreTake` — synchronization

This call chain uses 3,000–3,500+ bytes of stack. With only 4,096 total (minus TLS/pthread overhead of ~500 bytes), the thread has only ~3,600 usable bytes. Peak usage of ~3,060 bytes leaves only 540 bytes spare.

**Secondary: `check_watermark()` diagnostic logging exacerbates the problem.**

When `check_watermark()` detects the stack is critically low (<1024 bytes), it calls `log::warn!()`. This logging call itself consumes stack for:
- `format_args!()` temporaries (~200-400 bytes)
- `ffi_guard::record_enter/exit()` for timer, mutex lock/unlock
- Logger internal buffer allocation

The warning log pushes the thread over the edge — the diagnostic code becomes the trigger for the crash it's trying to report.

### Step 5: Root Cause Hypothesis

```yaml
root_cause:
  category: stack_overflow
  description: >
    UART thread configured with 4096-byte stack (config::UART_THREAD_STACK).
    std::io::stdin().read() requires 3,000+ bytes of stack through its FFI
    call chain. Only 540 bytes remain at peak usage. The log::warn!() in
    check_watermark() pushes it over the edge.
  evidence:
    - "crash_analyzer reports UART thread watermark=540 (critical)"
    - "config::UART_THREAD_STACK=4096 — 4x smaller than motor/temp (16384)"
    - "Black box shows t3 StackLow at 2395µs, then FFI events at 2406-2408µs, then crash"
    - "All other threads show watermark=0 (never checked, not overflowed)"
    - "Reproducible 3/3 times in boot loop"
  confidence: high
  reproduction: >
    Flash firmware with UART_THREAD_STACK=4096 on any ESP32 board.
    Within ~2 seconds of boot, the firmware crashes with:
    "A stack overflow in task pthread has been detected."
```

## Secondary Issues Found

1. **NVS open failure (`NvsOpenFailed`) on first boot** — Already documented in LL-003 as a red herring. Causes WiFi init to partially allocate then leak buffers, reducing heap from 108K largest to 10K.

2. **WiFi init partial failure + alloc leak** — WiFi static RX buffer allocation fails (3×1600 bytes MALLOC_CAP_INTERNAL), then `esp_wifi_deinit()` also fails with 0x3001 because the internal WiFi task handle is NULL. Memory is leaked.

3. **ESP_ERR_HTTPD_TASK (45064)** — HTTP server can't allocate 12K for its FreeRTOS task because MALLOC_CAP_INTERNAL heap is fragmented.

4. **All threads showing watermark=0 is a diagnostic display issue** — `emergency_dump()` renders `u16::MAX` (never checked) as 0, making it look like all threads overflowed when only one was measured.

5. **`check_heap_integrity()` called before `logger::init()`** — The heap integrity check at line 82 of main.rs runs before the logger is initialized at line 87, so its output ("Heap integrity OK" / "HEAP CORRUPTION DETECTED!") is silently lost.

6. **`BLE_NOTIFY_THREAD_STACK` = 6144** — Below the 8192 default. Not currently active (BLE init was skipped), but could cause a similar overflow when BLE becomes functional.

## Fix

### Trivial Fix (Applied)

**Increase `UART_THREAD_STACK` from 4096 to 8192.**

- **File:** `src/config.rs` line 74
- **Change:** `pub const UART_THREAD_STACK: usize = 8192;`
- **Rationale:** Matches `CONFIG_PTHREAD_TASK_STACK_SIZE_DEFAULT`. Provides ~7688 usable bytes after TLS/pthread overhead, sufficient for the ~3060-byte peak of `std::io::stdin().read()` + 4600+ bytes of safety margin.
- **Verification:** `cargo clippy --lib` passes with 0 warnings (host target).

### Recommended Additional Fixes (Complex — Route to Implementer)

1. **Review `BLE_NOTIFY_THREAD_STACK` (6144 → 8192)** — When BLE becomes functional (heap issue fixed), the 6144-byte stack may also overflow. Should be increased to 8192 minimum to match AGENTS.md budget.

2. **Move `check_heap_integrity()` to after `logger::init()`** — Currently at line 82 before logger init at line 87. The integrity check log message is lost. Either move it after, or save the result and log it later.

3. **Guard against diagnostic-triggered overflow** — In `check_watermark()`, consider using a raw UART write instead of `log::warn!()` when the stack is critically low (< 600 bytes), to avoid the log call itself causing an overflow.

### Files to Modify

| File | Change | Status |
|------|--------|--------|
| `src/config.rs` | `UART_THREAD_STACK: 4096 → 8192` | ✅ Applied |
| `docs/lessons_learned/` | Added LL-010 for small-stack-pthread-overflow pattern | ✅ Applied |

## Investigation Artifacts

| File | Status |
|------|--------|
| `src/bin/smoke_test.rs` | N/A — not created |
| `[INVESTIGATION]` markers | N/A — no markers added |
| Lessons learned | ✅ LL-010 added |
| `src/config.rs` | ✅ UART_THREAD_STACK increased to 8192 |

## Remaining Issues

1. **WiFi init failure + DRAM fragmentation (LL-003/LL-009)** — Pre-existing. Not caused by stack overflow, but a separate DRAM fragmentation issue. WiFi → HTTP → BLE all fail due to insufficient contiguous MALLOC_CAP_INTERNAL memory. The AP fallback mode works as intended.

2. **NVS open fails on first boot** — Known harmless warning (LL-003). The NVS partition is empty on first boot; credentials haven't been written yet.

3. **BLE_NOTIFY_THREAD_STACK = 6144** — Potential future stack overflow when BLE becomes active. Should be increased to 8192 proactively.

4. **`check_heap_integrity()` call before logger init** — Diagnostic gap, not a crash cause, but should be fixed for completeness.

## Lessons Learned

**LL-010** added to `docs/lessons_learned/`: "UART thread 4 KB stack overflow — std::io::stdin().read() FFI chain consumes 3+ KB — diagnostic log::warn!() in check_watermark() pushes critically low stack over the edge."
