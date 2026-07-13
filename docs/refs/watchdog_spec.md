---
type: ESP32 Reference
title: Watchdog Timer Specification
description: Architecture, rationale, and configuration for all hardware watchdog timers on ESP32-S3
tags: [watchdog, iwdt, rwdt, twdt, brownout, esp32-s3]
timestamp: 2026-07-13
---

# Watchdog Timer Specification

This document defines the watchdog timer architecture for the ecotiter firmware. Each watchdog type is analysed for coverage, limitations, and resource cost. The goal is defence in depth: a layered set of timers that catch different failure modes, with no single point of failure and no false-positive source in normal operation.

---

## Overview — Defence in Depth

The ESP32-S3 provides three independent hardware watchdog timers, plus a software-configurable brownout detector. Each covers a distinct failure domain:

| Layer | Watchdog | Failure Domain | Timeout | Action |
|-------|----------|---------------|---------|--------|
| 1 | **IWDT** | Spinlock deadlock, ISR runaway, interrupts masked >500ms | 500 ms | Panic → backtrace → reset |
| 2 | **TWDT** | Task-level hang (task stops yielding) | 10 s | Panic → backtrace → reset |
| 3 | **RWDT** | Complete system freeze (both CPUs, panic handler itself) | 6 s | Hard reset |
| — | **Brownout** | Supply voltage drop | — | Hard reset — **disabled** (spurious) |

All three active watchdogs use `WDT_STAGE_ACTION_RESET_SYSTEM` as the terminal stage, ensuring the chip always recovers.

---

## Layer 1: IWDT (Interrupt Watchdog Timer)

### Hardware
- Timer Group 1 watchdog (MWDT1).
- Fed automatically by the FreeRTOS tick ISR hook (`esp_register_freertos_tick_hook_for_cpu`).
- Stage 0 (500 ms): fires interrupt → panic handler runs → backtrace + black box dump.
- Stage 1 (1000 ms): hard reset if panic handler itself hangs.

### Coverage

| Scenario | Caught? |
|----------|---------|
| `portENTER_CRITICAL` without matching `portEXIT_CRITICAL` | ✅ — tick ISR stops firing |
| GPIO spinlock during PHY calibration (LL-031) | ✅ |
| ISR that runs too long without yielding | ✅ |
| Nested interrupt deadlock | ✅ |
| Task-level infinite loop (interrupts still enabled) | ❌ — tick ISR keeps firing |

### Configuration

```ini
CONFIG_ESP_INT_WDT=y
CONFIG_ESP_INT_WDT_TIMEOUT_MS=500
```

### Resource Cost
- One MWDT hardware instance (shared with Timer Group 1).
- No stack or DRAM overhead — tick hook is zero-cost when not triggered.
- Comment in `sdkconfig.defaults` lines 8-15 documents the rationale.

### Verdict — ✅ KEEP

IWDT is the **first line of defence** against the most common embedded crash pattern: spinlock deadlock. Without it, a single `portENTER_CRITICAL` / `gpio_set_direction` deadlock hangs the chip silently until RWDT fires 6 s later, with no diagnostic evidence.

---

## Layer 2: TWDT (Task Watchdog Timer)

### Hardware
- Software timer running on the FreeRTOS tick.
- Fed by calling `esp_task_wdt_reset()` from each subscribed task.
- NOT fed by the tick ISR — TWDT advances even when interrupts are masked.
- Stage 0 (10 s): fires interrupt → `esp_task_wdt_isr_user_handler` → panic.
- Stage 1 (11 s): hard reset if panic handler hangs.

### Failure Domain

TWDT catches the class of bugs that IWDT **cannot**: a task that enters an infinite loop or blocks on a resource while the tick ISR continues to fire. Examples:

- `xQueueReceive(portMAX_DELAY)` on a queue that never receives.
- `while (!flag) { }` busy-wait with no yield.
- RMT `tx_wait_all_done()` hangs because of a hardware glitch.
- `vTaskDelay(60000)` typo (60 s instead of 60 ms).

### Current Status (before this spec)

`CONFIG_ESP_TASK_WDT_INIT=y` was set in `sdkconfig.defaults`, but no application task called `esp_task_wdt_add()`. Only the FreeRTOS idle tasks (CPU0 and CPU1) were subscribed. This meant TWDT only fired if **both** CPUs were starved of idle time for 10 s — a scenario already covered by IWDT. **TWDT was effectively dead code.**

