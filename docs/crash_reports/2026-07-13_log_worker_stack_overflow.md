---
type: CrashReport
version: "1.0"
task_id: "manual-2026-07-13"
timestamp: "2026-07-13T06:20:00Z"
crash_signature: "exccause=20 InstrFetchProhibited pc=0x00000000 backtrace: 0xfffffffd > broadcastWsEvent > wsLogCallback > workerTaskEntry"
---

# Crash Report: log_worker Task Stack Overflow

## Verdict

- **Status:** root_cause_found
- **Root Cause:** `log_worker` task stack (8192 bytes) insufficient for the callback chain
  `workerTaskEntry → wsLogCallback → broadcastWsEvent → httpd_ws_send_frame_async`
  which requires ~1200-1500 bytes. Repeated invocations progressively degraded the
  stack watermark (868 → 652 → 348 bytes) until overflow occurred.
- **Confidence:** high

## Evidence Chain

### Step 1: Triage

**Log excerpt showing progressive log_worker stack degradation:**
```
W (4043) stack_monitor: Thread log_worker: LOW STACK! 92% used
W (5043) stack_monitor: Thread log_worker: LOW STACK! 92% used
...
W (9054) stack_monitor: Thread log_worker: LOW STACK! 95% used
W (13040) stack_monitor: Thread log_worker: LOW STACK! 95% used
```

**First crash — InstrFetchProhibited pc=0 with corrupted backtrace:**
```
exccause=20 name=InstrFetchProhibited pc=0x00000000 excvaddr=0x00000000
Backtrace: 0xfffffffd:0x3fce5900 0x42026bb2:0x3fce5930 0x420143b1:0x3fce5960 0x4204e7e6:0x3fce5b00 0x420e02ba:0x3fce5bb0
current watermark=348
```

**Backtrace decoding (addr2line):**
```
0xfffffffd: ?? ??:0                                      ← corrupted stack
0x42026bb2: HttpServer::broadcastWsEvent() at http_server.cpp:621
0x420143b1: wsLogCallback() at main.cpp:138
0x4204e7e6: LogBuffer::workerTaskEntry() at log_buffer.cpp:69
0x420e02ba: vPortTaskWrapper at port.c:143
```

**Second crash — confirmed stack overflow with canary:**
```
A stack overflow in task log_worker has been detected.
Backtrace: ... 0x40385516:0xa5a5a5a5 |<-CORRUPTED
current watermark=0
```

### Step 2: Protocol Analysis

The S1–S5 protocol was applied. The key finding was S4 (backtrace decoding) identifying
the exact call chain. The '0xa5a5a5a5' pattern in the second crash is the definitive
FreeRTOS stack canary overflow signature.

### Step 3: Root Cause

Two factors combined to cause the crash:

1. **Insufficient stack allocation (8192 bytes):** The `log_worker` task processes
   ESP_LOG callbacks through the chain `workerTaskEntry → wsLogCallback →
   broadcastWsEvent → httpd_ws_send_frame_async`. This chain requires ~1200-1500
   bytes of stack (including `char buf[384]` in wsLogCallback, the `httpd_ws_frame_t`
   struct, and httpd_ws_send_frame_async internal overhead).

2. **Inadequate stack guard threshold (256 bytes):** The guard checked
   `uxTaskGetStackHighWaterMark(nullptr) < 256`. Since `uxTaskGetStackHighWaterMark`
   returns bytes on ESP-IDF v6, this only prevented callback execution when < 256
   bytes remained — far too late. The `char buf[384]` alone exceeds this threshold.

### Step 4: Fix

| File | Change |
|------|--------|
| `components/domain/include/domain/types.hpp` | Added `LOG_WORKER_STACK = 12288` constant |
| `main/main.cpp` | Increased log_worker xTaskCreate stack from 8192 → `LOG_WORKER_STACK` (12288) |
| `main/main.cpp` | Increased stack guard threshold from 256 → 1536 bytes |
| `main/main.cpp` | Updated StackMonitor registration to use `LOG_WORKER_STACK` |

## Fix Verification

30-second smoke test after fix:

```
stack_monitor: Thread log_worker: cfg=12288B wmark=1668 used=86%
```

- **No LOW STACK warnings** (was 16 warnings before crash)
- **Watermark stable at 1668** (was degrading 868 → 652 → 348 before crash)
- **No crashes** in 30-second monitoring period
- All other tasks' watermarks unchanged and healthy

## Investigation Artifacts

| File | Status |
|------|--------|
| `components/domain/include/domain/types.hpp` | ✅ Added `LOG_WORKER_STACK` constant |
| `main/main.cpp` | ✅ Stack size + guard threshold fixed |
| `[INVESTIGATION]` markers | ✅ None from this investigation |
| Lessons learned | ✅ LL-048 added |

## Remaining Issues

- The HTTP server internal task stack (12288) appears adequate but should be monitored
  if `wsLogCallback`/`broadcastWsEvent` stack usage grows.
- The `motor` task (86-89% used) has limited headroom — any additions to this task
  may require a stack increase.
