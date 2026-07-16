---
type: Known Issue
title: "Internal ESP-IDF tasks not registered with StackMonitor -- deferred registration needed"
description: "Tmr Svc and wifi tasks are never registered because xTaskGetHandle() is called before they exist. Only 8 of 10+ tasks appear in watermark logs. Proposed fix: two-phase registration with deferred lazy registration after WiFi init. (phy_init is NOT a FreeRTOS task — removed per v6 API verification.)"
tags: [stack, diagnostic, monitoring, registration]
timestamp: 2026-07-16
status: active
---

# Internal ESP-IDF tasks not registered with StackMonitor

## Problem

`StackMonitor::registerMainTask()` in `components/diag/src/stack_monitor.cpp:11` calls `xTaskGetHandle()` for internal ESP-IDF tasks (Tmr Svc, wifi) in a single synchronous pass at boot time. These tasks do not yet exist (or are not found for Tmr Svc) and registration is silently skipped. Only 8 of 10+ registered tasks appear in watermark output.

### Current watermark output (Phase 1, ISSUE-005)

Only these 8 tasks appear (missing: Tmr Svc, wifi):

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
| Tmr Svc | FreeRTOS `vTaskStartScheduler` | Early boot (before `app_main`) | In `registerMainTask()` | ❌ not found (unexpected — see §Phase 0) |
| wifi | `esp_wifi_init()` | During `net_owner` init | In `registerMainTask()` | ❌ not found (too early) |

Note: `ipc0` and `ipc1` ARE found by `xTaskGetHandle()` at boot time, confirming the mechanism works. **ESP-IDF v6 API verification** at `/home/vlabe/Downloads/esp-idf-master` confirmed:
- `configTIMER_SERVICE_TASK_NAME` is `"Tmr Svc"` (FreeRTOS-Kernel/timers.c:72, FreeRTOSConfig.h:193) — **name is correct** in the source.
- `CONFIG_FREERTOS_TIMER_SERVICE_TASK_NAME="Tmr Svc"` (sdkconfig:2957) — no override.
- `xTimerCreateTimerTask()` runs inside `vTaskStartScheduler()` (tasks.c:2395), which executes **before** `app_main`. Tmr Svc SHOULD exist when `registerMainTask()` runs.
- `phy_init` is **NOT a FreeRTOS task** — the string only appears as a log tag (`phy_init.c:76`). PHY calibration code runs synchronously inside the wifi task context. Removed from this issue.

### Impact

1. **Incomplete diagnostic data.** Watermark output is missing 2 of 10+ tasks. StackMonitor cannot detect stack overflows in these tasks.
2. **CI gate blind spot.** Phase 4 of ISSUE-005 (check_watermarks.py — not yet created) will need an accurate `EXPECTED_TASKS` list. Missing tasks will cause false CI failures.
3. **Silent failure.** `xTaskGetHandle()` returning `nullptr` produces no warning or error log.

## Root cause

`registerMainTask()` is called from `app_main` before the WiFi driver runs. The function has a single synchronous loop over `xTaskGetHandle()` with no retry mechanism. Tasks not yet created are silently skipped.

For `Tmr Svc`: the task IS created by `vTaskStartScheduler()` before `app_main`, and the name `"Tmr Svc"` is correct in both the source code and sdkconfig. The reason `xTaskGetHandle("Tmr Svc")` returns nullptr at boot is **unknown** — this requires further investigation (see §Phase 0).

## Solution — Two-phase registration

**Key findings from ESP-IDF v6 API verification (2026-07-16):**
- `Tmr Svc` — name `"Tmr Svc"` is correct, task is created before `app_main`. Currently not found in `registerMainTask()` for unknown reason. **Phase 0 must resolve** whether it can stay in `kInternal[]` or also needs deferral. Stack size in code (4096) is wrong — actual is 2048.
- `wifi` — name `"wifi"` is correct, created during `esp_wifi_init()` in `net_owner`. **Must be deferred.** Stack size 8192 is conventional and acceptable.
- `phy_init` — **NOT a FreeRTOS task.** The string is a log tag only (`phy_init.c:76`). PHY calibration runs synchronously inside the wifi task context. **Removed from this issue entirely.**
- `ensureGpioReady()` — referenced in docs (`project.md:241`, `CONSTITUTION.md:53`) but **does not exist in source code**. Must be implemented as prerequisite.
- `check_watermarks.py` — referenced in ISSUE-005 Phase 4 but **does not exist yet**.