### Configuration

```ini
CONFIG_ESP_TASK_WDT_INIT=y
CONFIG_ESP_TASK_WDT_PANIC=y
CONFIG_ESP_TASK_WDT_TIMEOUT_S=10
```

### Subscription — All Four Application Tasks

| Task | Entry Point | Subscribe At | Feed Interval | Feed Point |
|------|-------------|-------------|---------------|------------|
| **main** | `app_main()` | After boot init, before `while(true)` | 10 ms | Top of `while(true)` loop |
| **motor** | `motorTaskEntry()` | After queue creation, before `while(true)` | ≤ 100 ms | Top of command loop + each RMT chunk |
| **temp** | `tempTaskEntry()` | After `registerThread()` | ≤ 1000 ms | Top of `while(true)` loop |
| **net_owner** | `netTaskEntry()` | After `registerThread()` | ≤ 100 ms | Top of `while(true)` loop |

All feed points are in `while(true)` loops that iterate well within the 10 s budget. The motor task also feeds during long-running operations (homing, rinsing, moves) at each RMT chunk boundary (~85 ms), matching the existing RWDT feed points.

### Interaction with Long Operations

| Operation | Duration | TWDT Risk | Mitigation |
|-----------|----------|-----------|------------|
| Homing | Up to 120 s | Would timeout after 10 s | Feed TWDT every RMT chunk (~85 ms) alongside existing `gRtcWdt->feed()` |
| Rinse SM | Minutes | Would timeout after 10 s | Feed TWDT every fill/empty cycle |
| Cal speed seq | Minutes | Would timeout after 10 s | Feed TWDT every tick step |
| `xQueueReceive(100 ms)` | 100 ms block | No risk (feed before call) | Reset at top of loop |

### Resource Cost
- Internal DRAM: ~256 bytes for subscription list (negligible).
- Stack per `esp_task_wdt_reset()` call: ~32 bytes.
- No additional heap allocations after init.

### Verdict — ✅ KEEP & REVIVE

TWDT fills the gap between IWDT (interrupt-level hangs) and RWDT (system-freeze). A panic with backtrace pointing to the hung task is far more useful than a silent RWDT reset. All four application tasks must be subscribed.

---

## Layer 3: RWDT (RTC Watchdog Timer)

### Hardware
- RTC timer running on the RTC slow clock (~150 kHz internal RC or ~32 kHz external XTAL).
- Independent of CPU clock, FreeRTOS tick, and interrupt subsystem.
- Stage 0: **disabled** (we skip the implicit multiplier stage).
- Stage 1 (6 s): `WDT_STAGE_ACTION_RESET_SYSTEM` — hard reset.

### Failure Domain

RWDT is the **last resort** — it fires when even the panic handler cannot run. Scenarios:

- Both CPUs are stuck in spinlock with interrupts disabled >6 s.
- IWDT panic handler itself enters an infinite loop.
- The RTC slow clock keeps running even when the main CPU clock has stopped.

### Feed Strategy

- **Main loop:** feeds every 10 ms via `rtcWdt.feed()` at line 427 of `main.cpp`.
- **Motor task:** feeds during long operations via `gRtcWdt->feed()` in homing, rinsing, moves.
- **Other tasks (temp, net_owner):** do NOT feed. If they block, the main loop continues to feed. If the main loop also blocks, the RWDT reset is the correct outcome.

### RAII Wrapper

The `RtcWatchdog` class (`components/diag/src/rtc_watchdog.cpp`) owns the entire RWDT lifecycle:

```
Constructor: wdt_hal_init → configure stages → wdt_hal_enable
Destructor:  wdt_hal_deinit
feed():      write_protect_disable → wdt_hal_feed → write_protect_enable
```

The global pointer `diag::gRtcWdt` is set in the constructor and cleared in the destructor, allowing cross-task access from the motor task.

### Configuration

```ini
# sdkconfig.defaults
CONFIG_BOOTLOADER_WDT_DISABLE_IN_USER_CODE=y
```

This prevents ESP-IDF from disabling the RWDT at startup. The bootloader leaves RWDT running with its default timeout (~9 s). The `RtcWatchdog` constructor reconfigures it to 6 s.

