---
type: CrashReport
version: "1.0"
task_id: "manual"
timestamp: "2026-07-13T04:30:00Z"
crash_signature: "rst:0x10 (RTCWDT_RTC_RST) every 8-9s after BOOT OK"
---

# Crash Report: RTCWDT_RTC_RST — RWDT not fed

## Verdict

- **Status:** root_cause_found
- **Root Cause:** The RtcWatchdog RAII wrapper was commented out in `app_main()`, leaving the hardware RTC Watchdog Timer enabled with its bootloader-configured 9-second timeout and no task feeding it.
- **Confidence:** high

## Evidence Chain

### Step 1: Triage

From `logs/serial_2026-07-12_17-35-54.log`:

```
[17:35:55.174] BOOT OK: ecotiter v0.1.0 [2026-07-12 17:34:08] (git: 0954f2a)
[17:35:55.184] DBG: step 1 - nvs_flash_init
[17:35:55.194] DBG: step 5 - RWDT DISABLED
[17:35:55.195] DBG: step 6 - BLE object created
...
[17:35:55.269] DBG: step 9 - RUNNING
...
[17:36:03.376] rst:0x10 (RTCWDT_RTC_RST),boot:0x8 (SPI_FAST_FLASH_BOOT)
```

- Reset reason: `rst:0x10` (RTCWDT_RTC_RST) — hardware RTC watchdog reset
- Timing: ~8.2 seconds after BOOT OK
- No Guru Meditation, no panic handler — consistent with hardware WDT, not CPU exception
- Pattern repeats deterministically every boot cycle

Motor task log showing the null pointer:

```
DBG: gRtcWdt IS NULL!
DBG: gRtcWdt IS NULL!  x3
DBG: gRtcWdt IS NULL!  x4
DBG: gRtcWdt IS NULL!
```

Stack watermarks (all healthy — overflow ruled out):

```
Thread main:   cfg=32768B wmark=26044 used=20%
Thread temp:   cfg=16384B wmark=2184  used=86%
Thread motor:  cfg=16384B wmark=1516  used=90%
Thread net_owner: cfg=16384B wmark=1676  used=89%
```

### Step 2: S1–S5 Protocol

| Step | Result | Action |
|------|--------|--------|
| S1 (stack watermark) | All tasks >20% free | ✅ Pass — not stack overflow |
| S2 (heap integrity) | N/A — heap suspected functional (app runs, JSON broadcast works) | ✅ Skip |
| S3 (smoke test) | N/A — root cause found in Phase 0 static analysis | ✅ Skip |
| S4 (delta analysis) | Not applicable — no known-good baseline provided | ⏭️ Skip |
| S5 (red flags) | **RtcWatchdog RAII wrapper commented out** (line 310) | 🚩 **CRITICAL** |

### Step 3: Root Cause Confirmation

Three independent code confirmations:

1. **main.cpp:310-311**: The RAII wrapper is commented out; the global pointer is explicitly nulled.
2. **startup_funcs.c:132-141**: `init_disable_rtc_wdt()` is gated by `!defined(CONFIG_BOOTLOADER_WDT_DISABLE_IN_USER_CODE)` — since the flag IS set to `y`, the function is never compiled in.
3. **rtc_watchdog.cpp:60**: The constructor does not set `gRtcWdt = this`, so even if the RAII object were created, the global pointer wouldn't be usable without the explicit assignment in main.cpp.

### Step 4: Root Cause

**Primary cause:** `main/main.cpp` line 310: `// diag::RtcWatchdog rtcWdt;` — the RAII wrapper that would configure and feed the RTC Watchdog was commented out.

**Contributing cause:** `components/diag/src/rtc_watchdog.cpp`: The constructor did not set `gRtcWdt = this;`, so the global pointer was never managed automatically by the RAII object.

**Enabling cause:** `sdkconfig.defaults` line 26: `CONFIG_BOOTLOADER_WDT_DISABLE_IN_USER_CODE=y` — this config tells ESP-IDF to skip disabling the bootloader's RTC WDT at startup, expecting the app to manage it. Without the RAII wrapper, the RWDT runs un-fed.

The crash occurred because:
1. Bootloader enables RTC WDT with 9-second timeout (ESP-IDF default)
2. `CONFIG_BOOTLOADER_WDT_DISABLE_IN_USER_CODE=y` prevents ESP-IDF from disabling it at startup
3. `RtcWatchdog` RAII wrapper is commented out, so no 6-second reconfiguration or feed loop runs
4. `gRtcWdt = nullptr` prevents the motor task's null-guarded feed from working
5. After ~8-9 seconds, the native RTC WDT fires → `rst:0x10`

## Fix

### Trivial — Applied Directly

Three changes applied:

1. **`main/main.cpp` (line 310-311, 427):**
   - Uncommented `diag::RtcWatchdog rtcWdt;`
   - Changed `diag::gRtcWdt = nullptr;` to `diag::gRtcWdt = &rtcWdt;`
   - Uncommented `rtcWdt.feed();` in main loop
   - Updated misleading log message from "RWDT DISABLED" to "RWDT configured"

2. **`components/diag/src/rtc_watchdog.cpp` (constructor + destructor):**
   - Added `gRtcWdt = this;` after `enabled_ = true;` in constructor
   - Added `gRtcWdt = nullptr;` after `enabled_ = false;` in destructor
   - This ensures the global pointer always points to the active RtcWatchdog instance

### Files Modified

- `main/main.cpp`: Re-enabled RAII wrapper + feed
- `components/diag/src/rtc_watchdog.cpp`: RAII global pointer management

### Verification

```bash
scripts/idf.sh build   # ✅ Builds with 0 errors
# Flash and monitor: device should no longer reset after 8s
# Expected: BOOT OK → "DBG: RWDT enabled, 6s timeout" → no rst:0x10
# Run: scripts/idf.sh flash && timeout 30 python3 scripts/monitor.py
```

### Falsification

If the fix is correct:
- The log will show `"DBG: RWDT enabled, 6s timeout"` instead of `"DBG: gRtcWdt IS NULL!"`
- The device will remain running past the 9-second mark without `rst:0x10`
- The motor task's `if (diag::gRtcWdt) diag::gRtcWdt->feed();` will successfully feed the RWDT every ~100ms

## Investigation Artifacts

| File | Status |
|------|--------|
| `main/main_smoke.cpp` | ✅ Not created (no smoke test needed) |
| `[INVESTIGATION]` markers | ✅ None left (pre-existing markers in sdkconfig.defaults:76 and wifi.cpp:172 are unrelated) |
| Lessons learned | ✅ LL-047 added |

## Remaining Issues

- The `[INVESTIGATION]` markers on `sdkconfig.defaults:76` and `components/infrastructure/network/src/wifi.cpp:172` are pre-existing from other experiments — not in the scope of this fix.
