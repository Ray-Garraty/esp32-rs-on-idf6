---
type: ESP32 Reference
title: "Protocol: Heap Corruption"
description: >
  Debugging protocol for heap corruption on ESP32 with ESP-IDF v6 (TLSF allocator).
  Covers TLSF metadata corruption, misaligned addresses, and the critical insight
  that >90% of boot-time "heap corruption" is actually main task stack overflow.
tags: [esp32, debug, heap-corruption, protocol]
timestamp: 2026-07-03
---

# Protocol: Heap Corruption

## Trigger
- Guru Meditation with `A2=0xFFFFFFFC` (TLSF free-list next = -4)
- Guru Meditation with `EXCVADDR` in DRAM range (0x3FFBxxxx–0x3FFExxxx)
- `heap_caps_check_integrity_all()` fails
- `malloc()` / `pvPortMalloc()` returns NULL despite free space
- `store to misaligned address` / `LoadProhibited` inside allocator

## Background

On ESP32 with ESP-IDF v6, the heap allocator is TLSF (Two-Level Segregated Fit).
TLSF metadata (free-list heads, block headers) lives in DRAM adjacent to user
allocations. Stack overflow or buffer overflow in adjacent memory will corrupt
TLSF metadata — producing a crash that *looks* like heap corruption but is
actually a stack/buffer overflow.

**Critical insight (LL-001):** >90% of boot-time "heap corruption" crashes on
ESP32 are actually main task stack overflow. Always run S1 (stack watermark)
BEFORE assuming heap corruption.

## Steps

### Step 1: Confirm It's Really Heap (not Stack Overflow)

- Run S1 from `embedded_boot_crash.md` — check stack watermark
- Run S2 — insert `check_heap_integrity()` checkpoints
- If S1 shows low watermark (< 2048) → **this is stack overflow**, not heap.
  Switch to `docs/protocols/stack_overflow.md`.

### Step 2: Locate First Corrupting Operation

Binary search via `heap_caps_check_integrity_all()` checkpoints:

```cpp
// [INVESTIGATION] CHECK 1 — top of app_main
assert(heap_caps_check_integrity_all(true));
printf("CHECK 1: OK\n");

// ... next boot step ...

// [INVESTIGATION] CHECK N — after suspect call
assert(heap_caps_check_integrity_all(true));
printf("CHECK N: OK\n");
```

The checkpoint that FAILs identifies the operation that corrupts the heap.

### Step 3: Analyze the Corrupting Operation

Common patterns:

| Pattern | Likely cause |
|---------|--------------|
| Crash on first `std::mutex::lock()` | `pthread_mutex_t` zero-init issue. Use `PTHREAD_MUTEX_INITIALIZER` or `std::once_flag` |
| Crash on first `ESP_LOGI()` | Logger / FIFO buffer triggers first heap alloc. See Step 3a |
| Crash after GPIO init | Driver allocation corrupts heap |
| Crash after `xTaskCreate()` | Thread stack allocation fragments heap region |
| Crash in RMT motor motion | Large contiguous allocation exceeds available heap |

### Step 3a: Logger First-Alloc Analysis

If the first `ESP_LOGI()` call triggers the crash:

1. Remove `esp_log_set_vprintf()` → does the crash move or disappear?
2. Replace with direct `printf()` or `puts()` — bypasses FIFO alloc.

### Step 4: Check Linker Map

```bash
xtensa-esp32s3-elf-objdump -d -j .init_array build/ecotiter.elf
```

If `.init_array` section exists → C++ static constructors may allocate heap before `app_main()`.

### Step 5: Memory Layout Validation

```bash
xtensa-esp32s3-elf-objdump -h build/ecotiter.elf
```

Check that `.bss` / `.dram0.bss` end address does NOT overlap with heap start
(typically 0x3FCExxxx–0x3FE0xxxx on ESP32-S3).

## References

- ESP-IDF v6 Memory Layout: https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32/api-reference/system/mem_alloc.html
- TLSF allocator: https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32/api-reference/system/heap_debug.html
- `docs/lessons_learned/` LL-001
- `protocols/embedded_boot_crash.md` S1–S2
