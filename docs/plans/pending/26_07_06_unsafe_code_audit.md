---
type: Plan
title: Unsafe Code Audit — Eliminate ~58% of unsafe blocks with safe Rust alternatives
description: >
  Comprehensive audit of all 57 unsafe constructs (53 `unsafe {}` blocks + 4
  `unsafe impl`) across 11 files in the firmware. 31 blocks (54%) can be
  eliminated with moderate effort — mostly by using existing safe wrappers
  (`esp_safe::*`), EspNvs from esp-idf-svc, or `std::sync::Mutex` instead of
  custom `EspMutex`. 24 blocks (42%) are architecturally required
  (panic handler, lock-free ring buffer, GPIO ISR, MMIO Send impl). Includes
  priority-ordered execution plan with effort estimates and concrete
  replacement proposals.
tags: [audit, unsafe, safety, plan]
timestamp: 2026-07-06
status: pending
---

# Unsafe Code Audit — Eliminate ~58% of unsafe blocks with safe Rust alternatives

## Summary

**Total unsafe constructs:** 57 (53 `unsafe {}` blocks + 4 `unsafe impl`) across 11 files.

**Files with `#![forbid(unsafe_code)]`:** 41 — all clean, no violations ✅

| Category | Blocks | % of Total | Action |
|----------|--------|-----------|--------|
| **Can be eliminated** | **31** | **54%** | Replace with safe wrappers, EspNvs, `std::sync::Mutex` |
| Easy wins (P0) | 11 | 19% | Use existing `esp_safe::micros()`, `esp_safe::heap_stats()`, `AtomicBool` |
| Medium (P1) | 20 | 35% | NVS → `EspNvs` (13), `EspMutex` → `std::sync::Mutex` (7) |
| Deferred (P2) | 2 | 4% | WsSender — no safe non-blocking alternative exists |
| **Must remain** | **24** | **42%** | Panic handler, ISR, lock-free ring buffer, MMIO Send impl |
| Panic handler | 7 | 12% | Raw C-struct deref + stack walk in exception context |
| Lock-free ring buffer | 3 | 5% | Volatile ops from ISR/panic — no alloc/mutex available |
| GPIO ISR | 1 | 2% | `PinDriver::subscribe()` unsafe by HAL design |
| `unsafe impl Send` (MMIO) | 1 | 2% | `PinDriver` not `Send` upstream — MMIO ptr on dual-core |
| Other FFI (panic context) | 1 | 2% | `__real_esp_panic_handler` linked via `--wrap` |

---

## Inventory

### Summary table

| File | `unsafe {}` blocks | `unsafe impl` | Primary reason | Can be safe? | Effort |
|------|-------------------|--------------|----------------|-------------|--------|
| `src/esp_safe.rs` | 21 | 0 | FFI (boot, heap, UART, panic handler, HW regs) | Partial (14/21) | Medium |
| `src/infrastructure/storage/nvs.rs` | 13 | 0 | NVS FFI (open/read/write/commit/close) | **Yes (all 13)** | Medium |
| `src/esp_mutex.rs` | 5 | 2 | pthread_mutex FFI + UnsafeCell + Send/Sync | **Yes (7/7)** | Easy |
| `src/diag/black_box.rs` | 3 | 0 | Volatile lock-free ring buffer + HW timer | **No** | N/A |
| `src/diag/stack_monitor.rs` | 3 | 0 | Const-field raw-ptr mutation + FFI watermark | **Yes (3/3)** | Easy |
| `src/diag/heap_snapshot.rs` | 3 | 0 | Read-only heap FFI | **Yes (3/3)** | Easy |
| `src/diag/tick_watchdog.rs` | 2 | 0 | Read-only timer FFI | **Yes (2/2)** | Easy |
| `src/logger.rs` | 1 | 0 | Read-only timer FFI | **Yes (1/1)** | Easy |
| `src/infrastructure/network/http_server.rs` | 1 | 1 | WebSocket async send FFI + Send for raw handle | Partial | Hard |
| `src/infrastructure/drivers/limitswitch.rs` | 1 | 0 | GPIO ISR subscribe callback | **No** | N/A |
| `src/infrastructure/drivers/onewire.rs` | 0 | 1 | Send impl for PinDriver with MMIO | **No** | N/A |
| prototype/ (6 files) | 13 | 1 | Legacy copies — not compiled in main firmware | N/A (legacy) | N/A |

