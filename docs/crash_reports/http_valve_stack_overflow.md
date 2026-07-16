---
type: CrashReport
title: "HTTP valve POST handler stack overflow"
description: "Stack overflow in HTTP server task during POST /api/valve handler chain. Task stack was 12 KB, bumped to 16 KB (LL-050)."
tags: [crash, stack-overflow, http, valve, esp32-s3]
version: "1.0"
task_id: "manual"
timestamp: "2026-07-16"
crash_signature: "PC=0x403856da EXCVADDR=0xfffffec0 A2=0xa5a5a5a5 (post-reboot)"
---

# Crash Report

## Verdict

- **Status:** root_cause_found
- **Root Cause:** HTTP server task stack overflow during `POST /api/valve` handler chain
- **Confidence:** high

## Evidence Chain

### Step 1: Triage

Three consecutive crash dumps in `logs/http_serial_2026-07-16_06-31-09.log`:

1. **Original crash**: `exccause=29 StoreProhibited pc=0x403856da excvaddr=0xfffffec0` — in `vPortYieldFromInt` (FreeRTOS SMP scheduler, portasm.S:648). Writing to corrupted task list pointer.

2. **Secondary crash (panic handler)**: `exccause=28 LoadProhibited pc=0x420e7342 excvaddr=0x00000000` — `prvTaskCheckFreeStackSpace` at tasks.c:4811. "Panic handler entered multiple times" (LL-025 pattern — panic handler in IRAM calling flash functions).

3. **Post-reboot crash**: `exccause=28 LoadProhibited pc=0x40385cdd excvaddr=0x00000044`, **A2=0xa5a5a5a5** (FreeRTOS stack canary). Backtrace: `prvSelectHighestPriorityTaskSMP` → `vTaskSwitchContext` → `_frxt_dispatch` → `|<-CORRUPTED`. Then `rst:0x10 (RTCWDT_RTC_RST)`.

The `A2=0xa5a5a5a5` is a definitive stack overflow indicator (LL-001 pattern). Combined with the corrupted scheduler backtrace, the root cause is a stack overflow in a task running in the HTTP server context.

### Step 2: S1–S5 Protocol

| Step | Result | Action |
|------|--------|--------|
| S1 (stack watermark) | 27212 words free at app_main start | **Pass** — main task has ~109KB stack margin |
| S2 (heap integrity) | Not executed (runtime crash, not boot) | N/A |
| S3 (smoke test) | Boots OK, WiFi connects, BLE initializes | **Pass** — ESP-IDF init OK |
| S4 (delta analysis) | HTTP_SERVER_STACK=12288, existing config | Identified target |
| S5 (red flags) | New large ResponseBuffer (2048B) on stack in handler | **Flagged** — major stack consumer |

### Step 3: Elimination

**Technique A (Instrumentation):** Added `uxTaskGetStackHighWaterMark(nullptr)` at three points in the POST /api/valve handler chain:

```
valve_post_handler ENTER:       free=10684 bytes (14% used)
handleCommandCore ENTER:        free=5124 bytes  (59% used, Δ=5560)
handleSetPosition ENTER:        free=1700 bytes  (87% used, Δ=3424)
CRASH immediately after
```

**Stack consumption from handler entry to deepest call: 10684 - 1700 = 8984 bytes.**

The handler then calls `gValve.setPosition()`, `vTaskDelay(50ms)` (triggers FreeRTOS context switch), returns through multiple destructors, and calls `httpd_resp_send()`. With only **1700 bytes remaining** in a **12288-byte stack**, the overflow occurs during this return/delay path.

**Technique G (Backtrace decoding):**
- `0x403856da`: `vPortYieldFromInt` at portasm.S:648 — FreeRTOS SMP scheduler
- `0x40385cdd`: `prvSelectHighestPriorityTaskSMP` at tasks.c:3619 — with A2=0xa5a5a5a5 (canary)
- `0x420e7342`: `prvTaskCheckFreeStackSpace` at tasks.c:4811 — NULL pointer in panic handler

**Technique F (Component isolation):** The crash is deterministic — same PC, same excvaddr, same A2=0xa5a5a5a5 on every POST /api/valve. Reproduced 3/3 times.

### Step 4: Root Cause

**The HTTP server task stack (12288 bytes) is insufficient for the POST /api/valve handler call chain.**

The handler path allocates multiple large stack-local buffers:
- `CommandBuffer body` (256 bytes)
- `nlohmann::json::parse()` (~1-2KB temporary stack)
- `std::string posStr`
- `CommandBuffer cmdBuf` (256 bytes)
- `ResponseBuffer rspBuf` (2048 bytes)
- Inside `handleSetPosition`: `CommandResponse rsp` with body buffer
- ESP-IDF `httpd_resp_send()` internally uses additional stack

Total: ~8984 bytes consumed by the handler chain from entry to `handleSetPosition`. After `handleSetPosition`, the remaining code path (`vTaskDelay` + return chain + `httpd_resp_send`) requires more stack than the remaining 1700 bytes, causing an overflow into adjacent FreeRTOS TCB/list data structures.

The first symptom is a corrupted FreeRTOS ready list pointer, causing `StoreProhibited` in `vPortYieldFromInt` during the next context switch. After reboot, the stack canary `A2=0xa5a5a5a5` confirms the overflow.

## Fix Specification (for @implementer)

### Description

Increase HTTP server task stack from 12288 to 16384 bytes to accommodate the POST /api/valve handler chain and provide adequate safety margin.

### Files Modified

- `components/infrastructure/network/include/infrastructure/network/http_server.hpp:48`:
  - Changed `STACK_SIZE` from `12288` to `16384`
  - Added explanatory comment referencing the investigation

- `components/domain/include/domain/types.hpp:82`:
  - Changed `HTTP_SERVER_STACK` from `12288` to `16384` (kept in sync)

### Verification

1. Flash firmware: `scripts/idf.sh flash`
2. Wait for device to boot and get IP
3. Send POST /api/valve: `curl -X POST http://<IP>/api/valve -H "Content-Type: application/json" -d '{"position":"output"}'`
4. Expected response: `{"status":"ok","data":{"position":"output"}}`
5. Verify no crash: check serial log for `=== CRASH ===` or Guru Meditation
6. Send GET /api/valve: `curl http://<IP>/api/valve`
7. Expected: `{"status":"ok","data":{"position":"output"}}`

**Result:** Fix verified on 2026-07-16 — POST returns success, GET confirms valve position, no crash.

## Investigation Artifacts

| File | Status |
|------|--------|
| `main/main_smoke.cpp` | ✅ Never created |
| `[INVESTIGATION]` markers | ✅ Removed from all source files |
| Lessons learned | ✅ LL-050 added |

## Remaining Issues

None. The root cause is isolated to the HTTP server task stack size. No other handlers or tasks were found to have insufficient stack.
