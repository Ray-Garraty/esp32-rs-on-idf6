---
type: Known Issue
title: "Internal ESP-IDF tasks not registered with StackMonitor -- deferred registration needed"
description: "Wifi task was never registered because xTaskGetHandle() called before it exists. Fix: two-phase registration with registerLazyTasks() after BLE init. Tmr Svc intentionally not created in ESP-IDF v6 (weak stub optimization when no FreeRTOS SW timers used). phy_init removed. ensureGpioReady() moved to separate issue."
tags: [stack, diagnostic, monitoring, registration]
timestamp: 2026-07-16
updated: 2026-07-17
status: resolved
---

# Internal ESP-IDF tasks not registered with StackMonitor

## Problem

`StackMonitor::registerMainTask()` in `components/diag/src/stack_monitor.cpp:11` calls `xTaskGetHandle()` for internal ESP-IDF tasks (Tmr Svc, wifi) in a single synchronous pass at boot time. The wifi task is not yet created (`esp_wifi_init()` runs later in `net_owner`). Tmr Svc is intentionally never created by ESP-IDF v6 (weak stub optimization). Both registrations are silently skipped. Only 8 of 10+ registered tasks appear in watermark output.

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
| Tmr Svc | FreeRTOS `vTaskStartScheduler` | N/A — **never created** | In `registerMainTask()` + `registerLazyTasks()` | ❌ not found (expected — see §Root cause) |
| wifi | `esp_wifi_init()` | During `net_owner` init | In `registerMainTask()` | ❌ not found (too early) |

Note: `ipc0` and `ipc1` ARE found by `xTaskGetHandle()` at boot time, confirming the mechanism works. `phy_init` is **NOT a FreeRTOS task** — the string only appears as a log tag (`phy_init.c:76`). PHY calibration code runs synchronously inside the wifi task context. Removed from this issue.

### Impact

1. **Incomplete diagnostic data.** Watermark output is missing 2 of 10+ tasks. StackMonitor cannot detect stack overflows in these tasks.
2. **CI gate blind spot.** `scripts/check_watermarks.py` had `EXPECTED_TASKS` including `phy_init` (not a task) while missing `Tmr Svc` and `wifi`. Missing tasks cause false CI failures.
3. **Silent failure.** `xTaskGetHandle()` returning `nullptr` produces no warning or error log.

## Root cause

`registerMainTask()` is called from `app_main` before the WiFi driver runs. The function has a single synchronous loop over `xTaskGetHandle()` with no retry mechanism. Tasks not yet created are silently skipped.

For `Tmr Svc`: **the timer service task is NEVER created.** ESP-IDF v6 provides a **weak** `xTimerCreateTimerTask()` stub at `freertos_tasks_c_additions.h:378` that returns `pdPASS` without creating the task. The real implementation in `FreeRTOS-Kernel/timers.c` is only linked when the application calls at least one FreeRTOS software timer function (e.g., `xTimerCreate()`, `xTimerStart()`). This firmware exclusively uses `esp_timer` — no FreeRTOS timer API calls exist. BLE is also configured with `CONFIG_BT_NIMBLE_USE_ESP_TIMER=y`, so no FreeRTOS timer path is linked. The weak stub is an intentional ESP-IDF memory optimization to avoid wasting ~2KB stack on the daemon task when it's not needed.

**Resolution:** `"Tmr Svc"` removed from registration entirely. The task will never exist unless the firmware starts using FreeRTOS software timers.

## Solution — Two-phase registration

**Key findings from ESP-IDF v6 API verification (2026-07-16 + 2026-07-17 research):**
- `Tmr Svc` — name `"Tmr Svc"` is correct (`FreeRTOS-Kernel/timers.c:72`, `FreeRTOSConfig.h:193`, `sdkconfig`). However, ESP-IDF v6 provides a **weak** `xTimerCreateTimerTask()` stub (`freertos_tasks_c_additions.h:378`) that returns `pdPASS` without creating the task. The real `timers.c` is only linked when the app calls FreeRTOS timer API functions. This firmware doesn't — it exclusively uses `esp_timer`. **Tmr Svc is never created.** Stack size in code (4096) was wrong — actual Kconfig default is 2048.
- `wifi` — name `"wifi"` is correct, created during `esp_wifi_init()` in `net_owner`. **Must be deferred.** Stack size 8192 is conventional and acceptable.
- `phy_init` — **NOT a FreeRTOS task.** The string is a log tag only (`phy_init.c:76`). No `xTaskCreate` with name `"phy_init"` exists anywhere in the ESP-IDF source tree. **Removed from this issue entirely.**
- `ensureGpioReady()` — referenced in docs (`project.md:241`, `CONSTITUTION.md:53`) but **does not exist in source code**. The referenced APIs `esp_phy_deinit()` / `esp_phy_init()` **do NOT exist in ESP-IDF v6**. Only `esp_phy_enable()`/`esp_phy_disable()` and private `phy_wakeup_init()`/`phy_close_rf()` are available. **Moved to separate issue** — not a prerequisite for ISSUE-006.
- `check_watermarks.py` — existed at `scripts/check_watermarks.py` with `phy_init` in `EXPECTED_TASKS`. **Fixed.**