### Per-file analysis

#### 1. `src/esp_safe.rs` — 21 unsafe blocks

Safe public API wrappers around ESP-IDF boot-time FFI. This is the designated "safe FFI wrapper" module.

| Lines | What | Can be safe? | Replacement |
|-------|------|-------------|-------------|
| 32–35 | `esp_task_wdt_deinit()` | No — no safe API exists | Keep |
| 47–53 | `esp_log_level_set()` | Yes — `esp_idf_svc::log::set_log_level()` | Use esp-idf-svc |
| 64–65 | `heap_caps_check_integrity_all()` | No — no safe wrapper in esp-idf-svc | Keep |
| 86–97 | `esp_get_free_heap_size()` + `heap_caps_get_largest_free_block()` ×2 | Yes — `esp_idf_svc::heap_alloc_caps()` equivalents | Use esp-idf-svc |
| 114 | `uxTaskGetStackHighWaterMark(NULL)` | No — no safe FreeRTOS task wrapper | Keep |
| 128–130 | `esp_restart()` | No — no safe system reset API | Keep |
| 144–146 | `write(1, ptr, len)` raw syscall | No — panic context | Keep |
| 180–205 | `uart_vfs_dev_register()` + `uart_driver_install()` + VFS | Partial — `UartDriver` exists but VFS binding not exposed | Keep (VFS gap) |
| 244–251 | `uart_read_bytes()` blocking read | No — LL-008 workaround (std::io::stdin panics on IDF v6) | Keep |
| 256 | `NonZeroI32::new_unchecked(ret)` | Yes — use `EspError::convert()` | Easy fix |
| 280–282 | `esp_netif_init()` | Yes — `EspNetif::new()` calls this internally | Use esp-idf-svc |
| 296–298 | `esp_coex_preference_set()` | Yes — `EspWifi` has coex methods | Use esp-idf-svc |
| 315 | `esp_wifi_deinit()` | Yes — `EspWifi::drop()` calls this | Use esp-idf-svc |
| 323 | `esp_rom_delay_us(10_000)` | Yes — `Ets::delay_us()` from esp-idf-hal | Use esp-idf-hal |
| 337–339 | `esp_timer_get_time()` as u64 | Yes — `Ets::now()` or `esp_safe::micros()` already exists | Use existing wrapper |
| 505 | `&*frame` (XtExcFrame deref) | No — panic handler walks raw stack | Keep |
| 530–535 | `read_volatile` base-save area | No — panic context, raw DRAM address | Keep |
| 588–597 | `read_volatile`/`write_volatile` UART MMIO | No — panic context, raw UART FIFO | Keep |
| 636 | `&*(info.cast::<PanicInfo>())` | No — C linker-wrapped entry point | Keep |
| 644 | `&*frame` (second deref) | No — from PanicInfo C struct | Keep |
| 706–708 | `__real_esp_panic_handler(info)` | No — via `--wrap` linker flag | Keep |

**Safe candidates (7 blocks):** lines 47, 86, 256, 280, 296, 315, 337 can use safe wrappers.

#### 2. `src/infrastructure/storage/nvs.rs` — 13 unsafe blocks

Custom NVS FFI wrappers. **ALL 13 blocks can be replaced** by `esp_idf_svc::nvs::EspNvs`.

