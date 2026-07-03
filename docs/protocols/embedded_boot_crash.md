---
type: ESP32 Reference
title: "Protocol: Embedded Boot Crash"
description: >
  S1–S5 Occam's Razor protocol for debugging ESP32 boot crashes: Guru Meditation,
  WDT reset, boot hang, and incomplete flash. Mandatory systematic debugging
  procedure for the EcoTiter firmware.
tags: [esp32, debug, boot-crash, protocol, s1-s5]
timestamp: 2026-07-03
---

# Protocol: Embedded Boot Crash

## Trigger
- Guru Meditation at boot (any category)
- WDT reset (`rst:0x8`, `TG1WDT_SYS_RESET`)
- Boot hang (no serial output after startup log)
- `invalid segment length 0xffffffff` (incomplete flash)

## Mandatory Steps (S1–S5 Occam's Razor Protocol)

Execute in strict order. No exceptions.

### S1: Stack Watermark Baseline (2 min)

Insert `uxTaskGetStackHighWaterMark(NULL)` immediately after `link_patches()`:

```rust
// [INVESTIGATION] S1: stack watermark baseline
let wm = ecotiter_fw::esp_safe::stack_watermark();
log::info!("[INVESTIGATION] main task stack watermark: {wm} bytes");
```

Build, flash, monitor.

**Decision:**
- Watermark < 2048 bytes → **root cause = stack overflow**. Skip to Phase 4.
- Watermark < 4096 → **likely stack overflow**. Bump stack size 2x, test.
- Watermark >= 4096 → not a stack overflow. Proceed to S2.

### S2: Heap Integrity Pre-Check (2 min)

Insert `heap_caps_check_integrity_all(true)` before the first heap allocation
(immediately after `link_patches()`):

```rust
// [INVESTIGATION] S2: heap integrity pre-check
ecotiter_fw::esp_safe::check_heap_integrity();
log::info!("[INVESTIGATION] heap integrity: OK");
```

If heap integrity check itself crashes → ESP-IDF init issue (not Rust).
If it passes → Rust code path corrupts heap.

### S3: Smoke Test Binary (5 min)

Create `src/bin/smoke_test.rs`:

```rust
//! [INVESTIGATION] smoke test — zero allocations
fn main() {
    esp_idf_sys::link_patches();
    // No log::info!, no Mutex, no heap allocs
    loop { core::hint::spin_loop(); }
}
```

Build (`cargo +esp build --target xtensa-esp32-espidf`), flash, monitor.

**Decision:**
- Smoke test boots and stays alive → ESP-IDF init OK, problem is in Rust code.
- Smoke test crashes too → sdkconfig / ESP-IDF configuration issue.

### S4: Delta Analysis (5 min)

```bash
git log --oneline <last-known-good>..HEAD
git diff <last-known-good> HEAD -- sdkconfig.defaults
git diff <last-known-good> HEAD --stat
xtensa-esp32-elf-size target/xtensa-esp32-espidf/debug/ecotiter
xtensa-esp32-elf-objdump -h target/xtensa-esp32-espidf/debug/ecotiter
```

**Decision:**
- `.bss` grew >30% → static variable added / large buffers
- `.text` grew >50% → many new functions, stack pressure
- sdkconfig has new options → test independently (S5)

### S5: Red Flags Checklist (2 min)

Check for:
- [ ] New function returning `Vec` > 10 KB (e.g., `compute_ramp()`)
- [ ] New `Mutex<RingBuffer>` — first heap allocation trigger
- [ ] New threads spawned (stack usage compounding)
- [ ] sdkconfig changes (WebSocket, NimBLE pools, stack sizes)
- [ ] Phase N worked, Phase N+1 crashes with same hardware
- [ ] `CONFIG_ESP_MAIN_TASK_STACK_SIZE` unchanged despite main.rs growth

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

- `docs/lessons_learned.yaml` — known crash patterns
- `AGENTS.md` — golden rule, build commands, crash investigation
- `docs/protocols/heap_corruption.md` — heap-specific triage
- `docs/protocols/stack_overflow.md` — stack-specific triage
- `scripts/crash_analyzer.py` — Guru Meditation parser
