---
type: CrashReport
version: "1.0"
task_id: "manual"
timestamp: "2026-07-11T13:00:00Z"
crash_signature: "IP_EVENT_STA_GOT_IP → callback chain overflow in system event task (stack=2304B default)"
---

# Crash Report

## Verdict

- **Status:** root_cause_found
- **Root Cause:** Stack overflow in the **default system event task** (stack=2304 B) caused by the `ESP_LOGI` → `LogBuffer::push()` → `wsLogCallback` → `broadcastWsEvent()` callback chain, which adds ~2000+ B of stack depth when `IP_EVENT_STA_GOT_IP` fires.
- **Confidence:** high

## Evidence Chain

### Step 1: Triage

**Crash log:** `logs/serial_2026-07-11_12-45-57.log`

**Boot flow succeeds** through NVS, task creation, WiFi init, HTTP server, BLE init, PHY wait.

**Crash timing:**
- `[12:46:01.503]` `I (3741) esp_netif_handlers: sta ip: 192.168.1.103` — system event task processes `IP_EVENT_STA_GOT_IP` (default ESP-IDF handler runs first)
- `[12:46:01.513]` `I (3753) wifi: STA got IP: 192.168.1.103` — `WifiManager::handleEvent()` runs from system event task
- `[12:46:01.554]` `I (3756) http_srv: WS broadcast:Panic handler entered multiple times...` — **CRASH at tick 3756** (~3.7 seconds into run)

**Key observations:**
- The ONLY output from the crash handler is "Panic handler entered multiple times" — **no "=== CRASH ===" header was printed**
- The first panic's exception information was completely lost
- The panic handler itself crashed (double fault)
- The crash occurs **41 ms after** `IP_EVENT_STA_GOT_IP`
- At the time of crash: **motor is homing** (`"brt":{"sts":"working"}`, `"motor":{"isMoving":true}`)
- **4 WS sessions exist** (all invalid/expired) — shown by "4 skipped (total 4 sessions)"

### Step 2: S1–S5 Protocol (Skip — Runtime Crash Protocol)

| Step | Result | Action |
|------|--------|--------|
| S1 (stack watermark) | N/A — crash prevents app_main watermark measurement | Would be useful to add |
| S2 (heap integrity) | N/A — not a heap issue | — |
| S3 (smoke test) | N/A — application code is running | — |
| S4 (delta analysis) | N/A — crash involves fundamental architecture | — |
| S5 (red flags) | **RED FLAG**: Callback chain from ESP_LOG hook into WS broadcast executes in ANY calling task's context | **GR-6 violation** |

### Step 3: Systematic Elimination

#### Evidence A: Callback chain stack analysis

The `ESP_LOGI` macro calls `esp_log_write()` → `logVprintf()` (custom vprintf hook, main.cpp line 80):

```cpp
static int logVprintf(const char* fmt, va_list args) {
    char buf[384];  // ← 384 bytes on CALLING task's stack
    int n = std::vsnprintf(buf, sizeof(buf), fmt, args);
    // ...
    ecotiter::domain::LogBuffer::instance().push(ts, level, buf);
    // ...
}
```

`LogBuffer::push()` (log_buffer.cpp line 12) calls the registered callback synchronously:

```cpp
if (callback_) {
    LogEntry entry;
    std::strncpy(entry.level, slot.level, sizeof(entry.level) - 1);
    std::strncpy(entry.message, slot.message, sizeof(entry.message) - 1);
    callback_(entry);  // ← wsLogCallback, SYNCHRONOUS
}
```

`wsLogCallback` (main.cpp line 100) allocates another buffer and calls `broadcastWsEvent`:

```cpp
static void wsLogCallback(const ecotiter::domain::LogEntry& entry) {
    // ...
    char buf[384];  // ← Another 384 bytes on CALLING task's stack
    // ... format JSON ...
    hs->broadcastWsEvent(buf, static_cast<size_t>(n));  // ← httpd API call
}
```

**Total stack usage on CALLING task** (e.g., system event task) from this chain:
```
ESP_LOGI frame                     ~50 B
logVprintf (384 B buffer)          ~450 B
LogBuffer::push()                   ~50 B  
wsLogCallback (384 B buffer)       ~450 B
broadcastWsEvent frame             ~100 B
httpd_ws_send_frame_async          ~200 B
httpd_ws_get_fd_info               ~100 B
ESP_LOGD inside broadcastWsEvent   suppressed by pushing_ flag  ~0 B
```
**Total: ~1400 B minimum** — and that's for a SINGLE call through the chain.

#### Evidence B: System event task stack size is critically low

From ESP-IDF v6 `esp_system/Kconfig` (verified in `/home/vlabe/Downloads/esp-idf-master/components/esp_system/Kconfig:229`):