| Lines | Operation | Replacement in `EspNvs` |
|-------|-----------|------------------------|
| 95 | `nvs_open()` | `EspDefaultNvsPartition::open()` |
| 119 | `nvs_get_u32()` | `EspNvs::get_u32()` |
| 158 | `nvs_set_u32()` | `EspNvs::set_u32()` |
| 182 | `nvs_get_i64()` | `EspNvs::get_i64()` |
| 207 | `nvs_set_i64()` | `EspNvs::set_i64()` |
| 231 | `nvs_get_u32()` (duplicate) | `EspNvs::get_u32()` |
| 256 | `nvs_set_u32()` (duplicate) | `EspNvs::set_u32()` |
| 290–297 | `nvs_get_str()` null query | `EspNvs::get_str()` |
| 327–334 | `nvs_get_str()` actual read | `EspNvs::get_str()` |
| 372 | `nvs_set_str()` | `EspNvs::set_str()` |
| 393 | `nvs_erase_key()` | `EspNvs::erase()` |
| 412 | `nvs_commit()` | Automatic with `EspNvs` (Drop) |
| 430–432 | `nvs_close()` in Drop | Automatic with `EspNvs` (Drop) |

**Trade-off:** Different ergonomics — `EspNvs` requires namespace per instance, different error types. No fundamental technical barrier.

#### 3. `src/esp_mutex.rs` — 5 blocks + 2 `unsafe impl` — **Can be eliminated entirely**

The `EspMutex` was a workaround for an old bug where `std::sync::Mutex::new()` used `mem::zeroed()` (`0x00000000`) but ESP-IDF expected `PTHREAD_MUTEX_INITIALIZER` (`0xFFFFFFFF`). **This bug is already fixed in the current toolchain** (esp 1.95.0-nightly): `std::sync::Mutex` uses `libc::PTHREAD_MUTEX_INITIALIZER` which expands to `[0xFF; 4]` for `target_os = "espidf"`.

See §P2 Item 1 for full evidence chain. Action: delete `EspMutex`, replace all uses with `std::sync::Mutex`.

**Caveat — Mutex poisoning:** `std::sync::Mutex` uses poisoning: if a thread panics while holding the lock, subsequent `lock()` calls return `Err(PoisonError)`. The guard is still accessible via `into_inner()`. The current `EspMutex` has no poisoning — a panicked thread simply unlocks via `Drop`. Callers using `if let Ok(guard) = mutex.lock()` (as in `logger.rs`) will **silently stop working** after ANY thread panics. Mitigation: use `self.inner.lock().unwrap_or_else(|e| e.into_inner())` for critical paths like logging, or `self.inner.lock()` with explicit `PoisonError` handling for less critical paths.

#### 4. `src/diag/black_box.rs` — 3 blocks

Lock-free ring buffer working from ISR/panic context. All 3 are architecturally required:
- `esp_timer_get_time()` — no safe timer API for ISR/panic context
- `write_volatile()` / `read_volatile()` — lock-free concurrency with no heap/mutex available

**Keep as-is.** Could theoretically use CAS-based ring buffer to eliminate `write_volatile`, but that introduces ABA complexity for zero safety gain.

#### 5. `src/diag/stack_monitor.rs` — 3 blocks — **Easy win**

| Lines | Current | Safe replacement |
|-------|---------|-----------------|
| 54–56 | `ptr::addr_of!(info.name) as *mut &str).write(name)` | `OnceLock<&'static str>` or `UnsafeCell` |
| 58–60 | Same pattern for `registered` | `AtomicBool` |
| 76–81 | `uxTaskGetStackHighWaterMark(NULL)` | Call `esp_safe::stack_watermark()` (already exists!) |

The raw-ptr const-field mutation (lines 54–60) is the most egregious unsafe pattern in the codebase. Fix with `AtomicBool` + `OnceLock`.

#### 6. `src/diag/heap_snapshot.rs` — 3 blocks — **Easy win**

All three call `heap_caps_get_free_size()` / `heap_caps_get_largest_free_block()` directly. Replace with `esp_safe::heap_stats()` which already wraps both.

#### 7. `src/diag/tick_watchdog.rs` — 2 blocks — **Easy win**

Both call `esp_timer_get_time()` directly. Replace with `esp_safe::micros()` which already wraps it.

#### 8. `src/logger.rs` — 1 block — **Easy win**

