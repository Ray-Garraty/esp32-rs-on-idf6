---
type: ESP32 Reference
title: "Protocol: Boot Loop — Flash/Image Integrity"
description: >
  F1–F4 protocol for ESP32 boot loops where the app image is corrupt,
  missing, or mismatched. Use when serial shows "invalid segment length
  0xffffffff", "Factory app partition is not bootable", or infinite
  rst:0x3 reboot cycle — cases where app_main() never executes.
tags: [esp32, debug, boot-loop, flash, f1-f4]
timestamp: 2026-07-11
---

# Protocol: Boot Loop — Flash/Image Integrity

## Trigger Markers

| Marker | Meaning |
|--------|---------|
| `esp_image: invalid segment length 0xffffffff` | Flash is erased/unprogrammed (all 0xFF) or corrupt |
| `E (262) boot: Factory app partition is not bootable` | Bootloader rejected the app image header |
| `E (262) boot: No bootable app partitions in the partition table` | No valid app partition found |
| `rst:0x3 (RTC_SW_SYS_RST)` | Software reset — bootloader resets after failure, creating a loop |
| Repeating pattern of the above ≈ every 2s | Confirmed boot loop |

**Distinction from runtime crashes:**
- Boot loop: app image never loads → `app_main()` NEVER executes
- Runtime crash: app loads and starts, then crashes inside `app_main()` or a task

For runtime crashes, see `embedded_boot_crash.md` (S1–S5 protocol).

## F1–F4 Protocol

Execute in order. Each step narrows the root cause.

### F1: Verify Build Artifact Exists

Check that the binary was actually built:

```bash
ls -la build/ecotiter.bin
```

**Decision:**
- **File not found** → build never ran or failed. Run F2.
- **Size is 0 or tiny (<1 KB)** → build produced a stub/partial binary. Run F2.
- **Size >100 KB** → build output looks plausible. Skip to F3.

### F2: Clean Build

```bash
scripts/idf.sh build
```

`scripts/idf.sh build` always removes `build/` and `sdkconfig` first, forcing
CMake to regenerate from `sdkconfig.defaults`. This catches stale config
mismatches that a cached build would silently hide.

**Decision:**
- **Build fails** → compilation/linker error. Route to @implementer for fix.
- **Build succeeds** → binary produced. Proceed to F3.

### F3: Verify Partition Table Scheme

Check that the partition scheme in `sdkconfig.defaults` matches what the
bootloader expects:

```bash
grep CONFIG_PARTITION_TABLE sdkconfig.defaults
```

Common values:
- `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y` — single factory app at
  offset 0x10000, size 1500 KB (used by this firmware)
- `CONFIG_PARTITION_TABLE_SINGLE_APP=y` — single factory app at offset
  0x10000, size 1 MB
- `CONFIG_PARTITION_TABLE_CUSTOM=y` + `partitions.csv` — custom layout

**Decision:**
- **Scheme changed** → if the chip was previously flashed with a different
  partition table, erase the entire flash:
  ```
  esptool.py --port /dev/ttyUSB0 erase_flash
  scripts/idf.sh flash
  ```
- **Scheme unchanged** → proceed to F4.

### F4: Flash and Verify Write

Flash the device and watch for esptool errors:

```bash
scripts/idf.sh flash
```

**What to look for in esptool output:**
- `Writing at 0x00010000...` — app is being written at the factory offset
- `Hash of data verified.` — flash verification passed
- `Leaving... Hard resetting` — chip resetting after flash

**Error patterns:**
```
A fatal error occurred: Failed to connect to ESP32-S3
```
→ Serial port issue, wrong port, or chip in bad state. Try:
  - Hold BOOT button during connect
  - Check `/dev/ttyUSB0` exists
  - Try different USB cable/port

```
A fatal error occurred: Timed out waiting for packet header
```
→ Flash speed issue or hardware problem. Try: `CONFIG_ESPTOOLPY_FLASHSPEED_40M`.

**Decision:**
- **Flash succeeds, device boots** → root cause was unprogrammed/corrupt flash.
  Resolution: re-flash.
- **Flash succeeds, still loops** → possible partition mismatch or hardware
  issue. Try: `scripts/idf.sh erase_flash && scripts/idf.sh flash`.
- **Flash fails** → hardware issue (serial, power, flash chip). Escalate to
  human.

### F-extra: Full Flash Erase + Reprogram

If F1–F4 did not resolve:

```bash
esptool.py --port /dev/ttyUSB0 erase_flash
scripts/idf.sh build
scripts/idf.sh flash
```

This wipes ALL data including bootloader, partition table, and NVS, then
re-writes everything from scratch. Use when the flash contains stale/corrupt
data from a different firmware or partition layout.

## Post-Resolution: Verify Boot

After successful flash, confirm the device boots into the app:

```bash
timeout 15 python3 scripts/monitor.py
```

Look for:
- Application startup log messages
- `app_main()` executing
- No boot failure markers

## Root Cause Categories

| Category | Description | F-step that identifies |
|----------|-------------|----------------------|
| `unprogrammed_flash` | Flash was erased/blank, no app written | F1 + F4 resolve |
| `corrupt_image` | Binary built but corrupted during flash | F1 (binary exists) + F4 (flash errors) |
| `partition_mismatch` | sdkconfig partition scheme changed | F3 |
| `build_failure` | Compilation/linker error, no binary produced | F1 (file missing) + F2 (build fails) |
| `stale_sdkconfig` | Stale `sdkconfig` masks changed `sdkconfig.defaults` | F2 (clean build resolves) |
| `hardware` | Flash chip, power, serial, or connection issue | F4 (flash fails) |

## Time Budget

| Step | Time |
|------|------|
| F1 (check binary) | 1 min |
| F2 (clean build) | 5 min |
| F3 (partition check) | 1 min |
| F4 (flash + verify) | 3 min |
| **Total** | **≤ 10 min** |

## References

- `docs/protocols/embedded_boot_crash.md` — runtime crash protocol (app executes)
- `.opencode/agents/debugger.md` — debugger agent configuration
- `sdkconfig.defaults` — partition table config
- `scripts/idf.sh` — build and flash wrapper
- `AGENTS.md` — build commands, golden rules