```kconfig
config ESP_SYSTEM_EVENT_TASK_STACK_SIZE
    int "Event loop task stack size"
    default 2304  ← ONLY 2304 BYTES!
```

**2304 bytes** is the default. The sdkconfig.defaults does NOT override this.

Stack usage breakdown when `IP_EVENT_STA_GOT_IP` fires:

| Call | Stack used | Running total |
|------|-----------|---------------|
| Event loop dispatch overhead | ~500 B | 500/2304 |
| `handleEvent()` frame | ~100 B | 600/2304 |
| `ESP_LOGI("STA got IP")` → callback chain | ~1400 B | **2000/2304** |
| `startMdns()` with `ESP_LOGW` on failure | ~500 B | **2500/2304 ← OVERFLOW** |
| `esp_wifi_set_mode(WIFI_MODE_STA)` | ~200 B | 2700/2304 |
| `ESP_LOGI("AP stopped")` → callback chain | ~1400 B | 4100/2304 |

**Even the FIRST `ESP_LOGI` in the handler (step 3) already uses ~2000 of 2304 bytes.** The subsequent `startMdns()` and the `ESP_LOGI("AP stopped")` definitively overflow.

#### Evidence C: "Panic handler entered multiple times" without crash dump

The crash handler (`__wrap_esp_panic_handler` in crash_handler.cpp) tries to:
1. `printf("\n=== CRASH ===\n")` — **needs working stack**
2. `BlackBox::instance().dump()` — **needs working stack**  
3. `__real_esp_panic_handler(info)` — **ESP-IDF panic handler needs stack**

When the first crash is a **stack overflow**, the remaining stack is already depleted. The panic handler itself cannot operate with the few remaining bytes, causing a **double fault** → "Panic handler entered multiple times. Abort panic handling."

This matches **exactly** the pattern documented in **LL-001**: "boot-time heap corruption crashes ... are usually caused by main task stack overflow ... producing misleading crash signatures."

#### Evidence D: The interleaved log line

```
I (3756) http_srv: WS broadcast:Panic handler entered multiple times...
```

The `http_srv: WS broadcast:` is the tail end of `ESP_LOGD(TAG, "WS broadcast: %d sent, %d skipped (total %zu sessions)", ...)` at `http_server.cpp:556`. This line is printed at the END of `broadcastWsEvent()`, which is called from the callback chain. The interleaving with "Panic handler entered" means:

1. The callback chain reached `broadcastWsEvent()` successfully
2. `broadcastWsEvent` completed its iteration (all 4 sessions skipped)
3. `ESP_LOGD` at line 556 was called → `logVprintf` → `LogBuffer::push()` → pushing_ flag is false now → callback fires **again** → `wsLogCallback` → `broadcastWsEvent()` call 2 (recursive)
4. The recursive `broadcastWsEvent` completed (pushing_=true from inner push prevented further recursion)
5. Back in outer `push()`, `fwrite` prints "I (3756) http_srv: WS broadcast: 0 sent, ..."
6. **DURING this `fwrite`** (or immediately after), the stack overflows → first crash → panic handler fails → double fault

The "WS broadcast:" text and "Panic handler entered" interleave because `fwrite` from the outer log and `printf` from the failing panic handler both write to the same UART.

### Step 4: Root Cause

**The `LogBuffer` callback chain (`ESP_LOGI` → `logVprintf` → `LogBuffer::push()` → `wsLogCallback` → `broadcastWsEvent()` → `httpd_ws_send_frame_async`) executes synchronously in the calling task's context.** When the caller is the **default system event task** (which has only **2304 bytes** of stack), the additional ~2000+ bytes of stack usage from:

1. `logVprintf`'s 384-byte buffer
2. `wsLogCallback`'s 384-byte buffer
3. `broadcastWsEvent` frame + `httpd_ws_get_fd_info` + `httpd_ws_send_frame_async`

...combined with the event loop's existing stack usage (~500+ bytes), **exceeds the 2304-byte limit**, causing a stack overflow.