Calls `esp_timer_get_time()` directly. Replace with `esp_safe::micros()`.

#### 9. `src/infrastructure/network/http_server.rs` — 1 block + 1 `unsafe impl`

- `WsSender::send()` → `httpd_ws_send_frame_async()` FFI: no safe alternative in esp-idf-svc (gap in public API — no `WsDetachedSender`)
- `unsafe impl Send for WsSender`: needed because `httpd_handle_t` is `*mut c_void`

**Safe path:** Contribute `WsDetachedSender` to `esp-idf-svc`, or re-export the existing private `EspHttpWsDetachedSender`.

#### 10. `src/infrastructure/drivers/limitswitch.rs` — 1 block

`PinDriver::subscribe()` is inherently unsafe — the closure runs in interrupt context where no blocking, heap, or mutex operations are allowed. Cannot be eliminated without HAL redesign.

#### 11. `src/infrastructure/drivers/onewire.rs` — 1 `unsafe impl`

`unsafe impl Send for OneWireBus` — `PinDriver` is not `Send` upstream because register addresses are not guaranteed unique per core on all targets. On ESP32/C2/C3/S3 the MMIO addresses are identical on both cores, so this is sound but the compiler cannot prove it.

**Safe path:** Upstream `Send` impl for `PinDriver` on ESP32 family targets in `esp-idf-hal`.

---

## Priority-Ordered Execution Plan

### P0 — Immediate fix (11 blocks, ~1 hour)

Low risk, high payoff. All replacements use existing safe APIs.

```
### 🛫 Pre-Flight Checklist — P0
1. **Thread:** Main (tick_watchdog, logger) + any (heap_snapshot, stack_monitor)
2. **Blocking >10ms?** No — all replacements call existing non-blocking safe wrappers (`esp_safe::*`)
3. **Stack impact:** None — call depth identical (safe wrapper → FFI → same path as current direct FFI)
4. **Init order dep:** None
5. **FFI boundary:** No new FFI; replacing direct FFI with existing safe wrappers that internally call the same FFI
6. **Stop flag:** N/A
7. **DRAM:** None
```

| # | File | Lines | Change | Blocks eliminated | Risk |
|---|------|-------|--------|-----------------|------|
| 1 | `stack_monitor.rs` | 54–60 | Replace raw ptr const mutation with `AtomicBool` + `OnceLock` | 2 | Low |
| 2 | `stack_monitor.rs` | 76–81 | Replace direct FFI with `esp_safe::stack_watermark()` | 1 | Low |
| 3 | `heap_snapshot.rs` | 19, 21, 42 | Replace 3 direct FFI calls with `esp_safe::heap_stats()` | 3 | Low |
| 4 | `tick_watchdog.rs` | 28, 37 | Replace 2 direct FFI calls with `esp_safe::micros()` | 2 | Low |
| 5 | `logger.rs` | 84 | Replace 1 direct FFI call with `esp_safe::micros()` | 1 | Low |
| 6 | `esp_safe.rs` | 337–339 | Replace direct `esp_timer_get_time()` with `esp_safe::micros()` | 1 | Low |
| 7 | `esp_safe.rs` | 256 | Replace `NonZeroI32::new_unchecked` with `EspError::convert()` | 1 | Low |

**Verification:**
- `cargo clippy -- -D warnings` — 0 warnings
- `cargo test --lib` — all pass
- `cargo build --target xtensa-esp32-espidf` — 0 errors

### P1 — Medium effort, high impact (20 blocks, ~6 hours)

Eliminates the two largest sources of unsafe blocks: NVS FFI (13) and EspMutex (7).

```
### 🛫 Pre-Flight Checklist — P1
1. **Thread:** Main + net_owner + motor + HTTP (all call `info!()` → logger mutex)
2. **Blocking >10ms?** Mutex::lock() in logger.rs — pre-existing violation, not introduced by plan
3. **Stack impact:** std::sync::Mutex is same size as EspMutex (both store u32 handle); NVS migration uses EspNvs (heap for strings)
4. **Init order dep:** NVS must be available before WifiManager init (already true)
5. **FFI boundary:** EspMutex deletion removes 7 ffi_guard events (see GR-7 mitigation); NVS migration replaces 13 raw FFI with safe EspNvs API
6. **Stop flag:** N/A
7. **DRAM:** EspNvs allocates internal buffers (~256 bytes per namespace) — acceptable
```

