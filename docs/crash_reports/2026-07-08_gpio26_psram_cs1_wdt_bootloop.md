---
type: CrashReport
title: GPIO26 PSRAM CS1 gpio_set_direction Hang — WDT Boot Loop
description: >
  gpio_set_direction(GPIO_NUM_26) hangs because GPIO26 is PSRAM CS1
  (MSPI_IOMUX_PIN_NUM_CS1). IOMUX reconfiguration conflicts with MSPI
  controller, causing system hang → IWDT → TG1WDT_SYS_RST reset loop.
tags: [crash, gpio, psram, wdt, esp32-s3]
version: "1.0"
task_id: "manual"
timestamp: "2026-07-08"
crash_signature: "rst:0x8 (TG1WDT_SYS_RST) at gpio_set_direction(GPIO_NUM_26) in StepperMotor ctor"
---

# Crash Report: GPIO 26 (PSRAM CS1) `gpio_set_direction` Hang → WDT Boot Loop

## Verdict

- **Status:** root_cause_found
- **Root Cause:** `gpio_set_direction(GPIO_NUM_26, GPIO_MODE_OUTPUT)` hangs because GPIO 26 is the MSPI CS1 pin for octal PSRAM. The GPIO driver's IOMUX reconfiguration conflicts with the MSPI controller, causing a complete system hang (interrupts disabled → IWDT fires → reset).
- **Confidence:** high

## Evidence Chain

### Step 1: Triage

**Crash signature:** `rst:0x8 (TG1WDT_SYS_RST)` — Timer Group 1 Watchdog (IWDT) reset.
**Timing:** ~1.35s after `gpio_set_direction` call.
**Determinism:** 100% deterministic — every boot, same crash point.
**Saved PC:** Varies (0x4037b6b4, 0x4038e858, etc.) — typical for WDT resets where PC is wherever the task was when the WDT fired.

**Log excerpt (repeated identically across 15+ iterations):**
```
DBG: StepperMotor ctor B
DBG: StepperMotor ctor B1 - before gpio_set_direction
~1.35s silence →
ESP-ROM:esp32s3-20210327
rst:0x8 (TG1WDT_SYS_RST),boot:0x8 (SPI_FAST_FLASH_BOOT)
```

### Step 2: S1–S5 Protocol

| Step | Result | Action |
|------|--------|--------|
| S1 (stack watermark) | Main: 120,592 bytes, Motor: 11,264 bytes | ✅ Pass — no stack overflow |
| S2 (heap integrity) | Check passed (no crash) | ✅ Pass — heap clean |
| S3 (smoke test) | Not needed (S1+S2 passed) | N/A |
| S4 (delta analysis) | `.bss`/`.text` sizes normal | ✅ Pass — no memory pressure |
| S5 (red flags) | GPIO 26 = `MSPI_IOMUX_PIN_NUM_CS1` | 🚨 **CRITICAL** — PSRAM pin conflict |

### Step 3: Elimination

**Technique A: GPIO pin isolation test**
- `gpio_set_direction(GPIO_NUM_14, GPIO_MODE_OUTPUT)` — **WORKS** (valve pin, not on PSRAM bus)
- `gpio_set_direction(GPIO_NUM_26, GPIO_MODE_OUTPUT)` — **HANGS** (DIR pin, PSRAM CS1)
- `gpio_set_direction(GPIO_NUM_27, GPIO_MODE_OUTPUT)` — **HANGS** (EN pin, PSRAM HD) — confirmed by existing code comment

**Technique B: Task isolation test**
- `gpio_set_direction(GPIO_26)` from main task — **HANGS**
- `gpio_set_direction(GPIO_26)` from motor task — **HANGS**
- Conclusion: not task-specific, it's GPIO 26-specific

**Technique C: RgbLed isolation test**
- With RgbLed constructor — **HANGS**
- Without RgbLed constructor — **HANGS**
- Conclusion: RgbLed is not the trigger; GPIO 26 is always broken

### Step 4: Root Cause

**GPIO 26 is `MSPI_IOMUX_PIN_NUM_CS1`** — the Chip Select 1 pin for the octal PSRAM on ESP32-S3.

