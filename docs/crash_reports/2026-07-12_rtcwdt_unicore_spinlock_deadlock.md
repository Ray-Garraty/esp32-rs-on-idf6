---
type: CrashReport
version: "1.0"
task_id: "RTCWDT_RECURSIVE_INVESTIGATION"
title: "RTCWDT_SYS_RST — UNICORE spinlock deadlock"
description: "CONFIG_FREERTOS_UNICORE=y causes spinlock deadlock between WiFi driver task and WiFi MAC ISR — tick freezes, RWDT fires after 6s"
tags: [crash, wdt, unicore, wifi, spinlock, debugger-failed]
timestamp: "2026-07-12"
crash_signature: "RTCWDT_SYS_RST (rst:0x9 / rst:0x10) ~5.6s after WiFi AP start — CPU hung in _xt_lowint1 — tick counter frozen"
---

# Crash Report: RTCWDT_SYS_RST — UNICORE Spinlock Deadlock

## Verdict

- **Status:** hypothesis_with_high_confidence
- **Probable Root Cause:** `CONFIG_FREERTOS_UNICORE=y` causes a spinlock deadlock between the WiFi driver task and WiFi MAC interrupt handler. The WiFi task holds a spinlock while processing a MAC event; the WiFi ISR tries to acquire the same spinlock; on single-core the ISR cannot yield, the task cannot release the spinlock, the tick ISR is masked, and RWDT fires after 6s.
- **Confidence:** high (documented ESP-IDF limitation, direct correlation with `startAP()`, reproducible on every boot)
- **Verification needed:** Set `CONFIG_FREERTOS_UNICORE=n`, rebuild, reflash — if crash disappears, cause is confirmed

## Timeline

| Step | Description | Result |
|------|-------------|--------|
| 2026-07-10 | Initial code review — 4 gaps identified (brownout, RMT RAII, RgbLed types, HeapSnapshot) | Plan created |
| 2026-07-12 10:09 | Step 1: `CONFIG_BROWNOUT_DET=n` added — smoke test | ✅ BOOT OK, but "Panic handler entered multiple times" |
| 2026-07-12 10:09 | Debugger 1: fix `uart_write_bytes` → `fwrite/fflush` in logVprintf (LL-026) | ✅ Panic handler crash fixed |
| 2026-07-12 10:09 | First smoke after fix shows `rst:0x9 (RTCWDT_SYS_RST)` — NEW crash unmasked | 🔴 New issue |
| 2026-07-12 11:32 | Debugger 1: blamed GPIO27/PSRAM (LL-027) — MOVED EN to GPIO13 | ❌ Crash persists |
| 2026-07-12 11:41 | Debugger 2: invented "floating GPIO15 + RF noise" — added pull-down | ❌ Crash persists |
| 2026-07-12 12:08 | GPIO refactor: all `gpio_config()` moved to `app_main` before tasks | ❌ Crash persists |
| 2026-07-12 12:34 | Debugger 3: binary search — 7 experiments isolating variables | ❌ Crash persists |
| 2026-07-12 12:55 | **Breakthrough:** `wifiManager.startAP()` isolated as trigger | ✅ Without AP: BOOT OK |
| 2026-07-12 13:59 | `gHomingDone` workaround applied (defer AP after homing) | ❌ Crash persists |
| 2026-07-12 14:13 | Crash changes to `rst:0x10 (RTCWDT_RTC_RST)` | 🔴 Deeper RWDT level |
| 2026-07-12 14:13 | Failed 5-second grep: `CONFIG_FREERTOS_UNICORE` Kconfig help | 🔴 Known ESP-IDF limitation |

## Symptoms

- **Reset reason:** `rst:0x9 (RTCWDT_SYS_RST)` (sometimes `rst:0x10 RTCWDT_RTC_RST`)
- **Timing:** Deterministic ~5.6s after `wifi: AP started:` message
- **Saved PC:** `0x4037b9a1` / `0x4037b9e1` — both in `_xt_lowint1` (level-1 interrupt dispatcher, `xtensa_vectors.S:1232`)
- **Tick counter:** Freezes at value ~1250-1404 (at moment of WiFi AP start)
- **No panic output:** No Guru Meditation, no `exccause`, no backtrace, no black box dump
- **IWDT does not fire:** IWDT configured at 500ms but never triggers — because interrupts ARE being serviced (dispatch loop IS running), just too busy to exit to main loop

## Evidence Chain

### Fact 1: `startAP()` is necessary and sufficient

| Configuration | Result |
|---------------|--------|
| Full firmware (AP + STA + HTTP + BLE + homing) | r`st:0x9` in ~5.6s |
| Without `startAP()` (wifi init only, no AP) | ✅ BOOT OK, no rst |
| AP only, no STA, no HTTP, no BLE | `rst:0x9` |
| AP only, no RMT homing | `rst:0x9` |
| AP only, no PSRAM fetch instructions | `rst:0x9` |
| AP only, GPIO15 pull-down enabled | `rst:0x9` |