#### Item 1: `nvs.rs` → `esp_idf_svc::nvs::EspNvs` (13 blocks)

Replace the entire `NvsManager` with `EspNvs` from `esp-idf-svc`.

**Steps:**
1. Audit all callers of `NvsManager` methods across the codebase
2. Map each call to the equivalent `EspNvs` API — see NVS key mapping below
3. Remove `src/infrastructure/storage/nvs.rs` and `nvs_open/read/write/commit/close` FFI wrappers
4. Update callers to use `EspDefaultNvsPartition::take()` + `EspNvs::new()`

**Risk:** Medium — `EspNvs` requires a namespace per instance. Current code uses flat key-value access. Namespace migration must preserve NVS key names for backward compatibility with existing flashed devices.

**NVS key mapping (current → EspNvs):**

| Namespace (`EspNvs::new`) | Key | Type | Used by |
|----------------------------|-----|------|---------|
| `"cal"` | `"steps_per_ml"` | f32 (stored as u32 bits) | Burette calibration |
| `"wifi"` | `"ssid"` | String (≤32 bytes) | `WifiManager::load_credentials_from_nvs()` |
| `"wifi"` | `"password"` | String (≤64 bytes) | `WifiManager::load_credentials_from_nvs()` |
| `"sys"` | `"firmware_version"` | String | First-boot OTA marker |
| `"sys"` | `"boot_count"` | u32 | Boot counter |

For `f32` values stored as `u32` bits: replace `nvs_read_f32()` / `nvs_write_f32()` with `EspNvs::get_u32()` + `f32::from_bits()` / `EspNvs::set_u32()` + `f32::to_bits()` in callers.

#### Item 2: `esp_mutex.rs` → `std::sync::Mutex` (5 blocks + 2 impl)

**Action:** Delete `src/esp_mutex.rs`. Replace all `EspMutex<T>` with `std::sync::Mutex<T>`.

**Callers to update:**

| File | Usage | Change required |
|------|-------|----------------|
| `src/logger.rs` | `EspMutex<LogBuffer>` → `std::sync::Mutex<LogBuffer>` | Lock poisoning: use `.lock().unwrap_or_else(\|e\| e.into_inner())` — logging must survive panics in other threads |
| `src/infrastructure/network/http_server.rs` | `EspMutex<BTreeMap<...>>` → `std::sync::Mutex<BTreeMap<...>>` | `try_lock()` returns `TryLockError` (two variants: `Poisoned` and `WouldBlock`). Current `if let Ok(...)` pattern will handle both as "skip" — acceptable for WebSocket broadcast. |
| `src/infrastructure/network/ble.rs` | `EspMutex<BleState>` → `std::sync::Mutex<BleState>` | Same pattern as http_server — `try_lock()` only in main loop. |

**`logger.rs` Lock Poisoning Mitigation:**

The logger's `Log::log()` is called from `info!()` / `warn!()` / `error!()` in every thread. If any thread panics while holding the logger's mutex, `std::sync::Mutex` becomes poisoned. Without mitigation, `if let Ok(buf) = self.inner.lock()` in `logger.rs:87` would silently drop all subsequent log entries.

```rust
// Required change in logger.rs:
let mut buf = self.inner.lock().unwrap_or_else(|e| e.into_inner());
//                              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// This recovers from poisoning by consuming the PoisonError and returning the guard.
// Logs are never lost, even after panics.
```

**Additional concern — `Mutex::lock()` in main loop (pre-existing):** `logger.rs` currently calls `Mutex::lock()` (blocking) from `log()` which is called by `info!()` in the main loop tick. This violates GR-1 ("`Mutex::lock()` — use `try_lock()` only"). This is a **pre-existing violation** that predates this plan. The plan does not introduce it. A future fix should change `logger.rs` to use `try_lock()` with a fallback (skip logging if contended). Documented but not addressed by this audit.

