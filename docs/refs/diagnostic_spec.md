---
type: Architecture Reference
title: Diagnostic Subsystem Specification
description: Crash capture, panic handler, BlackBox, stack monitoring, core dump pipeline — architecture and known gaps
tags: [diagnostic, crash, panic, watchdog, black-box, stack-monitor, core-dump]
timestamp: 2026-07-13
---

# Diagnostic Subsystem Specification

## Overview

The diagnostic subsystem provides crash capture and pre-crash forensic data for the ecotiter firmware. It is designed as a layered pipeline:

```
Exception → panic handler → BlackBox dump → ESP-IDF real handler → core dump → monitor.py capture → crash_analyzer.py classification
```

Each layer had known gaps (documented in the Gaps section below). All 12 gaps were fixed on 2026-07-13 per the implementation plan.

### Current State (2026-07-14)

All 12 diagnostic gaps are implemented. The system now produces:
1. ✅ A complete panic header with `exccause`, `excvaddr`, `pc`, and all 16 registers
2. ✅ Diverse BlackBox events: FfiEnter/FfiExit, StateTransition, Error, TickDuration
3. ✅ Core dump captured to `dumps/*.coredump.base64` (122 KB in latest crash)
4. ✅ Correct classification — `InstrFetchProhibited` detected, `0xa5a5a5a5` canary detected

**Remaining issue:** The firmware still crashes (`InstrFetchProhibited excvaddr=0x00000000` — null pointer call) after WiFi AP + HTTP server init. This is a pre-existing bug now visible because AP/HTTP start immediately (before homing). It was previously masked because HTTP never ran before homing completed.

---

## Component Map

```
┌─────────────────────────────────────────────────────────────┐
│                    FIRMWARE (C++)                            │
│                                                             │
│  components/diag/                                           │
│  ├── crash_handler.cpp      wraps esp_panic_handler          │
│  ├── black_box.{hpp,cpp}    64-event lock-free ring buffer  │
│  ├── ffi_guard.{hpp,cpp}    RAII FfiEnter/FfiExit recorder  │
│  ├── stack_monitor.{hpp,cpp} periodic stack watermark       │
│  ├── tick_watchdog.hpp      (header-only, active)           │
│  ├── rtc_watchdog.{hpp,cpp} RWDT RAII (6s)                 │
│  ├── state_tracer.{hpp,cpp} state transitions + BlackBox    │
│  ├── heap_snapshot.{hpp,cpp} DRAM pre-check                 │
│  └── CMakeLists.txt         build defs                      │
│                                                             │
└─────────────────────────────┬───────────────────────────────┘
                              │
                              ▼ UART (115200 8N1)
┌─────────────────────────────────────────────────────────────┐
│                    PYTHON SCRIPTS                            │
│                                                             │
│  scripts/                                                    │
│  ├── monitor.py             serial capture + coredump save  │
│  └── crash_analyzer.py      log parsing + lesson matching   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## Pipeline — End-to-End

```
1. CPU exception (illegal instruction, store prohibited, etc.)
          │