### Implementation — Changes applied (2026-07-17)

#### Phase 0 — Investigate Tmr Svc not found

| Step | Status | Detail |
|------|--------|--------|
| Root cause identified | ✅ Done | ESP-IDF v6 weak `xTimerCreateTimerTask()` stub at `freertos_tasks_c_additions.h:378` — task never created |
| Fix stack size 4096→2048 | ✅ Done | Was `{"Tmr Svc", 4096}` → corrected to `2048` but moot: removed entirely |
| Remove Tmr Svc from registration | ✅ Done | Not in `kInternal[]` or `kLazy[]` — task will never exist

#### Phase 1 — Split registration

| Step | File | Change | Status |
|------|------|--------|--------|
| 1.1 | `stack_monitor.hpp:20` | Add `void registerLazyTasks() noexcept;` declaration | ✅ Done |
| 1.2 | `stack_monitor.cpp:15-26` | Remove wifi/phy_init/Tmr Svc from `kInternal[]`. Only ipc0/ipc1 remain. | ✅ Done |
| 1.3 | `stack_monitor.cpp:33-51` | New method `registerLazyTasks()`: poll `"wifi"` 5×100ms | ✅ Done |
| 1.4 | `net_owner.cpp:78` | Call `registerLazyTasks()` after BLE init, before queue creation | ✅ Done |
| 1.5 | `check_watermarks.py:18` | Remove `"phy_init"` from `EXPECTED_TASKS` | ✅ Done |

### Deferred registration design (as implemented)

```cpp
void StackMonitor::registerLazyTasks() noexcept {
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

## Smoke test results (2026-07-17)

Final `scripts/idf.sh smoke` run:

| Criterion | Result |
|-----------|--------|
| Device boots | ✅ BOOT OK (no Guru Meditation, no panics, no WDT resets) |
| WiFi connects + gets IP | ✅ 192.168.1.103 |
| HTTP server starts | ✅ |
| BLE inits | ✅ |
| `wifi` task registration | ✅ No `ESP_LOGW("Deferred task not found")` — found on first attempt |
| Immediate watermark output | ✅ 9 tasks visible at ~3.6s post-boot (immediate `logAllWatermarks()` in `net_owner`) |
| `Tmr Svc` | ✅ Confirmed intentionally not created (ESP-IDF v6 weak stub) — removed from registration |
| No compilation warnings | ✅ `scripts/idf.sh build` — 0 errors, 0 warnings |
| Unit tests | ✅ 246/246, 776 assertions |
| clang-tidy | ✅ pass |
| Modified files | `stack_monitor.hpp`, `stack_monitor.cpp`, `net_owner.cpp`, `check_watermarks.py` |

### Final watermark output (T+3.6s)

```
I (3582) stack_monitor: Thread main:       cfg=32768B  wmark=25980  used=20%
I (3584) stack_monitor: Thread ipc0:       cfg=4096B   wmark=476    used=88%
I (3592) stack_monitor: Thread ipc1:       cfg=4096B   wmark=480    used=88%
I (3595) stack_monitor: Thread temp:       cfg=16384B  wmark=2276   used=86%
I (3601) stack_monitor: Thread motor:      cfg=16384B  wmark=1972   used=87%
I (3607) stack_monitor: Thread net_owner:  cfg=20480B  wmark=2276   used=88%
I (3613) stack_monitor: Thread ble_notify: cfg=8192B   wmark=5272   used=35%
I (3622) stack_monitor: Thread wifi:       cfg=8192B   wmark=3684   used=55%
I (3625) stack_monitor: Thread log_worker: cfg=16384B  wmark=2512   used=84%
```

9 tasks registered. `Tmr Svc` intentionally absent — see root cause.

## Acceptance criteria — status

| Criterion | Status |
|-----------|--------|
| 9 tasks visible in watermarks within 10s of boot | ✅ Visible at T+3.6s |
| `wifi` registered | ✅ Found by `registerLazyTasks()` on first attempt |
| `Tmr Svc` | ✅ Root cause identified: intentionally not created by ESP-IDF v6 weak stub |
| No compilation warnings | ✅ |
| No crashes during boot | ✅ |
| Zero additional DRAM (MAX_THREADS=16) | ✅ |
| `phy_init` absent | ✅ Removed from code and EXPECTED_TASKS |

## Remaining work

- [ ] Verify `check_watermarks.py` EXPECTED_TASKS matches current 9-task list (optional — CI gate)

## Related

- [ISSUE-005: Stack sizing](ISSUE-005-stacking-sizing.md) — Phase 1 (MAX_THREADS 8→16) enabled capacity for these tasks
- [StackMonitor source](../../components/diag/src/stack_monitor.cpp:11) — `registerMainTask()`
- [StackMonitor header](../../components/diag/include/diag/stack_monitor.hpp)
- [net_owner — WiFi/BLE init sequence](../../main/net_owner.cpp)
- [CI gate script](../../scripts/check_watermarks.py)
