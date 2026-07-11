---
type: ESP32 Reference
title: "Protocol: Embedded Boot Crash"
description: >
  S1–S5 Occam's Razor protocol for debugging ESP32 boot crashes: Guru Meditation,
  WDT reset, boot hang, and incomplete flash. Mandatory systematic debugging
  procedure for the ESP32-S3 C++23 firmware.
tags: [esp32, debug, boot-crash, protocol, s1-s5]
timestamp: 2026-07-03
---

# Protocol: Embedded Boot Crash

> **Scope:** This protocol applies when the bootloader has transferred control
> to `app_main()` — i.e., the app image exists and is validated. If the device
> never boots due to a missing/corrupt app image (boot loop), use
> `boot_loop.md` (F1–F4 protocol) instead.

## Trigger
- Guru Meditation at boot (any category)
- WDT reset (`rst:0x8`, `TG1WDT_SYS_RESET`)
- Boot hang (no serial output after startup log)
- `invalid segment length 0xffffffff` (incomplete flash — see `boot_loop.md`)

## Mandatory Steps (S1–S5 Occam's Razor Protocol)

Execute in strict order. No exceptions.

### S1: Stack Watermark Baseline (2 min)

Insert `uxTaskGetStackHighWaterMark(nullptr)` at the top of `app_main()`:

```cpp
// [INVESTIGATION] S1: stack watermark baseline
UBaseType_t wm = uxTaskGetStackHighWaterMark(nullptr);
printf("[INVESTIGATION] main task stack watermark: %u bytes\n",
       wm * sizeof(configSTACK_DEPTH_TYPE));
```

Build (`scripts/idf.sh build`), flash, monitor.

**Decision:**
- Watermark < 2048 bytes → **root cause = stack overflow**. Skip to Phase 4.
- Watermark < 4096 → **likely stack overflow**. Bump stack size 2x, test.
- Watermark >= 4096 → not a stack overflow. Proceed to S2.

### S2: Heap Integrity Pre-Check (2 min)

Insert `heap_caps_check_integrity_all(true)` at the top of `app_main()`:

```cpp
// [INVESTIGATION] S2: heap integrity pre-check
assert(heap_caps_check_integrity_all(true));
printf("[INVESTIGATION] heap integrity: OK\n");
```

If heap integrity check itself crashes → ESP-IDF init issue.
If it passes → main.cpp code path corrupts heap.

### S3: Smoke Test (5 min)

Strip `main/main.cpp` to a minimal loop:

```cpp
// [INVESTIGATION] smoke test — zero allocations
extern "C" void app_main(void) {
    // No printf, no heap allocs
    while (true) {
        // spin
    }
}
```

Build (`scripts/idf.sh build`), flash, monitor.

**Decision:**
- Smoke test boots and stays alive → ESP-IDF init OK, problem is in application code.
- Smoke test crashes too → sdkconfig / ESP-IDF configuration issue.

### S4: Delta Analysis (5 min)

```bash
git log --oneline <last-known-good>..HEAD
git diff <last-known-good> HEAD -- sdkconfig.defaults
git diff <last-known-good> HEAD --stat
xtensa-esp32s3-elf-size build/ecotiter.elf
xtensa-esp32s3-elf-objdump -h build/ecotiter.elf
```

**Decision:**
- `.bss` grew >30% → static variable added / large buffers
- `.text` grew >50% → many new functions, stack pressure
- sdkconfig has new options → test independently (S5)

### S5: Red Flags Checklist (2 min)

Check for:
- [ ] New `std::array` or stack-local buffer > 4 KB
- [ ] New `std::mutex` — first heap allocation trigger
- [ ] New threads spawned (stack usage compounding)
- [ ] sdkconfig changes (WebSocket, NimBLE pools, stack sizes)
- [ ] Phase N worked, Phase N+1 crashes with same hardware
- [ ] `CONFIG_ESP_MAIN_TASK_STACK_SIZE` unchanged despite main.cpp growth

**Gate: ONLY after S1–S5 pass can complex hypotheses be proposed.**

## Time Budget

| Step | Time |
|------|------|
| S1–S5 total | ≤ 15 min |
| Systematic elimination | ≤ 60 min |
| **Total** | **≤ 75 min** |

## Escalation

If no root cause after 75 minutes → escalate to human with:
- All evidence gathered (each S1–S5 output documented)
- All hypotheses ruled out (with why)
- Recommended next investigation angle

## References

- `docs/lessons_learned/` — known crash patterns
- `AGENTS.md` — golden rule, build commands, crash investigation
- `docs/protocols/heap_corruption.md` — heap-specific triage
- `docs/protocols/stack_overflow.md` — stack-specific triage
- `docs/protocols/boot_loop.md` — boot loop / flash integrity (F1–F4)
- `scripts/crash_analyzer.py` — Guru Meditation parser