### P2 — Deferred (2 blocks, no safe non-blocking alternative)

```
### 🛫 Pre-Flight Checklist — P2 (deferred, no-action)
1. **Thread:** Main (broadcast_websocket_event)
2. **Blocking >10ms?** Deferred precisely because EspHttpWsDetachedSender::send() is blocking
3. **Stack impact:** N/A — no change
4. **Init order dep:** None
5. **FFI boundary:** WsSender keeps its 2 unsafe blocks; no change
6. **Stop flag:** N/A
7. **DRAM:** None
```

#### Item 1: `http_server.rs` — WsSender to stay as-is (1 block + 1 impl)

**Research result (2026-07-06): Keep current approach.**

`EspHttpWsDetachedSender` **exists** in `esp-idf-svc` (via `EspHttpWsConnection::create_detached_sender()`), but its `send()` is **blocking** — it uses `httpd_queue_work` + `Condvar::wait()`, blocking the calling thread until the httpd task processes the work. The project's current `WsSender::send()` calls `httpd_ws_send_frame_async` directly (fire-and-forget, non-blocking), which is strictly better for GR-1 compliance.

Internally, `EspHttpWsDetachedSender` makes the **same** `unsafe { httpd_ws_send_frame_async(...) }` call — it just wraps it in another `httpd_queue_work` layer + blocking condvar. Switching would:
- Eliminate unsafe from `http_server.rs` (moved into the library)
- **Break GR-1** — blocking call from main loop violates the 10 ms rule
- Add a double-queue overhead (pointless since the FFI already queues internally)

**Recommendation:** Keep `WsSender` as-is. The two unsafe blocks are the minimum necessary to call the C API. They are well-documented with `// SAFETY:` comments and tracked in the audit baseline.

If GR-1 strictness is a concern, a follow-up could move `broadcast_websocket_event` to a dedicated low-priority thread with `EspHttpWsDetachedSender::send()` (blocking OK in a worker thread). This would eliminate unsafe from `http_server.rs` at the cost of a new thread + channel. Deferred.

Source evidence:
- `C:\Users\vlbes\.cargo\git\checkouts\esp-idf-svc-3846902cb7f9c731/` — `EspHttpWsDetachedSender` public struct, `create_detached_sender()` public, `send()` uses `httpd_queue_work` + condvar
- `httpd_ws_send_frame_async()` internally calls `httpd_queue_work` — verified via ESP-IDF C source (the FFI already queues; `EspHttpWsDetachedSender` double-queues)

### WsSender deferred (2 blocks — no safe non-blocking alternative)

| File | Blocks | Reason |
|------|--------|--------|
| `http_server.rs` | 2 | `EspHttpWsDetachedSender` exists but is blocking; project needs fire-and-forget for GR-1 |

Same pattern as `esp-idf-svc` itself uses. Deferred unless a non-blocking safe wrapper emerges upstream.

### Wontfix (24 blocks — architecturally required)

| File | Blocks | Reason | Mitigation |
|------|--------|--------|------------|
| `esp_safe.rs` (panic handler) | 7 | C-struct deref + raw stack walk in exception context | Existing SAFETY comments are thorough |
| `black_box.rs` | 3 | Lock-free volatile ring buffer for ISR/panic logging | Documented invariant: single-writer (ISR) multiple-reader (dump) |
| `limitswitch.rs` | 1 | GPIO ISR subscribe | Wrap in safe `LimitSwitch::new()` (already done) |
| `onewire.rs` | 1 | `unsafe impl Send` for MMIO PinDriver | `// SAFETY:` comment confirms dual-core address invariance |
| `esp_safe.rs` (VFS UART, WDT, restart) | 6 | No safe API exists in esp-idf-svc for these operations | Monitor esp-idf-svc releases for new safe wrappers |
| `esp_safe.rs` (UART stdin reader) | 1 | LL-008 workaround — std::io::stdin panics on IDF v6 | Documented in lessons_learned.yaml |
| `esp_safe.rs` (heap integrity) | 1 | No safe wrapper for `heap_caps_check_integrity_all` | Low-value diagnostic only |
| `esp_safe.rs` (watermark FFI) | 1 | No safe FreeRTOS task wrapper | Low-value diagnostic only |
| `esp_safe.rs` (__real panic handler) | 1 | `--wrap` linker entry point | Required by linker contract |
| `esp_safe.rs` (write syscall) | 1 | Panic context output | No heap/alloc available |

