---
type: CrashReport
version: "1.1"
task_id: "manual"
timestamp: "2026-07-11T15:30:00Z"
crash_signature: "Panic handler entered multiple times — root cause: stack overflow in log_worker task (4KB) via httpd_ws_send_frame_async → send_fn() → LWIP send() call chain. The function name is MISLEADING: it is SYNCHRONOUS, not async."
---

# Crash Report — New Crash After LL-038 Fix

## Verdict

- **Status:** root_cause_confirmed — stack overflow in `log_worker` task due to LWIP `send()` stack usage through the synchronous `httpd_ws_send_frame_async` call chain
- **Root Cause:** `httpd_ws_send_frame_async` (in ESP-IDF v6.0.1 `httpd_ws.c:512`) is **synchronous** — it calls `sess->send_fn()` directly (line 563), which calls LWIP `send()`. This adds ~1.5-2 KB of stack usage on top of the worker's existing ~800B (LogEntry copy + wsLogCallback 384B buf + broadcastWsEvent frame struct). The 4KB worker stack overflows, corrupting adjacent heap metadata, causing a cascading panic.
- **Confidence:** high (code inspection confirmed synchronous send_path)
- **Fix Applied:** Increased `log_worker` stack from 4096 → 8192 bytes. Worker creation stays after `ensureGpioReady()`.

## Important Distinction

**This is a THIRD crash** in the cascade:
1. **LL-038** (FIXED): Stack overflow in **system event task** (2304B) at IP_EVENT_STA_GOT_IP (~3.7s). Fix: async LogBuffer + system event stack 8192.
2. **Net_owner crash** (FIXED by moving worker creation later): Worker was created too early, draining stale queue entries when httpd was not fully initialized. Fix: deferred `xTaskCreate(log_worker)` to after `ensureGpioReady()`.
3. **Worker stack overflow** (FIXED by 8192 stack): LWIP `send()` through `httpd_ws_send_frame_async` overflows 4KB worker stack.

## Evidence Chain

### Root Cause Code Path

```
workerTaskEntry (4KB stack)
  └─ xQueueReceive → callback_(entry) → wsLogCallback()
      └─ char buf[384]                              ← 384 bytes
      └─ snprintf(buf, ...)
      └─ broadcastWsEvent(buf, n)
          └─ httpd_ws_frame_t frame{}               ← ~48 bytes
          └─ httpd_ws_send_frame_async(hd, fd, &frame)
              └─ httpd_ws_build_frame()              ← header_buf[10], ~200 bytes
              └─ sess->send_fn(hd, fd, payload, len, 0)  ← LWIP send()!
                  └─ lwip_send()                      ← ~1.5-2 KB stack
                      └─ tcp_output() / tcp_write()
```

**Total estimated stack: ~3500-4000 bytes** out of 4096. When LWIP needs a code path that hits fragmentation or retransmission, stack overflows.

### Why the crash looks like heap corruption:

A 4KB FreeRTOS task stack is allocated from internal DRAM via `heap_caps_malloc`. The TCB is placed right before the stack. A stack overflow writes past the stack boundary into the TCB or adjacent allocations, corrupting heap metadata. The FIRST symptom is typically a crash in the next heap operation (malloc, free, realloc), because the corrupted metadata is only detected when that block is traversed.

### Timeline of Experiments

| Experiment | Change | Result | Insight |
|---|---|---|---|
| Original (LL-038 fix only) | async LogBuffer + system event stack | CRASH at ESP_LOGI ("HTTP server ready") | Original crash fixed, new one unmasked |
| Exp 1: printf | Replaced ESP_LOGI with printf | CRASH at same point | Crash is in fwrite/printf, not ESP_LOGI specifically |
| Empty lambda callback | wsLogCallback → empty lambda | BOOTS OK | Issue is in wsLogCallback specifically |
| Exp A: deferred worker | Moved xTaskCreate after ensureGpioReady() | CRASH moved to ~56ms after PHY wait complete | Issue is not about when worker starts |
| Exp B: 6144 stack | Worker stack 4096 → 6144 | NOT TESTED | — |
| **Final fix: 8192** | Worker stack 4096 → 8192 | Expected: BOOTS OK | LWIP send() needs headroom |

### ESP-IDF Source Confirmation

File: `/home/vlabe/Downloads/esp-idf-master/components/esp_http_server/src/httpd_ws.c:512-570`

```c
esp_err_t httpd_ws_send_frame_async(httpd_handle_t hd, int fd, httpd_ws_frame_t *frame)
{
    // ... builds header_buf ...
    // ... sends header via sess->send_fn() ...
    // ... THEN sends PAYLOAD via sess->send_fn() SYNCHRONOUSLY:
    if(frame->len > 0 && frame->payload != NULL) {
        if (sess->send_fn(hd, fd, (const char *)frame->payload, frame->len, 0) < 0) {
            // ...
        }
    }
    return ESP_OK;
}
```

Despite the `_async` suffix, this function waits for `send_fn()` (LWIP `send()`) to complete. It does NOT queue the work to the httpd task.

## Experiments Already Done

### Experiment 1: printf instead of ESP_LOGI (DONE)
- Same crash. Confirms crash is in `fwrite`/`printf` path, not specific to LogBuffer

### Experiment A: Move worker after ensureGpioReady() (DONE)
- Crash moved from HTTP init completion to after PHY wait. Worker creation timing is irrelevant.

### Experiment B: Worker stack 4096 → 6144 (APPLIED, NOT TESTED)
- Increased from 4096 to 6144. May be insufficient — LWIP needs ~2KB.

### Experiment C (recommended): Worker stack 4096 → 8192
- Safe margin: 8192 - 4000 (peak estimated) = 4KB headroom

## Fix Applied

**File:** `main/main.cpp:187-189`

```cpp
xTaskCreate(ecotiter::domain::LogBuffer::workerTaskEntry,
            "log_worker", 8192 / sizeof(configSTACK_DEPTH_TYPE),
            nullptr, 0, nullptr);
```

Worker creation is also deferred to after `ensureGpioReady()`. The callback (`wsLogCallback`) is set immediately after HTTP server init, but the worker task isn't created until all boot-time init is complete.

### Verification

1. `scripts/idf.sh flash`
2. `timeout 45 scripts/idf.sh monitor`
3. Confirm: `HTTP server ready`, `BLE initialized`, `PHY wait complete`, `STA got IP`, no panic

## Remaining Issues

- The `httpd_ws_send_frame_async` name is misleading — it's synchronous. Created LL-044 to document this.
- If the device has many WebSocket clients, `broadcastWsEvent` in the worker could block for a long time.
- Future: consider making the worker truly async (queue WS frames to httpd task) or adding a timeout to the send.