2. ESP-IDF panic_handler() entry
          │  ┌─ dual-core: skips WDT feed
          │  └─ sets RWDT to 10s (overrides user's 6s)
          │
3. __wrap_esp_panic_handler()         ← crash_handler.cpp
          │  ┌─ prints "=== CRASH ==="
          │  ├─ prints BlackBox dump (64 events)
          │  ├─ prints stack watermarks
          │  └─ prints "!!! EXCEPTION END !!!"
          │
4. __real_esp_panic_handler()
          │  ┌─ prints Guru Meditation (exccause, regs, bt)
          │  ├─ triggers FreeRTOS stack overflow hook (if applicable)
          │  ├─ prints core dump header + base64 data
          │  └─ reboots
          │
5. monitor.py captures UART output
          │  ┌─ detects "=== CRASH ===" → capturing_coredump = True
          │  ├─ detects "Print core dump to uart" → re-arms capture
          │  ├─ stops capture on "Rebooting..." only
          │  ├─ base64-decodes captured data → searches for \x7fELF
          │  └─ falls back: saves raw base64 as *.coredump.base64
          │
6. crash_analyzer.py parses log
          │  ┌─ classifies by exccause, backtrace, known patterns
          │  ├─ detects 0xa5a5a5a5 canary → stack_overflow
          │  └─ matches against docs/lessons_learned/
          │
7. dumps/*.coredump saved (if step 5 succeeded)
```

---

## DRAM Allocation

| Component | RAM Type | Size |
|-----------|----------|------|
| BlackBox ring buffer | Internal DRAM | 1024 B (64 × 16 B) |
| StackMonitor thread list | Internal DRAM | ~200 B |
| RtcWatchdog object | Stack (main) | ~8 B |
| Core dump buffer (monitor.py) | Host RAM | ~256 KB peak |

No component uses PSRAM — all diagnostic data must survive crashes where PSRAM is in an unknown state.

---

## Mandatory Instrumentation Requirements (GR-7)

Every new function MUST have the appropriate instrumentation. Code without these points is INCOMPLETE.

| What | Instrumentation | Applies When |
|------|----------------|--------------|
| **FFI boundary** | `FfiGuard guard(boundary_id)` — RAII wrapper | Every Rust/C++ FFI call |
| **RMT motion** | `assert_rmt_preconditions()` before `move_steps_intervals()` | Every RMT motion start |
| **New thread** | `StackMonitor::registerThread(name, stack_size)` | Every `xTaskCreate` call |
| **State transition** | `StateTracer::logBuretteTransition(old, new)` + `BlackBox::record()` | Every burette state change |
| **Large alloc (>4 KB)** | `HeapSnapshot::assertCanAllocate(size)` before allocation | Every `malloc`/`new`/`heap_caps_malloc` > 4 KB |
| **Main loop iteration** | `TickWatchdog watchdog` — RAII at top of main loop | Every `app_main` iteration |

### Instrumentation Points Map

```
Location                          Instrumentation
────────────────────────────────────────────────────────────────
app_main loop body                TickWatchdog watchdog;
FFI call                          auto guard = FfiGuard(boundary_id);
motor_task RMT start              assert_rmt_preconditions();
state machine transition          StateTracer::logBuretteTransition(from, to);
task creation                     StackMonitor::registerThread(name, stack);
allocation > 4 KB                 HeapSnapshot::assertCanAllocate(size);
```

### Enforcement

- Code reviews MUST check for missing instrumentation
- Each component's tests should verify instrumentation is present (e.g., BlackBox event count increases)
- Sub-agents: mark missing instrumentation as a review finding

### Rationale

Every past crash in this project was detectible pre-mortem by a diagnostic event. The 2026-07-13 crash series proved that without BlackBox events from StateTracer and TickWatchdog, root cause analysis takes 3× longer and often misdiagnoses heap corruption for stack overflow (LL-001).

---

## Gaps and Fixes

### Gap 1: monitor.py Clears `capturing_coredump` at `!!! EXCEPTION END !!!`

**Status:** ✅ IMPLEMENTED (2026-07-13)

**File:** `scripts/monitor.py`

**Problem:**
```python
if "!!! EXCEPTION END !!!" in line:
    capturing_coredump = False
```
The `!!! EXCEPTION END !!!` marker is printed by the custom wrapper BEFORE calling `__real_esp_panic_handler`. The actual core dump data (backtrace, ELF SHA256, base64 dump) comes AFTER. Clearing the flag here discards all core dump data.

**Fix:** Remove `!!! EXCEPTION END !!!` from the capture-clearing condition. Only clear on `Rebooting...` or `ESP-ROM:` (bootloader ROM output after reset).

```python
if "Rebooting..." in line:
    capturing_coredump = False
```

---

### Gap 2: monitor.py Searches for `\x7fELF` in Base64-Encoded Data

**Status:** ✅ IMPLEMENTED (2026-07-13)

**File:** `scripts/monitor.py`

**Problem:**
```python
if coredump_buffer:
    elf_magic = b'\x7fELF'
    start = coredump_buffer.find(elf_magic)
```
ESP-IDF's `CONFIG_ESP_COREDUMP_ENABLE_TO_UART` transmits the core dump as **base64-encoded text**, not raw ELF binary. The `\x7fELF` magic never appears in the UART stream.

**Fix:** Detect core dump by alternative markers:
- Search for `CORE DUMP START` / `CORE DUMP END` markers (if ESP-IDF provides them)
- OR detect `esp_core_dump_uart:` log prefix and collect all subsequent data until `Rebooting...`
- OR decode base64 on the fly and then look for `\x7fELF` in the decoded buffer

```python
# Detect core dump start via log marker
if "esp_core_dump_uart: Print core dump to uart" in line:
    capturing_coredump = True  # re-arm if previously cleared
```

---

### Gap 3: `__wrap_esp_panic_handler` Does Not Feed RWDT

**Status:** ✅ IMPLEMENTED (2026-07-13)

**File:** `components/diag/src/crash_handler.cpp`

**Problem:**
The custom wrapper takes ~200-700 ms to dump BlackBox and watermarks over UART at 115200 baud. During this time, RWDT is NOT fed:
- ESP-IDF's `panic_handler()` does NOT feed WDTs in dual-core mode (line 153 of `panic_handler.c`)
- The custom wrapper does not feed WDTs
- Only `__real_esp_panic_handler` feeds WDTs, which runs AFTER the wrapper

If the BlackBox dump is large or UART is slow, the wrapper alone can consume a significant portion of the 10s RWDT budget.

**Fix:** Feed RWDT at the start of `__wrap_esp_panic_handler`:

```cpp
extern "C" void __wrap_esp_panic_handler(void* info) {
    // Feed RWDT to prevent reset during crash dump
    esp_panic_handler_feed_wdts();
    // ... rest of wrapper
}
```

Or, as a lighter alternative, config a long-enough RWDT timeout before heavy output.

---

### Gap 4: Crash Handler Ignores `panic_info_t*` — No Registers in Output

**Status:** ✅ IMPLEMENTED (2026-07-13)

**File:** `components/diag/src/crash_handler.cpp`

**Problem:**
The `__wrap_esp_panic_handler` function receives a `void* info` parameter (castable to `panic_info_t*`) but never reads it. The `panic_info_t` struct contains:
- `exccause` (exception cause register)
- `excvaddr` (exception address)
- `frame` (register frame with PC, PS, A0-A15, etc.)
- `core` (which CPU panicked)

The wrapper prints `=== CRASH ===` and the BlackBox but never outputs the exception cause, the faulting PC, or any register values. If `__real_esp_panic_handler` later crashes (e.g., secondary stack overflow in `log_worker`), this information is permanently lost.

**Fix:** Read `panic_info_t*` and output the key fields as a fallback:

```cpp
extern "C" void __wrap_esp_panic_handler(void* info) {
    auto* pi = static_cast<panic_info_t*>(info);
    panic_puts("=== CRASH ===\n");
    
    // Print exception cause (fallback — real handler does this too, but catches
    // cases where the real handler's output is lost to a secondary crash)
    char buf[128];
    auto* frame = pi->frame;
    snprintf(buf, sizeof(buf), "exccause=%d pc=0x%x excvaddr=0x%x\n",
             pi->exccause, frame->pc, pi->excvaddr);
    panic_puts(buf);
    
    // Print BlackBox, watermarks, then delegate
    BlackBox::instance().dump(panic_puts);
    StackMonitor::instance().logAllWatermarks();
    
    panic_puts("!!! EXCEPTION END !!!\n");
    __real_esp_panic_handler(info);
}
```

---

### Gap 5: `getThreadId()` Returns 0 for All Threads

**Status:** ✅ IMPLEMENTED (2026-07-13) — uses `pcTaskGetName()` to map task handles to thread IDs

**Files:** `components/diag/src/ffi_guard.cpp`

**Problem:**
```cpp
static uint8_t getThreadId() noexcept {
    return 0;
}
```
All 64 BlackBox events show `t0` — impossible to tell which thread recorded them.

**Fix:** Implement thread-ID mapping using FreeRTOS task handles:

```cpp
struct ThreadMapEntry {
    TaskHandle_t handle;
    uint8_t id;
    char name[16];
};

static constexpr ThreadMapEntry kThreadMap[] = {
    // Populated at register_thread() time from stack_monitor
};

static uint8_t getThreadId() noexcept {
    auto current = xTaskGetCurrentTaskHandle();
    for (auto& entry : kThreadMap) {
        if (entry.handle == current) return entry.id;
    }
    return 0xFF; // unknown
}
```

The `StackMonitor::registerThread()` call site already has the task handle. It should store it in a shared lookup table that `getThreadId()` can access.

---

### Gap 6: `crash_analyzer.py` Has No `0xa5a5a5a5` Pattern

**Status:** ✅ IMPLEMENTED (2026-07-13) — classifies as `stack_overflow` matching LL-001

**File:** `scripts/crash_analyzer.py`

**Problem:**
The FreeRTOS stack canary pattern `0xa5a5a5a5` appears in corrupted backtraces (e.g., `0x40385516:0xa5a5a5a5 |<-CORRUPTED`). The parser extracts it as a valid backtrace address but does not flag it as a canary. The `classify_crash()` function never checks backtrace addresses for `0xa5a5a5a5`.

**Fix:** Add canary detection to backtrace parsing:

```python
# After backtrace extraction
if any(addr == "0xa5a5a5a5" for addr, _ in backtrace):
    info["stack_overflow_task"] = "detected (canary 0xa5a5a5a5 in backtrace)"
```

And add a corresponding known-lesson entry for LL-001 (stack overflow → check watermark).

---

### Gap 7: StateTracer Uses `ESP_LOGI` — Events Lost During Crash

**Status:** ✅ IMPLEMENTED (2026-07-13) — `BlackBox::record()` added for all state transitions and errors

**Files:** `components/diag/src/state_tracer.cpp`

**Problem:**
All state transitions (burette idle→homing→working, etc.) and error events are logged via `ESP_LOGI` / `ESP_LOGE`. These go through the ESP log subsystem, which may be unavailable during a crash (especially if the crash is IN the log subsystem, like the `log_worker` overflow). They never reach the BlackBox ring buffer.

**Fix:** Add `BlackBox::record()` calls alongside `ESP_LOGI` in `StateTracer`:

```cpp
void StateTracer::logBuretteTransition(const char* from, const char* to) {
    ESP_LOGI(TAG, "Burette: %s -> %s", from, to);
    BlackBox::record(EventType::StateTransition, 0, 0); // id=0 for transitions
}
```

Define new event types in `BlackBox`:
```cpp
enum EventType : uint8_t {
    FfiEnter        = 0,
    FfiExit         = 1,
    StateTransition = 2,
    Error           = 3,
    TickDuration    = 4,
};
```

---

### Gap 8: TickWatchdog is a No-Op

**Status:** ✅ IMPLEMENTED (2026-07-13) — logs warning when >15ms, records TickDuration to BlackBox every 100 ticks

**File:** `components/diag/include/diag/tick_watchdog.hpp`

**Problem:**
```cpp
class TickWatchdog {
public:
    inline TickWatchdog() noexcept
        : startUs_(static_cast<uint64_t>(esp_timer_get_time())) {}
    inline ~TickWatchdog() noexcept = default;  // does NOTHING
private:
    uint64_t startUs_;
};
```

The `TickWatchdog` captures a timestamp at construction but never checks it at destruction. It should:
- Warn if main loop iteration exceeds expected duration (10 ms)
- Record the actual duration to BlackBox for forensic analysis

**Fix:** Implement the destructor:

```cpp
inline ~TickWatchdog() noexcept {
    auto elapsed = static_cast<uint64_t>(esp_timer_get_time()) - startUs_;
    if (elapsed > 15000) { // >15ms = warning threshold
        char buf[32];
        snprintf(buf, sizeof(buf), "tick=%llu", (unsigned long long)elapsed);
        // Log warning
    }
    // Periodically record to BlackBox (every 100th tick to avoid flooding)
    static uint32_t counter = 0;
    if (++counter % 100 == 0) {
        BlackBox::record(EventType::TickDuration,
                         static_cast<uint8_t>(elapsed / 1000), 0);
    }
}
```

---

### Gap 9: `log_worker` Stack Size Unknown — Secondary Crash During Dump

**Status:** ✅ IMPLEMENTED (2026-07-13) — registered with StackMonitor via `registerByHandle()`

**File:** `main/main.cpp`

**Problem:**
The `log_worker` task is created with 8192 bytes stack:
```cpp
xTaskCreate(LogBuffer::workerTaskEntry,
            "log_worker", 8192 / sizeof(configSTACK_DEPTH_TYPE),
            nullptr, 0, nullptr);
```
However, 8192 bytes is the total allocation but `sizeof(configSTACK_DEPTH_TYPE)` converts it to words. The task actually gets 8192 bytes, but the panic handler's output triggers a stack overflow in `log_worker` during crash dump, causing a secondary crash that corrupts the primary crash data.

Root cause: When the panic handler calls `wsLogCallback()` → `httpd_ws_send_frame_async()` (which is actually synchronous), the HTTPD stack usage bursts. Combined with log formatting, the 8192 bytes stack overflows.

**Fix:** Increase stack or ensure `wsLogCallback` does not suppress stack guard check. Also register `log_worker` with StackMonitor.

---

### Gap 10: StackMonitor Does Not Cover Internal ESP-IDF Tasks

**Status:** ✅ IMPLEMENTED (2026-07-13) — registers `log_worker`, `Tmr Svc`, `ipc0`, `ipc1`, `wifi`, `phy_init`

**File:** `components/diag/src/stack_monitor.cpp`

**Problem:**
Only application tasks are registered:
- main, temp, motor, net_owner, ble_notify

Internal ESP-IDF tasks are NOT monitored:
- `log_worker` (overflowed in the 2026-07-13 crash)
- `etcd` (ESP timer task)
- `ipc0`, `ipc1` (inter-processor call tasks)
- `wifi`, `phy_init` (WiFi driver tasks)

**Fix:** Add registration for critical internal tasks. Task handles can be obtained via `xTaskGetHandle()`:

```cpp
// In StackMonitor::init() or boot init:
if (auto h = xTaskGetHandle("log_worker")) {
    registerByHandle(h, "log_worker", 8192);
}
if (auto h = xTaskGetHandle("Tmr Svc")) {
    registerByHandle(h, "timer", 4096);
}
```

---

### Gap 11: `HeapSnapshot` Declared but No `.cpp` Implementation

**Status:** ✅ IMPLEMENTED (2026-07-13) — `heap_snapshot.cpp` created with `canAllocate()`, `largestFreeBlock()`, `log()`, `assertCanAllocate()`

**File:** `components/diag/include/diag/heap_snapshot.hpp`

**Problem:**
The header declares:
```cpp
class HeapSnapshot {
public:
    static bool canAllocate(size_t size);
    static size_t largestFreeBlock();
    static void log();
};
```

But `components/diag/src/heap_snapshot.cpp` **does not exist**. The functions cannot be linked. `GR-7` mandates `HeapSnapshot::assert_can_allocate(size)` before every allocation >4 KB — but this check can never run.

**Fix:** Create `heap_snapshot.cpp`:

```cpp
#include "diag/heap_snapshot.hpp"
#include "esp_heap_caps.h"

namespace ecotiter::diag {

bool HeapSnapshot::canAllocate(size_t size) {
    auto largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    return largest >= size;
}

size_t HeapSnapshot::largestFreeBlock() {
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
}

void HeapSnapshot::log() {
    // Print heap info via ESP_LOGI
}

bool HeapSnapshot::assertCanAllocate(size_t size) {
    auto largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (largest < size) {
        ESP_LOGW("heap", "Cannot alloc %u B (largest=%u B)", size, largest);
        return false;
    }
    return true;
}

} // namespace ecotiter::diag
```

And register it in `CMakeLists.txt`.

---

### Gap 12: BlackBox Contains Only FfiEnter/FfiExit — No Diverse Events

**Status:** ✅ IMPLEMENTED (2026-07-13) — StateTransition, Error, TickDuration events added from StateTracer and TickWatchdog

**File:** `components/diag/src/black_box.cpp`

**Problem:**
Only `FfiGuard` pushes events to BlackBox. StateTracer, TickWatchdog, StackMonitor, and HeapSnapshot all use `ESP_LOGx` or do nothing. The BlackBox ring buffer fills with 64 identical FfiEnter/FfiExit pairs from the main loop, crowding out potentially useful events from other subsystems.

**Fix:** Add event sources (see Gaps 7, 8, 11). Also, reduce FfiGuard verbosity:
- Only record FfiExit (not both Enter and Exit) for hot paths
- OR increase ring buffer size from 64 to 128
- OR use a sampling rate: record 1 in N FFI events in the main loop; record ALL events from motor/temp/net_owner threads

---

## Priority Matrix

| Gap | Area | Severity | Status | Priority |
|-----|------|----------|--------|----------|
| 1 | monitor.py — early capture clear | P0 | ✅ Fixed | **1** |
| 2 | monitor.py — ELF magic in base64 | P0 | ✅ Fixed | **2** |
| 3 | crash_handler — no WDT feed | P0 | ✅ Fixed | **3** |
| 4 | crash_handler — no register dump | P1 | ✅ Fixed | **4** |
| 5 | ffi_guard — thread ID hardcoded | P1 | ✅ Fixed | **5** |
| 6 | crash_analyzer — no canary pattern | P1 | ✅ Fixed | **6** |
| 7 | StateTracer — ESP_LOGI not BlackBox | P2 | ✅ Fixed | **7** |
| 8 | TickWatchdog — no-op | P2 | ✅ Fixed | **8** |
| 9 | log_worker — secondary crash | P2 | ✅ Fixed | **9** |
| 10 | StackMonitor — internal tasks | P3 | ✅ Fixed | **10** |
| 11 | HeapSnapshot — no .cpp | P3 | ✅ Fixed | **11** |
| 12 | BlackBox — event diversity | P3 | ✅ Fixed | **12** |

---

## Verification

### Verification (2026-07-14 — all 12 gaps fixed)

The system was verified on a real crash (`InstrFetchProhibited excvaddr=0x00000000`):

| Check | Result |
|-------|--------|
| `exccause=` and register dump in crash header | ✅ Printed |
| `0xa5a5a5a5` canary detection | ✅ Classified as `stack_overflow` |
| Core dump captured | ✅ 122497 bytes saved to `dumps/*.coredump.base64` |
| `crash_analyzer.py` classification | ✅ `InstrFetchProhibited` detected |
| RWDT fired during dump | ❌ NO — core dump completed before reset |
| BlackBox diverse events | ✅ StateTransition, Error events present |

### CI Integration

```bash
# Run crash analyzer on serial log
python3 scripts/crash_analyzer.py < logs/serial_*.log

# Check that dumps/ is non-empty after a crashed smoke test
ls -la dumps/
```

---

## Related Documents

| Document | Link |
|----------|------|
| GR-7 mandatory instrumentation requirements | (this document — §Mandatory Instrumentation Requirements) |
| Watchdog specification — RWDT 6s/10s config | [watchdog_spec.md](watchdog_spec.md) |
| WiFi specification — init order, IP visibility | [wifi_spec.md](wifi_spec.md) |
| LL-001: Stack overflow → check watermark | [../lessons_learned/LL-001.yaml](../lessons_learned/LL-001.yaml) |
| LL-046: Sub-agent scope creep | [../lessons_learned/LL-046.yaml](../lessons_learned/LL-046.yaml) |
| LL-047: RWDT not fed | [../lessons_learned/LL-047.yaml](../lessons_learned/LL-047.yaml) |
| ESP-IDF panic handler source | `/home/vlabe/Downloads/esp-idf-master/components/esp_system/src/panic_handler.c` |
| ESP-IDF core dump UART source | `/home/vlabe/Downloads/esp-idf-master/components/esp_system/src/esp_core_dump_uart.c` |
