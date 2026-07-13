---
type: ESP32 Reference
title: "Protocol: Crash Triage"
description: >
  Quick-reference for ESP32 crash analysis: crash output format, canary detection,
  known pattern signatures, and triage commands.
tags: [esp32, crash, triage, debug, protocol]
timestamp: 2026-07-13
---

# Protocol: Crash Triage

## Crash Output Format

A full crash dump looks like this:

```
=== CRASH ===
exccause=20 name=InstrFetchProhibited excvaddr=0x00000000 pc=0x420e00ca
=== REGISTERS ===
a0=0x3fce53a0 a1=0x3fce53c8 a2=0x3fce5420 a3=0x00000000 a4=0x00000000
a5=0x00000000 a6=0x00000000 a7=0x00000000 a8=0x00000000 a9=0x00000000
a10=0x00000000 a11=0x00000000 a12=0x00000000 a13=0x00000000 a14=0x00000000 a15=0x00000000
ps=0x00060525 sp=0x3fce53a0
=== BLACK BOX (64 events, newest first) ===
[28412us] t1 FfiExit  { boundary: 20, result: 0 }
[28302us] t1 FfiEnter { boundary: 20 }
[17300us] t5 Error    { id: 7, value: 0 }
=== STACK ===
t1 main  watermark=25980 used=20%  t2 motor  watermark=1524 used=90% ...
```

**Parts:**
- `exccause=` — exception cause number (see table below)
- `excvaddr=` — address that triggered the exception
- `pc=` — program counter at crash point
- `a0..a15` — CPU registers (a1 = stack frame pointer)
- `BLACK BOX` — last 64 pre-crash events from ring buffer
- `STACK` — per-task stack watermarks (high used% = near overflow)

### Common Exception Causes

| Cause | Name | Meaning |
|-------|------|---------|
| 0 | IllegalInstruction | Corrupted PC, function pointer, or vtable |
| 6 | StoreProhibited | Write to invalid memory (common: NULL deref, freed handle) |
| 7 | LoadProhibited | Read from invalid memory (common: NULL ptr) |
| 9 | LoadStoreAlignment | Unaligned access (rare in ESP-IDF) |
| 20 | InstrFetchProhibited | Tried to execute code at invalid address (common: `0x00000000` = NULL call) |
| 28 | IntegerDivideByZero | `idiv` / `uidiv` with divisor = 0 |

## Stack Canary Detection

`0xa5a5a5a5` in a backtrace address = FreeRTOS stack canary overwritten.

```
0x40385516:0xa5a5a5a5  |<-CORRUPTED
```

**Action:** Check the task's stack watermark. If used > 85%, increase the task's stack size. This is a stack overflow, NOT heap corruption (despite `tlsf_check` errors often appearing nearby).

## Known Crash Signatures

| Signature | Real Cause | Fix |
|-----------|------------|-----|
| `A2=0xFFFFFFFC`, `tlsf_check`, `heap_caps_*free`, `0xa5a5a5a5` in backtrace | **Stack overflow**, NOT heap (LL-001) | Increase stack, check watermark FIRST |
| `ESP_ERR_HTTPD_TASK (45064)` | DRAM fragmentation — WiFi+BLE+HTTP exhausted contiguous MALLOC_CAP_INTERNAL | Follow GR-3 init order (WiFi → HTTP → BLE); reduce WiFi buffer counts |
| `wifi:fail to alloc timer, type=9` | WiFi timer allocation after BLE+HTTP consumed DRAM | Reduce `CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM` and `TX_BUFFER_NUM` |
| `StoreProhibited EXCVADDR=0x28` | Dangling `httpd_req_t*` stored across threads (GR-5 violation) | Use `httpd_ws_send_frame_async`; never store C opaque pointers |
| `IllegalInstruction` + `heap_caps_get_largest_free_block` = ~6 KB | DRAM fragment → HTTP server alloc fails silently | Keep event loop handle alive; review init order |
| GPIO init hangs on pins 26–37 | PSRAM/Flash bus conflict — pad mux locked by octal PSRAM (LL-027) | Move GPIOs to safe pins (5, 6, 7, 15); see `docs/refs/gpio_pins_spec.md` |
| `esp_phy_load` spinlock at boot | PHY calibration deadlock — WiFi/BLE PHY init races with GPIO ISR (LL-031) | Call `esp_phy_deinit()` before BLE init, `esp_phy_init()` after |
| `#error "WIFI_RX_BA_WIN > WIFI_DYNAMIC_RX_BUFFER_NUM"` | Stale `sdkconfig` — changed defaults not picked up | `scripts/idf.sh build` auto-removes stale build dir; ensure RX_BA_WIN ≤ DYNAMIC_RX_BUFFER_NUM |
| TWDT panic: `task_log_worker` did not reset TWDT | `log_worker` blocked > 10 s — usually calling network function from wrong task | GR-14 violation: `log_worker` must push to queue, not call `httpd_ws_send_frame_async` |

## Triage Pipeline

```
1. Capture crash log
   └─ monitor.py:     timeout 30 python3 scripts/monitor.py
   └─ existing log:   python3 scripts/crash_analyzer.py < logs/serial_*.log

2. Identify exception cause
   └─ exccause=6/7 → NULL pointer or freed handle
   └─ exccause=0/20 → corrupted function pointer or vtable
   └─ 0xa5a5a5a5 → stack overflow

3. Check stack watermarks (from STACK section or manually)
   └─ high used% → increase task stack

4. Check BlackBox events (last events before crash)
   └─ last FfiEnter boundary = where crash occurred
   └─ Error events = pre-crash warnings

5. Match against Known Signatures table above

6. If unresolved → check docs/lessons_learned/ for matching patterns
```

## Commands

| Task | Command |
|------|---------|
| Live serial capture | `timeout 30 python3 scripts/monitor.py` |
| Analyse existing log | `python3 scripts/crash_analyzer.py < logs/serial_*.log` |
| Check for core dump | `ls -la dumps/` |
| Search crash by keyword | `rg "exccause=\|CRASH\|Panic" logs/` |
| Check stack watermark (live) | `python3 -c "import serial; ..."` (see `scripts/monitor.py`) |

## References

- **Full diagnostic docs:** `docs/refs/diagnostic_spec.md` (pipeline, gaps, fixes)
- **Stack overflow protocol:** `docs/protocols/stack_overflow.md` (detailed step-by-step)
- **Heap corruption protocol:** `docs/protocols/heap_corruption.md` (often misdiagnosed)
- **Boot crash protocol:** `docs/protocols/embedded_boot_crash.md` (S1–S5 Occam's Razor)
- **Lessons learned:** `docs/lessons_learned/` (LL-001 through LL-048)
