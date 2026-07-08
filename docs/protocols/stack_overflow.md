---
type: ESP32 Reference
title: "Protocol: Stack Overflow"
description: >
  Debugging protocol for ESP32 stack overflows: detection, default stack sizes
  for all tasks, instrumentation with stack watermark, and root cause analysis
  for the ESP32-S3 C++23 firmware.
tags: [esp32, debug, stack-overflow, protocol]
timestamp: 2026-07-03
---

# Protocol: Stack Overflow

## Trigger
- `***ERROR*** A stack overflow in task <name> has been detected.`
- Guru Meditation LoadProhibited with corrupted/truncated backtrace
- Guru Meditation with `EXCVADDR = 0xFFFFFFA0` or similar NULL+offset pattern
- Intermittent crashes that change address on every boot
- Crash that disappears when logging is reduced or removed
- Boot crash where stack watermark is < 2048 bytes (see S1)

## Default Stack Sizes

| Task | Default | Notes |
|------|---------|-------|
| `main` (FreeRTOS main task) | 32768 (32 KB) | `CONFIG_ESP_MAIN_TASK_STACK_SIZE` |
| Net owner thread | 16384 (16 KB) | WiFi → HTTP → BLE init, `xTaskCreate` |
| Motor thread | 16384 (16 KB) | RMT stepper + homing, `xTaskCreate` |
| Temperature thread | 16384 (16 KB) | DS18B20 bitbang reads |
| BLE notify thread | 8192 (8 KB) | NimBLE notify pushes |
| HTTP server (FreeRTOS internal) | 12288 (12 KB) | Set via `CONFIG_HTTPD_TASK_STACK_SIZE` |

## Steps

### Step 1: Diagnose Which Task Overflows

**If explicit FreeRTOS detection message is present:**
```
***ERROR*** A stack overflow in task main has been detected.
```
→ The crashing task is named in the message. Fix: increase its stack size.

**If no explicit message — check watermark for ALL tasks:**
```cpp
// [INVESTIGATION] check main task watermark
UBaseType_t wm = uxTaskGetStackHighWaterMark(nullptr);
printf("main stack watermark: %u\n", wm * sizeof(configSTACK_DEPTH_TYPE));
```

### Step 2: Check Backtrace for Corruption

A corrupted backtrace is the #1 sign of stack overflow:
```
Backtrace: 0x403....:0x3ffd....
```
versus a healthy backtrace:
```
Backtrace:
  0x403....:0x3ffd....
  0x403....:0x3ffd....
  0x403....:0x3ffd....
```

If backtrace has only 1–2 entries when it should have 10+ → stack pointer
has been corrupted by overflow.

### Step 3: Measure Stack Usage

1. Add watermark measurement to the crashing task at every key point.
2. Calculate: `used_bytes = stack_size - watermark`
3. If used > 80% of stack size → overflow likely under load.

### Step 4: Increase Stack Size

Find the relevant config and double it:

| Location | Variable | Default | Action |
|----------|----------|---------|--------|
| `sdkconfig.defaults` | `CONFIG_ESP_MAIN_TASK_STACK_SIZE` | 32768 | Increase by 2x |
| `components/domain/include/domain/channels.hpp` | `MOTOR_THREAD_STACK` | 16384 | Increase by 2x |
| `xTaskCreate()` call site | 4th arg (stack_depth) | varies | Increase by 2x |

After doubling → build → flash → test.

### Step 5: Find Minimum Viable Stack

If doubling fixes the crash, binary-search the minimum required size:
1. Set to midpoint between old and new value.
2. Build → flash → test.
3. If OK, reduce further. If crash, increase.
4. When done, add 20% safety margin.

### Step 6: Root Cause — Why Did Stack Usage Grow?

Once the crash is fixed, identify the code change that pushed stack usage
over the limit:

- New deep call chains (function A → B → C → D → E)
- New large stack allocations (`std::array<T, N>` or C-style arrays on stack)
- New thread spawns (each thread has its own stack)
- New `ESP_LOGI` / `std::print()` calls (format_args temporaries on stack)
- Recursive or deeply nested parser/handler
- `std::format()` or `nlohmann::json::dump()` in constrained threads

## ESP32 Stack Overflow Mechanics

On ESP32, the stack grows DOWNWARD from high addresses. The stack canary
is placed at the top of the stack (lowest address). When stack overflows:

1. **First stage:** Canary is overwritten → FreeRTOS detects on next context
   switch → `***ERROR*** A stack overflow in task ...` message.
2. **Second stage** (no canary / canary disabled): Stack writes into adjacent
   memory. Adjacent to the main task stack is typically the TLSF heap metadata.
   This produces a misleading heap corruption Guru Meditation.
3. **Third stage** (severe overflow): Stack corrupts FreeRTOS TCB or other
   kernel structures → unpredictable crash.

This is why **S1 (stack watermark) must run before S2 (heap integrity)**.
A "heap corruption" crash at boot is a stack overflow until proven otherwise.

## References

- FreeRTOS stack overflow detection: https://www.freertos.org/Stacks-and-stack-overflow-checking.html
- ESP-IDF task stack config: https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32/api-reference/system/freertos.html#task-stack
- `docs/lessons_learned/` LL-001
- `docs/protocols/embedded_boot_crash.md` S1
- `docs/protocols/heap_corruption.md` (to understand why stack overflow looks like heap corruption)