### Resource Cost
- RTC timer hardware (always powered, zero incremental cost).
- No DRAM or stack beyond the `RtcWatchdog` object (~8 bytes + HAL context).

### Known Incidents

| Incident | Cause | Fix |
|----------|-------|-----|
| LL-047 (2026-07-13) | `RtcWatchdog` RAII commented out → RWDT never configured → bootloader 9 s timeout fired | Uncommented RAII, added global pointer management |

### Verdict — ✅ KEEP

RWDT is the non-negotiable last line of defence. It is the only watchdog that can recover from a panic-handler hang. Must always be enabled and fed from the main loop.

---

## Brownout Detector

### Hardware
- Chip supply voltage monitor.
- Triggers at ~2.45 V (configurable threshold).
- Action: hard reset (bypasses panic handler entirely).

### Reason for Disabling

`CONFIG_BROWNOUT_DET=n` in `sdkconfig.defaults` line 20.

The ESP32-S3 + 8 MB octal PSRAM + RGB LED burst causes transient voltage drops during WiFi/BLE init. These drops are within the chip's operating range (< 100 µs dips to ~2.8 V) but occasionally trigger the brownout detector. The WDTs (IWDT + TWDT + RWDT) provide sufficient protection against true system hangs.

### Verdict — ❌ DISABLED

Spurious resets during normal operation are worse than no protection against brownout, because:
1. Brownout is extremely rare in a powered USB/supply scenario.
2. The WDT stack already catches every meaningful hang.
3. A brownout reset bypasses diagnostics — no backtrace, no black box dump.

Re-evaluate if field data shows unrecoverable low-voltage lockups.

---

## Summary — Which Watchdogs Are Useful

| Watchdog | Useful | Rationale |
|----------|--------|-----------|
| **IWDT** | ✅ Yes | Catches spinlock deadlocks and ISR hangs — the #1 embedded crash pattern. Zero cost when idle. |
| **TWDT** | ✅ Yes | Catches task-level hangs that IWDT misses. Panic with backtrace pinpoints the hung task. All 4 app tasks must be subscribed. |
| **RWDT** | ✅ Yes | Last resort — fires when even the panic handler fails. Must be fed from main loop. Non-negotiable. |
| **Brownout** | ❌ No | Causes spurious resets during normal WiFi/BLE init. WDTs provide sufficient coverage. |

---

## Verification

### Build-time
- `CONFIG_ESP_INT_WDT=y` — IWDT enabled.
- `CONFIG_ESP_TASK_WDT_INIT=y` — TWDT enabled at startup.
- `CONFIG_BOOTLOADER_WDT_DISABLE_IN_USER_CODE=y` — RWDT kept alive for app.
- `CONFIG_BROWNOUT_DET=n` — brownout disabled.

### Runtime
- **Log check:** `BOOT OK` followed by `RWDT enabled: 6 s timeout` — confirms RWDT init.
- **Watermarks:** `uxTaskGetStackHighWaterMark()` after 60 s of operation should show all tasks with > 20 % headroom.
- **Smoke test:** 30 s monitor with no `rst:`, no `TWDT panic`, no `Guru Meditation`.

### Intentional failure test (development only)
- Temporarily remove `esp_task_wdt_reset()` from one task → confirm TWDT panic with backtrace pointing to that task.
- Temporarily set `RWDT_TIMEOUT_S = 2` and stall main loop → confirm `rst:0x10 (RTCWDT_RTC_RST)`.

---

## Related Documents

| Document | Link |
|----------|------|
| Project pinout and stack budgets | [project.md](project.md) |
| Coding conventions (RAII, error handling) | [coding_style.md](coding_style.md) |
| LL-047: RWDT not fed | [../lessons_learned/LL-047.yaml](../lessons_learned/LL-047.yaml) |
| LL-045: UNICORE spinlock deadlock | [../lessons_learned/LL-045.yaml](../lessons_learned/LL-045.yaml) |
| LL-031/032: PHY calibration GPIO spinlock | [../lessons_learned/LL-031.yaml](../lessons_learned/LL-031.yaml) |
| Unsafe GPIO pins | [unsafe_gpio_pins.md](unsafe_gpio_pins.md) |