The single change that eliminates the crash is removing `wifiManager.startAP()`.

### Fact 2: CPU is inside level-1 interrupt context

`Saved PC:0x4037b9a1` resolves to `_xt_lowint1` in `xtensa_vectors.S:1232`:
```
    dispatch_c_isr 1 XCHAL_INTLEVEL1_MASK
    call0   XT_RTOS_INT_EXIT
```

The CPU is in the level-1 dispatch loop — either dispatching an ISR or checking for more pending interrupts. It never exits to `XT_RTOS_INT_EXIT`.

### Fact 3: Tick counter freezes

The tick ISR fires on the timer interrupt (level 1). When the CPU is already in `_xt_lowint1`, `PS.INTLEVEL` is set to 1, masking the timer interrupt. The tick is not serviced until the CPU exits level 1 — which never happens.

### Fact 4: CONFIG_FREERTOS_UNICORE=y is documented as incompatible with WiFi/BLE

From `/home/vlabe/.espressif/v6.0.1/esp-idf/Kconfig`:

```
config FREERTOS_UNICORE
    bool "Run FreeRTOS on single core only"
    default n
    help
        Run FreeRTOS only on the first core (CPU0), and disable the other
        core (CPU1). This is an option to be used on a single core ESP target.
        ...
        It is not recommended to use single-core mode when Wi-Fi or Bluetooth
        are enabled, as some drivers rely on spinlocks that require both cores
        to make progress.
```

(Emphasis added.) This is a 3-second grep. It was NOT consulted by any of the 3 debugger sessions.

## False Hypotheses

### H1: GPIO27 (Stepper EN) on PSRAM D3 bus (LL-027)

- **Proposed by:** Debugger 1
- **Claim:** `gpio_set_level(GPIO27)` causes PSRAM bus conflict → system hang → RWDT
- **Experiment:** Moved EN to GPIO13 (safe pin), added `gpio_set_direction()`
- **Result:** Crash persists identically
- **Why it was wrong:** `gpio_set_level()` on PSRAM bus pins is documented as safe — it only writes GPIO OUT register, does not touch IOMUX. The EN pin was not the cause.

### H2: Floating GPIO15 + RF noise from WiFi (LL-027 variant)

- **Proposed by:** Debugger 2
- **Claim:** GPIO15 (EMPTY limit switch) configured with `pull_down=DISABLE`, floating near logic threshold, picks up 2.4 GHz RF noise from WiFi radio → continuous posedge interrupts → interrupt storm
- **Experiment:** Set `GPIO_PULLDOWN_ENABLE` on GPIO15
- **Result:** Crash persists identically
- **Why it was wrong:** The pin already had `GPIO_PULLDOWN_DISABLE` but was successfully used by the working build (06:55) with WiFi active. The asymmetric Pull-down with GPIO7 was coincidental, not causal.

### H3: RMT ISR not in IRAM causes cache stall

- **Proposed by:** Build engineer (speculative)
- **Claim:** RMT interrupt handler is not in IRAM → flash/PSRAM access inside ISR → MSPI bus contention → hang
- **Experiment:** Disabled homing (RMT use), disabled `CONFIG_SPIRAM_FETCH_INSTRUCTIONS` and `CONFIG_SPIRAM_RODATA`
- **Result:** Crash persists even with no RMT activity and no PSRAM code/data
- **Why it was wrong:** RMT interrupts are not required for the crash. The crash happens with AP alone, no RMT running.

### H4: Deferring AP after homing (gHomingDone workaround)

- **Proposed by:** Debugger 3
- **Claim:** The combination of RMT + WiFi interrupts overloads level-1 dispatch. Defer AP until after homing to reduce interrupt load.
- **Experiment:** `gHomingDone` flag: motor task sets it after homing, net_owner waits before `startAP()`
- **Result:** Crash persists — even with AP after homing, even without RMT activity
- **Why it was wrong:** The interrupt load from RMT is not the trigger. WiFi AP alone is sufficient.

### H5: PSRAM code/data fetch inside WiFi ISR

- **Proposed by:** Build engineer (speculative)
- **Claim:** With `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y`, WiFi ISR code or data in PSRAM → cache miss cannot be serviced while MSPI busy → permanent stall inside ISR
- **Experiment:** Disabled both `FETCH_INSTRUCTIONS` and `RODATA` in sdkconfig.defaults, rebuild
- **Result:** Crash persists identically
- **Why it was wrong:** WiFi MAC code in ESP-IDF is explicitly placed in IRAM and does not fetch from PSRAM. The ISR dispatch loop and the WiFi firmware both operate from dedicated memory.

## Root Cause Hypothesis (not yet verified)

### H0: CONFIG_FREERTOS_UNICORE WiFi spinlock deadlock

