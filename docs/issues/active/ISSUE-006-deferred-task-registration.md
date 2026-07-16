---
type: Known Issue
title: "Internal ESP-IDF tasks not registered with StackMonitor -- deferred registration needed"
description: "Tmr Svc, wifi, and phy_init tasks are never registered because xTaskGetHandle() is called before they exist. Only 8 of 11+ tasks appear in watermark logs. Proposed fix: two-phase registration with deferred lazy registration after WiFi/PHY init."
tags: [stack, diagnostic, monitoring, registration]
timestamp: 2026-07-16
status: active
---

# Internal ESP-IDF tasks not registered with StackMonitor

## Problem

`StackMonitor::registerMainTask()` in `components/diag/src/stack_monitor.cpp:11` calls `xTaskGetHandle()` for internal ESP-IDF tasks (Tmr Svc, wifi, phy_init) in a single synchronous pass at boot time. These tasks do not yet exist and registration is silently skipped. Only 8 of 11+ registered tasks appear in watermark output.

### Current watermark output (Phase 1, ISSUE-005)

Only these 8 tasks appear (missing: Tmr Svc, wifi, phy_init):

```
Thread main:       cfg=32768B  wmark=25980  used=20%
Thread ipc0:       cfg=4096B   wmark=476    used=88%
Thread ipc1:       cfg=4096B   wmark=560    used=86%
Thread temp:       cfg=16384B  wmark=2148   used=86%
Thread motor:      cfg=16384B  wmark=1460   used=91%
Thread net_owner:  cfg=20480B  wmark=208    used=98%
Thread ble_notify: cfg=8192B   wmark=5264   used=35%
Thread log_worker: cfg=16384B  wmark=2140   used=86%
```

### Affected tasks

| Task | Created by | Likely created | Registration attempt | Status |
|------|-----------|----------------|---------------------|--------|
| Tmr Svc | FreeRTOS `vTaskStartScheduler` | Early boot | In `registerMainTask()` | ❌ not found (unknown reason — may be timing or name mismatch) |
| wifi | `esp_wifi_init()` | During `net_owner` init | In `registerMainTask()` | ❌ not found (too early) |
| phy_init | ESP-IDF PHY calibration | During `net_owner` init | In `registerMainTask()` | ❌ not found (too early) |

Note: `ipc0` and `ipc1` ARE found by `xTaskGetHandle()` at boot time, confirming the mechanism works. Tmr Svc may have a different name in ESP-IDF v6 or may not be created by the time `registerMainTask()` runs. This must be investigated in Step 0.

### Impact

1. **Incomplete diagnostic data.** Watermark output is missing 3 of 11+ tasks. StackMonitor cannot detect stack overflows in these tasks.
2. **CI gate blind spot.** Phase 4 of ISSUE-005 (check_watermarks.py) expects all 12 tasks in `EXPECTED_TASKS`. Missing tasks will cause false CI failures.
3. **Silent failure.** `xTaskGetHandle()` returning `nullptr` produces no warning or error log.

## Root cause

`registerMainTask()` is called from `app_main` before the WiFi driver and PHY calibration run. The function has a single synchronous loop over `xTaskGetHandle()` with no retry mechanism. Tasks not yet created are silently skipped.

Additionally, the ESP-IDF v6 timer service task name may differ from the hardcoded `"Tmr Svc"` used in the source.

## Solution — Two-phase registration

### Phase 0 — Investigate Tmr Svc name

Check `/home/vlabe/Downloads/esp-idf-master/components/freertos/FreeRTOS-Kernel/tasks.c` or ESP-IDF startup code for the actual timer task name used in ESP-IDF v6. If different, update the name in `kInternal[]`. If timing is the issue, treat Tmr Svc the same as wifi/phy_init (deferred).

### Phase 1 — Split registration

| Step | File | Change |
|------|------|--------|
| 1.1 | `stack_monitor.hpp` | Add `void registerLazyTasks() noexcept;` declaration |
| 1.2 | `stack_monitor.cpp:registerMainTask()` | Remove `Tmr Svc`, `wifi`, `phy_init` from `kInternal[]` array (keep main, ipc0, ipc1) |
| 1.3 | `stack_monitor.cpp` | New method `registerLazyTasks()`: poll `xTaskGetHandle` for each deferred task with retry (every 100ms, max 5 attempts, log warning on failure) |
| 1.4 | `net_owner.cpp` | After `ensureGpioReady()` (completes PHY calibration), call `StackMonitor::instance().registerLazyTasks()` |

### Deferred registration design

```cpp
void StackMonitor::registerLazyTasks() noexcept {
    static constexpr KnownTask kLazy[] = {
        {"Tmr Svc", 4096},
        {"wifi", 8192},
        {"phy_init", 4096},
    };
    for (auto& t : kLazy) {
        for (int attempt = 0; attempt < 5; ++attempt) {
            TaskHandle_t h = xTaskGetHandle(t.name);
            if (h != nullptr) {
                registerByHandle(h, t.name, t.stack);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}
```

### Why not a single retry loop in registerMainTask?

A single retry loop in `registerMainTask()` would block `app_main` for up to 500ms waiting for WiFi/PHY tasks that won't exist until `net_owner` runs. This violates Constitution Art. I (main loop non-blocking).

### Risks

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| Tmr Svc name mismatch | Medium | Phase 0 investigation before implementation |
| Tmr Svc still not found after retry | Low | Log warning; make `EXPECTED_TASKS` in CI gate tolerant of missing Tmr Svc |
| Deferred tasks never created (rare race) | Low | 5 retries × 100ms = 500ms window; WiFi/PHY init starts within seconds |

## Acceptance criteria

- `scripts/idf.sh smoke` + 70s → `rg "Thread" logs/serial_*.log` shows 11+ tasks including `Tmr Svc`, `wifi`, `phy_init`
- No compilation warnings
- No crashes during boot
- Zero additional DRAM beyond existing `MAX_THREADS` slots (already bumped to 16)

## Related

- [ISSUE-005: Stack sizing](ISSUE-005-stacking-sizing.md) — Phase 1 (MAX_THREADS 8→16) enabled capacity for these tasks
- [StackMonitor source](../../components/diag/src/stack_monitor.cpp:11) — `registerMainTask()`
- [StackMonitor header](../../components/diag/include/diag/stack_monitor.hpp)
- [net_owner — WiFi/PHY init sequence](../../main/net_owner.cpp)
- [Phase 4 CI gate script](../../scripts/check_watermarks.py)
