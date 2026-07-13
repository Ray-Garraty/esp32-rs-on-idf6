---
type: Known Issue
title: "Main loop tick watchdog latency exceeds 10ms GR-1 threshold"
description: "Main loop blocks for 15-101ms every ~1s, violating GR-1 10ms limit. Firmware tick_watchdog threshold also incorrectly set to 15ms instead of 10ms"
tags: [main_loop, GR-1, tick_watchdog, latency]
timestamp: 2026-07-13
---

# ISSUE-003: Main loop tick watchdog — latency exceeds 10ms GR-1 threshold

**Severity:** Medium  
**Detected:** 2026-07-13, serial log `serial_2026-07-13_07-25-02.log`  
**Related rule:** AGENTS.md §GR-1 (Never Block The Main Loop)

## Symptom

32 warnings in 30 s (every ~1 s):

```
W (1712) tick_watchdog: main loop took 96626 us (>15ms threshold)
W (1871) tick_watchdog: main loop took 15569 us (>15ms threshold)
W (1936) tick_watchdog: main loop took 25192 us (>15ms threshold)
...
W (29065) tick_watchdog: main loop took 59147 us (>15ms threshold)
```

**Note:** The firmware's `tick_watchdog` warns at 15ms (hardcoded in `tick_watchdog.hpp` line 19), but the project rule GR-1 forbids blocking >10ms. The watchdog threshold itself is wrong and should be 10ms. The actual main loop latency is **15–101ms** — all measurements exceed the 10ms limit regardless.

- Min: 15.5 ms
- Max: **101.9 ms**
- Typical: ~59 ms

## Impact

Main loop blocking >10 ms violates GR-1. This delays:
- WiFi/BLE event processing
- Temperature sensor polling
- Motor command responses
- WebSocket broadcast scheduling

The ~59 ms typical latency suggests a periodic blocking operation running every second.

## Suspected Causes

1. **RMT motion blocking:** `rmt_tx_wait_all_done()` in main loop (forbidden per GR-1 but may exist in newer code)
2. **Motor task operations:** If motor task synchronises with main (via semaphore or notification), the main loop may block waiting for motor
3. **BLE/HTTP processing:** Large JSON serialisation in main loop context
4. **Log flushing:** `log_worker` blocking while draining a large queue

## Investigation

1. Run with `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=1` to see task CPU usage
2. Add `TickWatchdog` timing to individual main loop operations (process motor, process logs, check BLE, etc.)
3. Check if any `vTaskDelay(pdMS_TO_TICKS(...))` with >10ms arg is called in main context

## Status

open