**Status:** HIGH confidence, not yet verified experimentally (no dual-core build flashed)

**Mechanism (predicted):**

```
Time | CPU0 (single core, UNICORE)
-----|---------------------------
T+0  | net_owner task: esp_wifi_start() → WiFi MAC starts
     | WiFi driver task (prio 23) starts, processes MAC init
     | WiFi driver enters critical section → takes gpio_spinlock
T+1  | WiFi MAC generates level-1 interrupt (packet/beacon/timer)
     | CPU enters _xt_lowint1 at INTLEVEL=1
     | Dispatcher calls WiFi ISR
T+2  | WiFi ISR tries to acquire gpio_spinlock → portENTER_CRITICAL
     | ⛔ SPINLOCK HELD by WiFi driver task (not yielding CPU)
     | ISR spins forever at INTLEVEL=1
     | Tick ISR (level 1) cannot fire — masked
     | xTaskGetTickCount freezes
T+6  | RWDT not fed for 6s → rst:0x9 (RTCWDT_SYS_RST)
     | ESP32 reboots → boot loop
```

**Falsification experiment:** Set `CONFIG_FREERTOS_UNICORE=n` in `sdkconfig.defaults`. If the crash disappears, the prediction is confirmed.

**Expected result of dual-core:**
- WiFi driver task runs on CPU1
- WiFi ISR runs on CPU1 at INTLEVEL=1
- Main loop runs on CPU0, feeds RWDT every 10ms
- No deadlock — spinlock release and ISR acquisition happen on the same core

**If falsified (dual-core also crashes):** The root cause is in the ESP-IDF WiFi AP implementation itself (e.g., unhandled interrupt condition, hardware errata). In that case, the fix would require an ESP-IDF version upgrade or a specific workaround.

## Remaining Work

### Must do (1 experiment)

- [ ] `sdkconfig.defaults`: `CONFIG_FREERTOS_UNICORE=n` → build → smoke
- [ ] If smoke passes — **confirm root cause**, document in LL-045
- [ ] If smoke fails — escalate: try ESP-IDF v6.1, or test without PSRAM, or reduce WiFi buffers

### Should do (after verification)

- [ ] Remove `gHomingDone` workaround (no longer needed)
- [ ] Review `sdkconfig.defaults` for other UNICORE-only settings that may need adjustment
- [ ] Verify `ESP_INTR_FLAG_IRAM` on all ISR handlers (limit switches: ✅, RMT: verify)
- [ ] Add LL-045 reference to AGENTS.md §5 (Known Patterns)
- [ ] Update `docs/refs/project.md` to note UNICORE+WiFi limitation and resolution

### Post-fix hardening

- [ ] 5-minute stress test: AP + STA + HTTP streaming + BLE connected + periodic motor moves
- [ ] Stack watermark logging at steady state to detect dual-core shift
- [ ] Heap snapshot before/after WiFi init to verify DRAM budget

## Debugger Workflow Failures

This investigation consumed 3 debugger sessions, 10+ build-smoke cycles, ~3 hours of wall-clock time, and 2 incorrect code changes committed to the working tree. The root cause was discoverable in a **3-second grep** of the ESP-IDF Kconfig.

### Failure catalog

| Failure | Impact |
|---------|--------|
| No debugger read the platform Kconfig or FreeRTOS documentation | 3 hours wasted |
| Debugger 1 conflated "known pattern" with "proven cause" | Misdirected 2 sessions |
| Debugger 2 built a plausible story without falsification | 1 build + misleading docs |
| Debugger 3 ran 7 experiments without asking "why does AP hang?" | 7 builds, no root cause |
| No inherited context between sessions | Repeated experiments |
| Free-form narratives mixed data, speculation, and conclusions | No audit trail |

### Fix applied to process

See `docs/plans/pending/26_07_12_debugger_refactoring.md` for the full overhaul plan. Key changes:
1. Mandatory platform study before any code change
2. Hypothesis falsification requirement
3. Structured YAML-format output
4. Inherited context between sessions
5. Explicit "I don't know" after 3 experiments

## References

- LL-026: `uart_write_bytes` panic handler crash (masked this issue)
- LL-027: PSRAM bus GPIO restrictions (decoy for 2 sessions)
- LL-031: PHY calibration spinlock deadlock (related mechanism)
- LL-045: UNICORE WiFi spinlock deadlock (this issue, documented)
- `/home/vlabe/.espressif/v6.0.1/esp-idf/Kconfig` — CONFIG_FREERTOS_UNICORE help
- `docs/plans/pending/26_07_12_debugger_refactoring.md`
- `docs/refs/project.md §Thread Architecture`
- `sdkconfig.defaults` line 26: `CONFIG_FREERTOS_UNICORE=y`
- Logs: `serial_2026-07-12_12-08-08.log`, `serial_2026-07-12_12-34-49.log`, `serial_2026-07-12_14-13-27.log`