From `components/esp_hal_gpspi/esp32s3/include/soc/spi_pins.h`:
```c
#define MSPI_IOMUX_PIN_NUM_CS1      26
```

From `components/esp_psram/esp32s3/esp_psram_impl_octal.c`:
```c
#define OCT_PSRAM_CS1_IO            MSPI_IOMUX_PIN_NUM_CS1  // = 26
```

The PSRAM initialization (before `app_main`) configures GPIO 26 as the MSPI CS1 pin via the IOMUX and reserves it with `esp_gpio_reserve()`. When `gpio_set_direction(GPIO_26, GPIO_MODE_OUTPUT)` is called, the GPIO driver:

1. Calls `gpio_hal_matrix_out_default()` → writes to `func_out_sel_cfg[26].func_sel` (GPIO matrix)
2. Calls `gpio_hal_output_enable()` → sets output enable bit
3. Calls `gpio_hal_func_sel()` → writes `PIN_FUNC_SELECT(IO_MUX_GPIO26_REG, PIN_FUNC_GPIO)` (IOMUX)

Step 3 tries to change the IOMUX function from MSPI CS1 to GPIO, which conflicts with the active MSPI controller. This causes a **bus lockup** that disables interrupts, leading to the IWDT firing after 500ms.

**Why the OLD code appeared to work:** The OLD code called `ensureGpioReady()` (1s delay) after BLE init. BLE init's PHY calibration may have indirectly changed the MSPI controller's state, making GPIO 26 safe to reconfigure. In the NEW code, BLE init is deferred to net_owner (which never runs because the motor task crashes first), so the PHY calibration never happens, and GPIO 26 remains in its default PSRAM-controlled state.

**Also affected:** GPIO 27 (`MSPI_IOMUX_PIN_NUM_HD`) — the EN pin. The existing code already has a comment: `// gpio_set_direction(enPin_, GPIO_MODE_OUTPUT);  // HANGS with BT enabled`.

## Fix

### Complex Fix (requires hardware change)

**Root cause fix:** Change the DIR and EN pins from PSRAM-conflicting GPIOs to safe GPIOs.

| Signal | Current Pin | Problem | Recommended Pin |
|--------|-------------|---------|-----------------|
| DIR | GPIO 26 | PSRAM CS1 (`MSPI_IOMUX_PIN_NUM_CS1`) | GPIO 5 (available on J1 pin 5) |
| EN | GPIO 27 | PSRAM HD (`MSPI_IOMUX_PIN_NUM_HD`) | GPIO 6 (available on J1 pin 6) |

**Files to Modify:**
- `components/infrastructure/include/infrastructure/config.hpp`: Change `PIN_DIR` and `PIN_EN`
- Hardware: Rewire TMC2209 DIR from GPIO 26 to GPIO 5, EN from GPIO 27 to GPIO 6

### Workaround (software-only, no hardware change)

Since `gpio_set_level()` works on PSRAM pins (it only writes to the output register, not the IOMUX), the EN pin (GPIO 27) can remain as-is. Only the DIR pin (GPIO 26) needs to change because `gpio_set_direction()` is called on it.

However, `gpio_set_direction()` is called on DIR to configure it as output. If the pin is already configured as output by the bootloader (which it isn't — it's configured as PSRAM CS1), this would work. Since it's not, the pin must be changed.

### Verification

After changing the pins:
1. Build, flash, monitor — should boot past StepperMotor constructor
2. Verify `gpio_set_level` on new DIR pin works
3. Verify motor homing works

## Investigation Artifacts

| File | Status |
|------|--------|
| `[INVESTIGATION]` markers | ✅ Removed |
| Lessons learned | ✅ LL-027 added |

## Remaining Issues

- GPIO 27 (EN pin) is also a PSRAM pin (`MSPI_IOMUX_PIN_NUM_HD`). Currently, `gpio_set_direction` is NOT called on it (the call is commented out). Only `gpio_set_level` is used, which works. If `gpio_set_direction` is ever needed on EN, it must be moved to a non-PSRAM pin.
- The `ensureGpioReady()` function (1s PHY delay) may have been masking this issue. Its removal in the NEW code exposed the pre-existing GPIO 26 conflict.