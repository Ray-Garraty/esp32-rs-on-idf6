---
type: CrashReport
version: "1.0"
task_id: "manual"
title: "Boot loop: Panic handler entered multiple times — uart_write_bytes before UART driver install"
description: "uart_write_bytes(UART_NUM_0) in logVprintf called before uart_driver_install, causing LoadProhibited in panic handler with 'Panic handler entered multiple times'"
tags: [crash, boot-loop, uart, panic-handler, LL-026]
timestamp: "2026-07-12"
crash_signature: "Boot loop: 'Panic handler entered multiple times' — uart_write_bytes in logVprintf before UART driver installed"
---

# Crash Report

## Verdict

- **Status:** root_cause_found
- **Root Cause:** `uart_write_bytes(UART_NUM_0, ...)` called inside `logVprintf` (ESP_LOG custom vprintf) before `uart_driver_install()` was called. The UART driver context pointer (`p_uart_obj[UART_NUM_0]`) was NULL, causing a LoadProhibited exception inside the panic handler wrapper, producing only "Panic handler entered multiple times".
- **Confidence:** high

## Evidence Chain

### Step 1: Triage

- **Log:** `/home/vlabe/Documents/esp32-rs-on-idf6/logs/serial_2026-07-12_10-09-06.log`
- **Symptom:** `BOOT OK` → ~50ms → `Panic handler entered multiple times. Abort panic handling. Rebooting ...` — repeated in boot loop
- **No crash dump** — no `exccause`, `BACKTRACE`, `BLACK BOX`, or registers visible
- `rst:0xc (RTC_SW_CPU_RST)` — software CPU reset after panic handler abort
- `Saved PC:0x403848c0` — ROM panic vector (secondary crash address)
- **Known-good log:** `/home/vlabe/Documents/esp32-rs-on-idf6/logs/serial_2026-07-12_06-55-25.log` (commit `764c024`, pre-`6168bfa`) — boots normally

### Step 2: S1–S5 Protocol

| Step | Result | Action |
|------|--------|--------|
| S1 (stack watermark) | 107488 bytes used of ~131072 total (~18%) | ✅ Stack OK — NOT overflow |
| S2 (heap integrity) | Not needed (S1 ruled out stack) | ✅ |
| S3 (smoke test) | Minimal logVprintf (`return 0`) boots OK | ✅ Crash is inside logVprintf |
| S4 (delta analysis) | `6168bfa` vs `764c024`: BOOT OK string change + CONFIG_HTTPD_TASK_STACK_SIZE removed | 🟡 Neither caused crash |
| S5 (red flags) | `uart_write_bytes` added in commit `2a20158` calls UART driver before `uart_driver_install()` | 🔴 Root cause |

### Step 3: Elimination

1. **Hypothesis: stack overflow** → Disproved by S1 (watermark 107488 bytes, only ~18% used)
2. **Hypothesis: .iram1 wrapper calls flash (LL-025)** → Disproved — `.iram1` was already removed in commit `9df9774`
3. **Hypothesis: LogBuffer::push crashes** → Disproved — crash reproduced with LogBuffer::push disabled
4. **Hypothesis: sdkconfig change (CONFIG_HTTPD_TASK_STACK_SIZE removal)** → Disproved — crash reproduced with config restored
5. **Hypothesis: vsnprintf crashes** → Disproved — `vsnprintf` only test boots successfully
6. **Hypothesis: fwrite/fflush vs uart_write_bytes** ✅ **CONFIRMED**
   - With `fwrite(buf, 1, n, stdout); fflush(stdout);`: **BOOTS OK**
   - With `uart_write_bytes(UART_NUM_0, buf, n);`: **CRASHES**

### Step 4: Root Cause

**Commit `2a20158`** changed the serial output in `logVprintf` from `fwrite/fflush(stdout)` to `uart_write_bytes(UART_NUM_0, ...)`. 

The issue:
- `uart_write_bytes()` is part of the ESP-IDF **UART driver** (`esp_driver_uart`)
- The UART driver requires `uart_driver_install()` to be called before use, which allocates `p_uart_obj[port]`
- At boot time, before `app_main()` calls `SerialReader::init()` (Step 4), the UART driver for UART_NUM_0 is **not installed**
- `fwrite/fflush(stdout)` uses the **VFS layer** which calls `uart_tx_chars()` — a HAL-level function that writes directly to UART FIFO without needing the driver
- When `uart_write_bytes()` is called with a NULL `p_uart_obj[UART_NUM_0]`, it dereferences a NULL pointer → **LoadProhibited** → Panic handler → "Panic handler entered multiple times"

**Why no crash dump appears:**
The `__wrap_esp_panic_handler` calls `printf()` which also goes through VFS → UART HAL (works). But the output is queued while interrupts are masked (`rsil a0, 3`), never transmitted. `__real_esp_panic_handler` detects the recursive entry and prints "Panic handler entered multiple times" using `panic_print_str` (direct UART FIFO polling).

## Fix

### Trivial Fix Applied

Changed `uart_write_bytes(UART_NUM_0, ...)` back to `fwrite(buf, 1, n, stdout); fflush(stdout);` in `logVprintf()`.

### Files Modified

- `main/main.cpp`: Reverted `uart_write_bytes` → `fwrite/fflush` in `logVprintf()`, removed unused `#include "driver/uart.h"`, added LL-026 comment explaining the fix

### Verification

1. `scripts/idf.sh build` — 0 errors, 0 new warnings
2. `scripts/monitor.py` — 30s smoke test: **NO "Panic handler entered multiple times"**, boot proceeds to WiFi AP + STA + HTTP + BLE init
3. Pre-existing RTC WDT reset (~6s after boot at GPIO27 init) is a separate issue (LL-027: PSRAM data bus conflict)

## Investigation Artifacts

| File | Status |
|------|--------|
| `main/main.cpp` `[INVESTIGATION]` markers | ✅ Removed |
| Lessons learned | ✅ LL-026 added |

## Remaining Issues

- `rst:0x9 (RTCWDT_SYS_RST)` ~6s after boot at GPIO init (StepperMotor EN pin 27 on PSRAM D3 data bus) — pre-existing LL-027 issue, not related to this fix