---

## Effort Summary

| Priority | Blocks | Est. time | Impact |
|----------|--------|-----------|--------|
| P0 — Easy wins | 11 | ~1 hour | 11 blocks eliminated (19%) |
| P1 — Mutex deletion | 7 | ~2 hours | 7 blocks eliminated (12%) — deletions beat adding |
| P1 — NVS migration | 13 | ~4 hours | 13 blocks eliminated (23%) |
| P2 — WsSender gap | 2 | N/A | **Deferred** — no safe non-blocking alternative |
| Wontfix | 24 | N/A | 42% of total |

**Total eliminable:** 31 blocks (54%) for ~7 hours of work.

---

## Dependencies

| Item | Depends on | Blocked by |
|------|-----------|------------|
| P0 #1-7 | None — all use existing safe wrappers | Nothing |
| P1 — Mutex deletion | Audit of all `EspMutex` callers | Nothing |
| P1 — NVS | Audit of all `NvsManager` callers | Nothing |
| P2 — WsSender | Upstream `esp-idf-svc` non-blocking async WS API | Deferred indefinitely |

---

## Verification

Each item must pass before moving to the next:

- [ ] `cargo build --target xtensa-esp32-espidf` — 0 errors
- [ ] `cargo clippy --target xtensa-esp32-espidf -- -D warnings` — 0 warnings
- [ ] `cargo test --lib` — all host tests pass
- [ ] `scripts/check_unsafe.py` — unsafe block count matches target (11 fewer after P0, 18 fewer after P1)
- [ ] `scripts/check_unsafe.py` baseline updated in file (reflects new count after changes)
- [ ] AGENTS.md §8.3 baseline updated to match new count
- [ ] No `unwrap()` / `expect()` / `panic!()` / `todo!()` introduced (boot-path `.expect()` only)
- [ ] No new `unsafe` blocks introduced
- [ ] Every remaining `unsafe { }` has a preceding `// SAFETY:` comment
- [ ] `timeout 30 python3 scripts/serial_monitor.py` — 30-second hardware smoke test: no Guru Meditation, no WDT, no panics

---

## Files affected

| File | Change |
|------|--------|
| `src/diag/stack_monitor.rs` | Replace raw-ptr const mutation with `AtomicBool` + `OnceLock`; use `esp_safe::stack_watermark()` |
| `src/diag/heap_snapshot.rs` | Replace direct FFI with `esp_safe::heap_stats()` |
| `src/diag/tick_watchdog.rs` | Replace direct FFI with `esp_safe::micros()` |
| `src/logger.rs` | Replace direct FFI with `esp_safe::micros()` |
| `src/esp_safe.rs` | (P0) Replace direct timer FFI with `micros()`; fix NonZeroI32 construction; (P2) no change |
| `src/infrastructure/storage/nvs.rs` | (P1) Replace entire file with `EspNvs` — potentially delete `NvsManager` |
| `src/esp_mutex.rs` | (P1) **Delete entire file** — `std::sync::Mutex` works correctly on current toolchain |
| `src/infrastructure/network/http_server.rs` | (P2) **Deferred** — WsSender stays as-is; no safe non-blocking API exists |
| Any files importing `NvsManager` | (P1) Update to `EspNvs` API |
| Any files importing `EspMutex` | (P1) Replace with `std::sync::Mutex`; adjust `try_lock()` callers for `WouldBlock` |

---

## GR-7 Diagnostic Instrumentation — Mitigation

