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

Binary search via `check_heap_integrity()` checkpoints:

```rust
// [INVESTIGATION] CHECK 1 — after link_patches
check_heap_integrity();
log::info!("CHECK 1: OK");

// ... next boot step ...

// [INVESTIGATION] CHECK N — after suspect call
check_heap_integrity();
log::info!("CHECK N: OK");
```

The checkpoint that FAILs identifies the operation that corrupts the heap.

### Step 3: Analyze the Corrupting Operation

Common patterns:

| Pattern | Likely cause |
|---------|--------------|
| Crash on first `Mutex::lock()` | `pthread_mutex_t` zero-init issue. Use `EspMutex` with `PTHREAD_MUTEX_INITIALIZER` (0xFFFFFFFF) |
| Crash on first `log::info!()` | Logger's `Mutex<RingBuffer>` triggers first heap alloc. See Step 3a |
| Crash after `Peripherals::take()` | GPIO/peripheral allocation corrupts heap |
| Crash after `std::thread::spawn()` | Thread stack allocation overlaps heap region |
| Crash in `compute_ramp()` | `Vec` allocation > available heap |

### Step 3a: Logger First-Alloc Analysis

If the first `log::info!()` call triggers the crash:

1. Comment out `logger::init()` → does the crash move or disappear?
2. Replace `std::sync::Mutex` with raw `pthread_mutex_t` init:
   ```rust
   let mut m = MaybeUninit::<libc::pthread_mutex_t>::uninit();
   unsafe {
       libc::pthread_mutex_init(m.as_mut_ptr(), core::ptr::null());
   }
   ```
3. Build & test. If crash disappears → `std::sync::Mutex::new()` zero-init is incompatible with ESP-IDF v6.

### Step 4: Check .init_array

```bash
xtensa-esp32-elf-objdump -d -j .init_array target/xtensa-esp32-espidf/debug/ecotiter
```

If section exists → C++ static constructors may allocate heap before `main()`.
If "not found" → no C++ static constructors.

### Step 5: Memory Layout Validation

```bash
xtensa-esp32-elf-objdump -h target/xtensa-esp32-espidf/debug/ecotiter
```

Check that `.bss` / `.dram0.bss` end address does NOT overlap with heap start
(typically 0x3FFDxxxx–0x3FFExxxx on ESP32).

## References

- ESP-IDF v6 Memory Layout: https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32/api-reference/system/mem_alloc.html
- TLSF allocator: https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32/api-reference/system/heap_debug.html
- `docs/lessons_learned/` LL-001
- `protocols/embedded_boot_crash.md` S1–S2
