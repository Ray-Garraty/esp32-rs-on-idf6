---
type: ESP32 Reference
title: GPIO Pins Specification — ESP32-S3
description: >
  Complete reference of all GPIO pins on ESP32-S3 with Octal PSRAM: project
  pinout, unsafe PSRAM/Flash bus pins, strapping pins, USB-JTAG, system UART,
  and safe alternatives.
tags: [esp32-s3, gpio, psram, hardware, pinout, safety]
timestamp: 2026-07-13
---

# GPIO Pins Specification — ESP32-S3

> **Single source of truth** for GPIO restrictions on ESP32-S3 with Octal PSRAM (8 MB).
>
> Violations cause: system hang (IOMUX bus lockup), WDT reset, cache corruption,
> boot failure, or silent data corruption.

## Quick Reference: Project Pinout

| GPIO | Function | Driver | Constraint |
|------|----------|--------|-----------|
| 1 | U0TXD | Serial | **DO NOT TOUCH** |
| 3 | U0RXD | Serial | **DO NOT TOUCH** |
| 4 | pH electrode (ADC) | `adc_oneshot_read()` (ADC1_CH3) | 0–2900 mV range |
| 5 | TMC2209 DIR | `gpio_set_direction()` + `gpio_set_level()` | HIGH=CW; moved from GPIO26 (LL-027) |
| 6 | DS18B20 | OneWire bitbang (`gpio_config`/`gpio_set_level`/`gpio_get_level`) | 4.7k pull-up; moved from GPIO33 (LL-027) |
| 7 | Limit FULL | GPIO ISR pos-edge (`gpio_config` + `gpio_isr_handler_add`) | Burette full (LIQ_IN); moved from GPIO34 (LL-027) |
| 13 | TMC2209 EN | `gpio_set_direction()` + `gpio_set_level()` | Active LOW; safe GPIO; moved from GPIO27 (LL-027) |
| 14 | Valve | `gpio_set_direction()` + `gpio_set_level()` | LOW=input, HIGH=output |
| 15 | Limit EMPTY | GPIO ISR pos-edge (`gpio_config` + `gpio_isr_handler_add`) | Burette empty (LIQ_OUT); moved from GPIO35 (LL-027) |
| 16 | TMC2209 UART RX | `uart_set_pin(UART_NUM_2)` | PDN_UART half-duplex |
| 17 | TMC2209 UART TX | `uart_set_pin(UART_NUM_2)` | PDN_UART half-duplex |
| 21 | TMC2209 STEP | RMT TX (channel 0) | Pulse train |
| 48 | Status LED | RMT TX (WS2812) | RGB via RMT — NOT `gpio_set_level` |

## 1. PSRAM / Flash Bus Pins (STRICTLY FORBIDDEN)

These pins are physically connected to the internal SPI flash and PSRAM chips via the
MSPI controller. **Any call to `gpio_set_direction()`, `gpio_config()`, or any function
that reconfigures the IOMUX on these pins will cause an immediate system hang**
(bus lockup → cache stall → IWDT → WDT reset).

`gpio_set_level()` is **safe** on these pins (writes GPIO OUT register only, no IOMUX touch).

| GPIO | MSPI Signal | Function | Project Usage |
|------|------------|----------|---------------|
| **26** | CS1 (PSRAM Chip Select) | PSRAM chip enable | ~~DIR (MOVED to GPIO5)~~ ✅ Fixed LL-027 |
| **27** | HD / D3 (Hold/Data 3) | PSRAM data bus | ~~EN (MOVED to GPIO13)~~ ✅ Fixed LL-027 |
| **28** | WP / D2 (Write Protect) | PSRAM data bus | Not used |
| **29** | D1 (Data 1) | PSRAM data bus | Not used |
| **30** | CLK (Clock) | PSRAM clock | Not used |
| **31** | D0 (Data 0) | PSRAM data bus | Not used |
| **32** | D0 / MISO | Flash data bus | Not used |
| **33** | D4 | PSRAM Octal data line 4 | ~~DS18B20 (MOVED to GPIO6)~~ ✅ Fixed LL-027 |
| **34** | D5 | PSRAM Octal data line 5 | ~~Limit FULL (MOVED to GPIO7)~~ ✅ Fixed LL-027 |
| **35** | D6 | PSRAM Octal data line 6 | ~~Limit EMPTY (MOVED to GPIO15)~~ ✅ Fixed LL-027 |
| **36** | D7 | PSRAM Octal data line 7 | Not used |
| **37** | DQS | PSRAM data strobe (Octal) | Not used |
| **38** | CS0 (Flash Chip Select) | Flash chip enable | Not used |

### Important Notes

- With `CONFIG_SPIRAM_MODE_OCT=y` (Octal PSRAM), **all of GPIO33-37** are actively used
  by the MSPI controller for the PSRAM data bus.
- `esp_gpio_reserve()` may NOT protect these pins if the bootloader detects Quad flash
  mode (only protects for Octal mode).
- `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y` means code may be executing from PSRAM —
  touching any of these pins crashes the CPU immediately.