The `EspMutex` deletion (P1 Item 2) removes 7 `ffi_guard::record_enter/exit` events that tracked `pthread_mutex_lock/unlock/trylock` FFI calls.

**Assessment of diagnostic value loss:**
- `EspMutex` was used in `logger.rs`, `http_server.rs` (WS_SESSIONS), and `ble.rs`. Lock contention is rare in all three — logging is fast, WS broadcast is fast, BLE `process()` is periodic.
- The `ffi_guard` mutex events **never triggered** in any of the 7 documented post-mortems (2026-07-01 to 2026-07-04). Mutex-related crashes have never been a root cause.
- The black box has 64 slots. These 7 events consumed ~11% of capacity for near-zero diagnostic value.

**Mitigation:** Replace the lost events with an `AtomicU64` lock-acquisition counter (zero FFI, zero allocation):

```rust
// In a shared location (e.g., diag/ or logger.rs):
static MUTEX_LOCK_COUNT: AtomicU64 = AtomicU64::new(0);

// Before each std::sync::Mutex::lock() call:
MUTEX_LOCK_COUNT.fetch_add(1, Ordering::Relaxed);
```

The counter is accessible from the crash black box (can be sampled on each main-loop tick). It does not track individual lock/unlock pairs but provides contention trends with 0 bytes of FFI overhead.

**Net diagnostic change:** −7 `ffi_guard` events, +1 `AtomicU64` counter. Net coverage: adequate.

---

## AGENTS.md Baseline Discrepancy

AGENTS.md §8.3 states: *"Total unsafe blocks: 32 (Last audited: 2026-07-03, baseline in `scripts/check_unsafe.py`)"*.

**Actual count:** 57 unsafe constructs (baseline in `scripts/check_unsafe.py` is 54). AGENTS.md is out of date by 22+ blocks.

**Action:** After implementing P0 + P1 (26 blocks eliminated), update AGENTS.md §8.3 and `scripts/check_unsafe.py` baseline to reflect ~31 blocks. These updates must happen **before** the first commit of P0.

---

## Risk assessment

| Risk | Level | Mitigation |
|------|-------|------------|
| NVS migration breaks backward compatibility with flashed devices | MEDIUM | Preserve all NVS key names; test by reading existing NVS data |
| `std::sync::Mutex` sentinel mismatch | LOW | Evidence confirmed — current toolchain uses `PTHREAD_MUTEX_INITIALIZER` correctly; test with `cargo test --lib` + hardware smoke test |
| **`std::sync::Mutex` poisoning silences logs** | **MEDIUM** | Use `.lock().unwrap_or_else(\|e\| e.into_inner())` in `logger.rs` to recover from poisoning. Documented in P1 Item 2. |
| logger.rs uses `Mutex::lock()` in main loop (pre-existing) | MEDIUM | This violates GR-1 ("use `try_lock()` only"). **Not introduced by this plan.** Deferred — would require changing logger to `try_lock()` with fallback (skip log if contended). |
| P0 changes introduce regression in timer-dependent logic | LOW | All P0 changes use identical API — `esp_safe::micros()` calls the same FFI underneath |
| GR-7 diagnostic loss: 7 ffi_guard events removed | LOW | Mitigated: replaced with `AtomicU64` lock counter (see §GR-7 Mitigation above) |
| Clippy discovers new warnings from refactored code | LOW | Fix as part of implementation; target 0 warnings as always |
| Mismatch between plan unsafe counts and AGENTS.md baseline | LOW | Update AGENTS.md §8.3 and `scripts/check_unsafe.py` before first commit |

---

## Related Documentation

- `AGENTS.md` §8.3 — Unsafe policy, block count baseline, per-file justification
- `AGENTS.md` §7 — Crash investigation (diagnostic instrumentation relies on unsafe black_box)
- `scripts/check_unsafe.py` — Unsafe block count enforcement
- `docs/lessons_learned.yaml` — LL-001 (stack overflow), LL-005 (heap fragmentation), LL-007 (C assert uncatchable)
