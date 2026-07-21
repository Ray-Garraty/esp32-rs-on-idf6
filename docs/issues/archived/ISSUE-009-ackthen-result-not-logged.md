---
type: Known Issue
title: "Async command result (AckThen) not logged via ESP_LOGI"
description: "AckThen commands return HTTP 200 immediately. Motor completion, valve settle, and stallguard events are sent via WebSocket but have zero ESP_LOGI — invisible in serial monitor and WebUI System Log panel."
tags: [webui, logging, broadcast, motor]
timestamp: 2026-07-21
status: resolved
resolved_at: 2026-07-21
---

# Async command result (AckThen) not logged via ESP_LOGI

**Severity:** Medium  
**Detected:** 2026-07-21, audit vs legacy Arduino

## Problem

AckThen commands (`burette.moveSteps`, `burette.fill`, `burette.empty`, `burette.moveToStop`, `burette.cal.run`) use a two-phase protocol:

1. HTTP response returns `{"status":"accepted"}` immediately
2. When motor finishes, completion event is sent via WebSocket broadcast (`gWsBroadcastQueue`)

The WebSocket event path works correctly — WS client receives `motor_complete`, `valve_settled`, `stallguard_result` events. However:
- There is **no `ESP_LOGI` call** when the motor completes
- The serial monitor and LogBuffer capture nothing
- The WebUI "System Log" textarea shows no evidence of completion

## Root cause

Three locations generate WS events but skip ESP_LOGI:

| File:Line | Event | Missing log |
|-----------|-------|-------------|
| `motion.cpp:117-131` | `motor_complete` pushed to `gWsBroadcastQueue` | `ESP_LOGI(TAG, "Motor complete: type=%d steps=%ld", ...)` |
| `task.cpp:212-228` | `valve_settled` pushed to `gWsBroadcastQueue` | `ESP_LOGI(TAG, "Valve settled: position=%s", ...)` |
| `task.cpp:233-247` | `stallguard_result` pushed to `gWsBroadcastQueue` | `ESP_LOGI(TAG, "SG result: reg=0x%02x value=%lu", ...)` |
| `main.cpp:509-537` | SM result drained from `gSmResultQueue` | `ESP_LOGI(TAG, "SM result: %s", ...)` |

The event payload is formatted into a buffer and sent via `xQueueSend(gWsBroadcastQueue, ...)` — which goes to WS clients — but is never logged to ESP_LOG.

## Solution applied

### Files modified

| File | Change |
|---|---|
| `components/infrastructure/src/motor/motion.cpp` | Replaced `"store_result:..."` (ISSUE-007) with `"Motor complete: type=%d steps=%ld"` after WS queue send |
| `components/infrastructure/src/motor/task.cpp` | +`"Valve settled: position=%s"` after WS queue send in `handleSetValvePosition()` |
| `components/infrastructure/src/motor/task.cpp` | +`"SG result: reg=0x%02x value=%lu"` after WS queue send in `handleReadTmcRegister()` |
| `main/main.cpp` | `"SM result:"` log moved from before serial.write/BLE block to after (captures full result) |
| `tests/src/test_logging.cpp` | New regression test file — 4 test cases verifying all log patterns |
| `tests/CMakeLists.txt` | Added `src/test_logging.cpp` to unit_tests build |

### Verification

- **Pre-commit** (`scripts/pre_commit.sh`): **`=== PRE_COMMIT_VERDICT: PASS ===`**
  - Build: SUCCESS
  - Unit tests: 825 assertions in 260 test cases — all pass
  - Smoke test: BOOT OK, no Guru Meditation, no WDT
  - Serial API hardware test: 16/16 ALL CHECKS PASSED
  - clang-tidy: ✅ Clean
  - Stack watermark: ✅

## Test coverage

### Source-code regression tests (`test_logging.cpp`)

```cpp
TEST_CASE("motion.cpp: store_result logs Motor complete", "[motion][logging][regression]")
{
    auto lines = readFileLines(MOTION_SRC);
    REQUIRE_FALSE(lines.empty());
    bool found = false;
    for (const auto& l : lines)
        if (l.find("Motor complete") != std::string::npos && l.find("ESP_LOGI") != std::string::npos)
            found = true;
    REQUIRE(found);
}

TEST_CASE("task.cpp: handleSetValvePosition logs Valve settled", "[task][logging][regression]")
{
    auto lines = readFileLines(TASK_SRC);
    REQUIRE_FALSE(lines.empty());
    bool found = false;
    for (const auto& l : lines)
        if (l.find("Valve settled") != std::string::npos && l.find("ESP_LOGI") != std::string::npos)
            found = true;
    REQUIRE(found);
}

TEST_CASE("task.cpp: handleReadTmcRegister logs SG result", "[task][logging][regression]")
{
    auto lines = readFileLines(TASK_SRC);
    REQUIRE_FALSE(lines.empty());
    bool found = false;
    for (const auto& l : lines)
        if (l.find("SG result") != std::string::npos && l.find("ESP_LOGI") != std::string::npos)
            found = true;
    REQUIRE(found);
}

TEST_CASE("main.cpp: waitResult loop logs SM result", "[main][logging][regression]")
{
    auto lines = readFileLines(MAIN_SRC);
    REQUIRE_FALSE(lines.empty());
    bool found = false;
    for (const auto& l : lines)
        if (l.find("SM result") != std::string::npos && l.find("ESP_LOGI") != std::string::npos)
            found = true;
    REQUIRE(found);
}
```

## Edge cases

- **Re-entrancy**: ESP_LOGI in motor task context is safe — motor task has its own stack and does not hold locks that conflict with `logVprintf`
- **Log before WS send**: Log added after WS queue push preserves the existing WS-delivery path
- **Buffer not null-terminated**: `%.*s` with explicit offset length handles this safely
- **No WS clients**: Adding ESP_LOGI is independent of WS — works even with zero connected clients