- `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` means WiFi/LWIP buffers may be in PSRAM —
  touching these pins crashes on the next buffer access.

## 2. Strapping Pins (Boot Configuration)

These pins are sampled at reset to determine boot mode, voltage, and debug settings.
External pull-ups/pull-downs from peripherals can prevent boot.

| GPIO | Strapping Function | Constraint |
|------|-------------------|------------|
| **0** | Boot mode: HIGH=normal boot, LOW=UART download | Must be HIGH at boot. **Safe as output after boot.** |
| **3** | JTAG control | Strapping + UART RX. **DO NOT TOUCH if JTAG needed.** |
| **45** | VDD_SPI voltage: LOW=3.3V, HIGH=1.8V | **CRITICAL** — wrong level kills flash/PSRAM. Do not use. |
| **46** | UART log output: LOW=enabled, HIGH=disabled | **Safe as output** if log output preserved at boot. |

**Project usage:** None currently — all strapping pins are avoided.

## 3. USB-JTAG Pins

Reconfiguring these pins disables the built-in USB-JTAG interface.

| GPIO | Function | Constraint |
|------|----------|------------|
| **19** | USB_DP (USB D+) | Reconfiguring disables USB-JTAG debug |
| **20** | USB_DN (USB D-) | Reconfiguring disables USB-JTAG debug |

**Project usage:** Not used (WiFi/HTTP/BLE only, no USB host/peripheral).

## 4. System UART Pins (Console)

| GPIO | Function | Constraint |
|------|----------|------------|
| **1** | U0TXD (UART TX) | System console output. **DO NOT TOUCH.** |
| **3** | U0RXD (UART RX) | System console input + strapping. **DO NOT TOUCH.** |

**Project usage:** Serial JSON command/response + debug logs.

## 5. Current Project Pin Audit

| Pin | Usage | API Used | Category | Risk | Status |
|-----|-------|----------|----------|------|--------|
| 1 | UART TX | `uart_driver_install()` default | UART system | None | ✅ OK |
| 2 | Status LED (legacy) | Never instantiated | Safe | None | ✅ OK |
| 3 | UART RX | `uart_driver_install()` default | UART system / strapping | None | ✅ OK |
| 4 | pH electrode ADC | `adc_oneshot_read()` | Safe | None | ✅ OK |
| **5** | **TMC2209 DIR** | `gpio_set_direction()` + `gpio_set_level()` | **Safe** | **None** | **✅ Fixed (was GPIO26)** |
| **13** | **TMC2209 EN** | `gpio_set_direction()` + `gpio_set_level()` | **Safe** | **None** | **✅ Moved from GPIO27** |
| 14 | Valve | `gpio_set_direction()` + `gpio_set_level()` | Safe | None | ✅ OK (not instantiated yet) |
| 21 | TMC2209 STEP | `rmt_new_tx_channel()` | Safe | None | ✅ OK |
| **6** | **DS18B20** | **`gpio_config()`** | **Safe** | **None** | **✅ Fixed (was GPIO33)** |
| **7** | **Limit FULL** | **`gpio_config()`** | **Safe** | **None** | **✅ Fixed (was GPIO34)** |
| **15** | **Limit EMPTY (LIQ_OUT)** | **`gpio_config()`** | **Safe** | **None** | **✅ Fixed (was GPIO35)** |
| 48 | RGB LED | `rmt_new_tx_channel()` | Safe | None | ✅ OK |

## 6. Safe GPIO Pins (Recommended)

These pins are NOT on PSRAM bus, NOT strapping, NOT UART, and NOT USB-JTAG.
Suitable for any function including `gpio_set_direction()`, `gpio_config()`, ISR, RMT, ADC.

| GPIO Range | Notes |
|-----------|-------|
| **5–13** | Fully safe. GPIO5 used for DIR. GPIO11-13 can be ADC2. |
| **15–18** | Fully safe. |
| **21** | Used for STEP (RMT). |
| **38–47** | Safe. GPIO38 is CS0 but accessible if flash is in legacy mode. GPIO45-46 are strapping but safe as output AFTER boot. |

### Relocation Completed

The following pins have been moved to safe GPIOs:

| Old Pin | Function | New Pin | Reason |
|---------|----------|---------|--------|
| GPIO27 | TMC2209 EN (gpio_set_level only) | **GPIO13** | PSRAM HD/D3 — `gpio_set_direction()` hangs |
| GPIO33 | DS18B20 (OneWire bitbang) | **GPIO6** | PSRAM D4 — `gpio_config()` hangs |
| GPIO34 | Limit FULL (ISR pos-edge) | **GPIO7** | PSRAM D5 — `gpio_config()` hangs |
| GPIO35 | Limit EMPTY (ISR pos-edge) | **GPIO15** | PSRAM D6 — `gpio_config()` hangs |

## 7. Root Cause Reference

- **LL-027**: `gpio_set_direction(GPIO_NUM_26)` → system hang → TG1WDT_SYS_RST.
  GPIO26 is `MSPI_IOMUX_PIN_NUM_CS1` (PSRAM Chip Select).
- **LL-027** also covers GPIO27-37 — all PSRAM bus pins with the same restriction.