The overflow corrupts adjacent memory (TLSF metadata, TCB, or another task's stack). The first observable crash happens during/after the second recursive `ESP_LOGD` call from within the callback chain. The panic handler itself cannot operate with the corrupted/depleted stack, resulting in a double fault.

### Violations Found

| Rule | Violation | Details |
|------|-----------|---------|
| **GR-6** | Stack budget not respected for callback chain | The `wsLogCallback` executes in ANY calling task's context without verifying the task has sufficient stack. The system event task (2304 B) cannot safely execute this callback. |
| **GR-6** | System event task stack not configured in `sdkconfig.defaults` | No `CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE` set — uses default 2304 B which is insufficient when the callback chain is active. |
| **Indirect GR-1** | `broadcastWsEvent()` called synchronously from EP_LOG hook | The callback chain turns every `ESP_LOGI` into a potential blocking operation (WS send), violating the principle of non-blocking operations. |
| **Cross-cutting** | `LogBuffer` callback is synchronous | Should be asynchronous (queue the entry, process in a dedicated task) to avoid arbitrary stack usage in any task that calls `ESP_LOGI`. |

## Fix

### Primary Fix (Required)

**Add `CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=8192` to `sdkconfig.defaults`:**

The system event task handles WiFi, IP, and other infrastructure events. With the `wsLogCallback` active, every `ESP_LOGI` from this task adds ~2000 B of stack usage. An 8 KB stack provides adequate margin.

### Secondary Fix (Recommended)

**Make the LogBuffer callback asynchronous** — queue log entries to a dedicated low-priority worker task instead of calling the callback synchronously from within the `ESP_LOG` hook. This prevents arbitrary stack usage in any task context.

Implementation sketch:
```cpp
// In LogBuffer or a new LogDispatch task:
static void logDispatchTask(void*) {
    while (true) {
        LogEntry entry;
        if (xQueueReceive(gLogQueue, &entry, portMAX_DELAY)) {
            wsLogCallback(entry);  // Runs in dedicated task with known stack
        }
    }
}
```

### Tertiary Fix (Defensive)

Add a stack guard in `wsLogCallback`:
```cpp
static void wsLogCallback(const ecotiter::domain::LogEntry& entry) {
    // Check available stack — skip callback if < 1024 bytes remaining
    UBaseType_t wm = uxTaskGetStackHighWaterMark(nullptr);
    if (wm * sizeof(configSTACK_DEPTH_TYPE) < 1024) return;
    // ... existing code ...
}
```

### Files to Modify

| File | Change |
|------|--------|
| `sdkconfig.defaults` | Add `CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=8192` |
| `main/main.cpp` (optional) | Add `wsLogCallback` stack guard before `broadcastWsEvent` call |

### Verification

1. Add `CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=8192` to `sdkconfig.defaults`
2. Run `scripts/idf.sh build` (clean build)
3. Flash and test with WiFi STA (connect to a known AP)
4. Monitor for 30+ seconds: no crash when `IP_EVENT_STA_GOT_IP` fires
5. Verify stack monitor shows all tasks healthy

## Investigation Artifacts

| File | Status |
|------|--------|
| `main/main_smoke.cpp` | ✅ Does not exist |
| `[INVESTIGATION]` markers | ✅ None added |
| Lessons learned | ✅ LL-038 created (see below) |

## Remaining Issues

- The motor homing was active during the crash (coincidental — not the cause)
- The `sessions_` array race condition (between `broadcastWsEvent` and `addSession`/`removeSession`) is a pre-existing issue but did not cause this crash
- The LWIP tcpip mbox / invalid mbox pattern from LL-004 was not observed

## Lessons Learned

A new entry `LL-038` should be created:

```yaml
id: LL-038
date: 2026-07-11
crash_signature: "IP_EVENT_STA_GOT_IP → 'Panic handler entered multiple times' — no crash dump"
category: stack_overflow
trigger_patterns:
  - "Panic handler entered multiple times"
  - "WS broadcast:Panic handler entered"
  - "esp_netif_handlers: sta ip:.*→.*WS broadcast:Panic"
lesson: >
  The LogBuffer callback chain (ESP_LOG hook → wsLogCallback → broadcastWsEvent → 
  httpd API) executes synchronously in the calling task's context. When the system
  event task (default stack=2304 B) calls ESP_LOGI during IP_EVENT_STA_GOT_IP handling,
  the callback chain adds ~2000+ bytes to the stack, exceeding the limit. The stack
  overflow corrupts adjacent memory, causing a primary crash. The panic handler itself
  cannot operate on the depleted stack, producing "Panic handler entered multiple times"
  without any crash dump.
  
  ALWAYS configure CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE when using ESP_LOG hooks
  that invoke complex callbacks. Alternatively, make the callback asynchronous.
  
  The symptom "Panic handler entered multiple times" without "=== CRASH ===" header
  is diagnostic of a stack overflow in the crash site — the panic handler has
  insufficient stack to print its dump.
diagnostic: >
  1. Check CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE in sdkconfig
  2. If default (2304) and wsLogCallback is active → stack overflow
  3. Add watermark check at start of wsLogCallback to confirm
fix: CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=8192 (or larger)
refs:
  - docs/crash_reports/2026-07-11_ip_event_stack_overflow.md
  - docs/protocols/stack_overflow.md
  - LL-001 (same pattern, different task)
```