### Phase 0 — Investigate Tmr Svc not found

The ESP-IDF v6 API verification (2026-07-16) confirmed:
- Task name `"Tmr Svc"` is correct (`FreeRTOS-Kernel/timers.c:72`, `FreeRTOSConfig.h:193`, `sdkconfig:2957`)
- Task is created by `vTaskStartScheduler()` before `app_main` (`tasks.c:2395`)
- Stack size is `CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH=2048` (`sdkconfig:2969`), NOT 4096 as currently hardcoded in `stack_monitor.cpp:16`

Since timing and name are both correct, the root cause of the nullptr is unknown. Before implementation:

1. Add a debug log to `registerMainTask()` showing the return value of `xTaskGetHandle("Tmr Svc")`
2. Build, flash, and check the log
3. If found → **keep Tmr Svc in `kInternal[]`**, no deferral needed. Only wifi is deferred.
4. If still not found → investigate further (queue creation failure in `xTimerCreateTimerTask`? FreeRTOS config issue?)
5. Regardless of outcome, fix the stack size from 4096 → 2048.

### Phase 1 — Split registration

| Step | File | Change |
|------|------|--------|
| 1.1 | `stack_monitor.hpp` | Add `void registerLazyTasks() noexcept;` declaration |
| 1.2 | `stack_monitor.cpp:registerMainTask()` | Remove `wifi` from `kInternal[]` array (keep main, Tmr Svc, ipc0, ipc1). Fix `Tmr Svc` stack: 4096 → 2048. |
| 1.3 | `stack_monitor.cpp` | New method `registerLazyTasks()`: poll `xTaskGetHandle` for deferred `"wifi"` with retry (every 100ms, max 5 attempts, log warning on failure) |
| 1.4 | `net_owner.cpp` | After `ensureGpioReady()` (implements PHY deinit/reinit per LL-031), call `StackMonitor::instance().registerLazyTasks()` |

### Deferred registration design

```cpp
void StackMonitor::registerLazyTasks() noexcept {
    // Only wifi needs deferral. Tmr Svc is created by vTaskStartScheduler
    // before app_main (confirmed by v6 API verification) and stays in kInternal[].
    static constexpr KnownTask kLazy[] = {
        {"wifi", 8192},
    };
    for (auto& t : kLazy) {
        TaskHandle_t h = nullptr;
        for (int attempt = 0; attempt < 5; ++attempt) {
            h = xTaskGetHandle(t.name);
            if (h != nullptr) {
                registerByHandle(h, t.name, t.stack);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (h == nullptr) {
            ESP_LOGW("stack_monitor", "Deferred task '%s' not found after 5 retries", t.name);
        }
    }
}
```

### Why not a single retry loop in registerMainTask?

A single retry loop in `registerMainTask()` would block `app_main` for up to 500ms waiting for the wifi task that won't exist until `net_owner` runs. This violates Constitution Art. I (main loop non-blocking).

### Risks

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| Tmr Svc still not found at boot | Medium | Phase 0 investigation required before implementation; if unresolved, defer with retry + log warning |
| Wifi task never created (rare race) | Low | 5 retries × 100ms = 500ms window; `esp_wifi_init()` runs within seconds |
| `ensureGpioReady()` not yet implemented | High | Must be implemented before or alongside this issue (prerequisite) |

## Acceptance criteria

- `scripts/idf.sh smoke` + 70s → `rg "Thread" logs/serial_*.log` shows 10+ tasks including `Tmr Svc` and `wifi`
- No compilation warnings
- No crashes during boot
- Zero additional DRAM beyond existing `MAX_THREADS` slots (already bumped to 16)
- **Note:** `phy_init` is intentionally absent — verified as NOT a FreeRTOS task. It runs inside the wifi task context.

## Related

- [ISSUE-005: Stack sizing](ISSUE-005-stacking-sizing.md) — Phase 1 (MAX_THREADS 8→16) enabled capacity for these tasks
- [StackMonitor source](../../components/diag/src/stack_monitor.cpp:11) — `registerMainTask()`
- [StackMonitor header](../../components/diag/include/diag/stack_monitor.hpp)
- [net_owner — WiFi/PHY init sequence](../../main/net_owner.cpp)
- [Phase 4 CI gate script](../../scripts/check_watermarks.py) (⚠️ not yet created)
- [Verification report (2026-07-16)](../../docs/plans/completed/ISSUE-006-plan-verification.md)
