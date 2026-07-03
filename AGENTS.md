# AGENTS.md — AI Agent Rules for EcoTiter Firmware (ESP32 + Rust + ESP-IDF v6)

This file defines **non-negotiable rules** for AI coding agents working on this
firmware. Violations of CRITICAL rules invalidate all changes and require
immediate revert. Every rule is derived from a real post-mortem or hardware
failure — read them before generating any code.

---

## 1. 🚨 CRITICAL RULES (Auto-Revert)

These rules are **non-negotiable**. Any generated code violating them must be
rejected by the agent itself before being shown to the user.

### GR-1: NEVER BLOCK THE MAIN LOOP

The main loop (`main.rs`, FreeRTOS task `main`) must **NEVER** execute a
blocking operation. Any call that can block for > 10 ms MUST live in a
dedicated thread/task.

**Forbidden in main loop:**
- `send_and_wait()` (RMT)
- `thread::sleep()` > 10 ms (10 ms pacing tick is the only exception)
- `recv()` / `recv_timeout()` on unbounded channels
- `Mutex::lock()` — use `try_lock()` only
- Any synchronous I/O (`std::fs::*`, blocking HTTP client, `write()` on
  blocking fd)

**Allowed in main loop:**
- `Atomic*.load/store/fetch_*`
- `try_lock()` with immediate `Err` handling
- Non-blocking `poll()` / `process()` functions
- `sleep(Duration::from_millis(10))` — pacing tick only
- Writes to pre-opened non-blocking fds

Violation → all changes invalidated, immediate revert.

📌 *Failure (2026-07-03): Homing called `send_and_wait()` in main, blocking
for 11 s. WiFi/BLE init never ran.*

### GR-2: MANDATORY RMT STOP FLAGS

Every RMT motion function **MUST** accept and check an `Option<&AtomicBool>`
stop flag between chunks. If set → motion aborts immediately.

```rust
pub fn move_steps_intervals(
    &mut self,
    intervals: &[u32],
    stop_flag: Option<&AtomicBool>,  // ← MANDATORY
) -> Result<(), StepperError> {
    for chunk in intervals.chunks(CHUNK_SIZE) {
        if let Some(flag) = stop_flag {
            if flag.load(Ordering::Acquire) {
                self.emergency_stop()?;
                return Err(StepperError::LimitSwitchTriggered);
            }
        }
        self.channel.send_and_wait(encoder, signal, &config)?;
    }
}
```

📌 *Failure (2026-07-03): Homing omitted stop flag. Motor ran through FULL
limit switch.*

### GR-3: DRAM INIT ORDER (The Triangle)

ESP-IDF v6 on ESP32 has a three-way DRAM conflict. Init order MUST be:

```
1. WiFi driver init (low DRAM cost, ~3.5 KB)
2. HTTP server task (needs 12 KB contiguous MALLOC_CAP_INTERNAL)
3. BLE NimBLE host (needs 12 KB contiguous MALLOC_CAP_INTERNAL)
```

WiFi must obtain IP before HTTP server binds to `0.0.0.0:80`.

**Forbidden reorderings:**
❌ HTTP → BLE → WiFi (7+ s HTTP latency, lwIP routing delay)
❌ BLE → WiFi → HTTP (`ESP_ERR_HTTPD_TASK` — xTaskCreate fails)
❌ WiFi → BLE → HTTP (`wifi:fail to alloc timer, type=9`)

**Required pattern in `net_owner` thread:**
```rust
wifi.init()?;
wifi.wait_for_ip(Duration::from_secs(5))?;
http_server = HttpServer::new()?;
ble.init()?;
```

📌 *Failures (2026-07-01): Each reordering fixed one issue but broke another.
This order is the only one that works with current buffer config.*

### GR-4: COEXISTENCE POLICY — NEVER PREFER BT

**Forbidden:**
- `esp_coex_preference_set(ESP_COEX_PREFER_BT)` — deprecated in ESP-IDF v6
- `ble.set_coex_ble_preferred()`

Use default `ESP_COEX_PREFER_BALANCE` — the ESP32 coexistence arbitrator
automatically gives 50/50 airtime.

📌 *Failure (2026-07-01): `ESP_COEX_PREFER_BT` starved WiFi L2 of airtime.
STA associated, got IP, but 100% TCP packet loss.*

### GR-5: NO RAW ESP-IDF POINTERS ACROSS TASK BOUNDARIES

**Forbidden:** Storing `*mut httpd_req_t`, `*mut httpd_ws_frame_t`, or any
ESP-IDF opaque pointer in `Arc`, `AtomicPtr`, `Box`, or passing them across
thread boundaries.

Once a C-side HTTP handler returns, ESP-IDF frees or recycles the request
structure. Any stored pointer → dangling → Guru Meditation: StoreProhibited.

**Required:** Use native WebSocket API (`ws_handler` + `EspHttpWsConnection`)
for real-time streaming, OR copy data into `heapless::Vec`/`heapless::String`
before leaving the C callback.

📌 *Failure (2026-07-02): SSE stored `Arc<AtomicPtr<httpd_req_t>>` in main
loop. Crash with EXCVADDR=0x28. Replaced with WebSocket API.*

### GR-6: STACK BUDGET IS LAW

Every `std::thread::Builder::new()` call MUST specify `stack_size` from the
approved budget table below. No exceptions. No defaults.

| Thread | Stack Size | Notes |
|---|---|---|
| Main loop (FreeRTOS `main` task) | **32768** (32 KB) | `CONFIG_ESP_MAIN_TASK_STACK_SIZE` — LL-001 requires 32 KB |
| net_owner (WiFi/BLE/HTTP lifecycle) | 16384 (16 KB) | GR-3 init order owner thread |
| Motor (RMT stepper + homing) | 16384 (16 KB) | ⚠️ Was 4 KB → stack overflow on homing |
| Temperature (DS18B20 bitbang) | 16384 (16 KB) | Bitbang call chain is deep |
| BLE notify | 8192 (8 KB) | Host stack is 12288 (separate FreeRTOS task) |
| HTTP server (FreeRTOS internal) | 12288 (12 KB) | `stack_size: 12288` mandatory |
| `std::thread` default | 8192 (8 KB) | `CONFIG_PTHREAD_TASK_STACK_SIZE_DEFAULT` |

**Forbidden inside threads with stack ≤ 8 KB:**
- `format!()` / `println!()` in loops (allocate String on stack)
- `serde_json::to_string()` — use `heapless::String` + `write!` instead
- Large stack-local arrays (`let buf: [u8; 4096]`)
- Deep recursion
- `format_args!()` with many temporaries

**Mandatory check:** After moving any code path between threads, verify with
`uxTaskGetStackHighWaterMark()` or at minimum log watermark before/after.

📌 *Failure (2026-07-03): Moving homing to 8 KB motor task caused stack
overflow. Increased to 16 KB.*

---

## 2. 🔲 PRE-FLIGHT CHECKLIST (Copy Before Codegen)

Before generating any code that touches: **threads, network, RMT, FFI, mutexes,
channels, GPIO ISR, NVS, WiFi, BLE, or HTTP** — copy and fill this block:

```
### 🛫 Pre-Flight Checklist
1. **Thread:** ________ (Main/Motor/Temp/BLE/HTTP/net_owner)
2. **Blocking >10ms?** ________ (Yes → move to worker: ________)
3. **Stack impact:** format!/String/arrays/recursion? ________ Budget: ________ KB
4. **Init order dep:** WiFi IP / NimBLE / HTTP / none? ________
5. **FFI boundary:** Stores C pointers? ________ Copies before return? ________
6. **Stop flag:** RMT/motion? ________ (if yes: GR-2 REQUIRED)
7. **DRAM:** MALLOC_CAP_INTERNAL? ________ Position in init order? ________
```

If you cannot confidently fill this → **stop and ask the user.**

---

## 3. ⚡️ EMBEDDED HARDWARE INVARIANTS

### 3.1 GPIO Pinout

| GPIO | Function | Constraint |
|---|---|---|
| 1 | U0TXD | Serial output — **DO NOT TOUCH** |
| 3 | U0RXD | Serial input — **DO NOT TOUCH** |
| 2 | On-board LED (?) | Available |
| 4–5, 12–19, 21–27 | General purpose | Available |
| 32 | Endstop FULL | `PinDriver::input` + PosEdge ISR → `AtomicBool` |
| 35 | Endstop HOME | `PinDriver::input` + PosEdge ISR → `AtomicBool` |
| EN | TMC2209 EN | Active LOW → `set_low()` in constructor |

**GPIO pin constructors have private fields** — always use
`peripherals.pins.gpioXX.degrade_output()`.

### 3.2 RMT Stepper API (esp-idf-hal v0.46, IDF v6)

```rust
// Creation
TxChannelDriver::new(pin: impl OutputPin, config: &TxChannelConfig) -> Result<Self, EspError>
CopyEncoder::new() -> Result<CopyEncoder, EspError>

// Blocking transmit (MOTOR THREAD ONLY — never in main loop)
channel.send_and_wait(encoder, signal, &TransmitConfig{..})
```

- `RmtChannel` trait must be in scope for `disable()`
- `PinDriver<'d, Output>` has **1** generic arg (MODE), not 2
- `EspError` is from `esp_idf_sys`, NOT `esp_idf_hal`
- EN pin active LOW: call `set_low()` in constructor

### 3.3 WDT & Brownout

**WDT must be disabled at boot** — RMT `send_and_wait()` blocks > 250 ms.

```rust
// Use safe wrapper only — never call esp_task_wdt_deinit() directly
ecotiter_fw::esp_safe::disable_wdt();
```

**Brownout detector:** Disabled via `sdkconfig.defaults`:
```
CONFIG_BROWNOUT_DET=n
```
NOT via `WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0)` at runtime.

### 3.4 Peripheral Drivers

| Driver | File | Notes |
|---|---|---|
| TMC2209 | — | EN active LOW → `set_low()` in constructor |
| Endstops | `infrastructure/drivers/limitswitch.rs` | GPIO32/35, PosEdge ISR, 1 unsafe block |
| DS18B20 (1-Wire) | `infrastructure/drivers/onewire.rs` | MMIO bitbang, 1 unsafe (`unsafe impl Send`) |
| NVS | `infrastructure/storage/nvs.rs` | 13 unsafe blocks (FFI wrappers) |

---

## 4. 🧵 THREAD MODEL & STACK BUDGET

| Thread | Role | Stack | Constraint |
|---|---|---|---|
| `main` (FreeRTOS) | Pacing tick, dispatch, atomic reads | 32768 | GR-1: no blocking |
| `net_owner` | GR-3 init, WiFi/BLE/HTTP lifecycle | 16384 | Must follow GR-3 order |
| `motor` | RMT stepper, homing | 16384 | GR-2: mandatory stop flag |
| `temp` | DS18B20 bitbang reads | 16384 | Blocking reads OK here |
| `ble_notify` | BLE notify pushes | 8192 | No `format!`/serde |
| `httpd` (FreeRTOS) | HTTP handlers | 12288 | GR-5: no stored C pointers |

**Cross-thread communication:**
- `Arc<AtomicBool>` for stop flags / status flags
- `mpsc` channels for event passing
- `Mutex` only with `try_lock()` in main loop

---

## 5. 🌐 NETWORK STACK

### 5.1 Init Order (GR-3)

See GR-3 above. WiFi → HTTP → BLE. **Never reorder.**

### 5.2 WiFi

- Custom `EspNetif` for AP mode with `swap_netif_ap()` before `wifi.start()`
  — fixes DHCP on `192.168.4.1/24`.
- mDNS requires valid IP — call `EspMdns::take()` only after `GOT_IP` event /
  `wait_for_ip()` returns.
- DNS responder: `UdpSocket` bound to AP_IP:53 first, fallback `0.0.0.0:53`.

### 5.3 BLE / NimBLE

- **Local patch required for IDF v6:** add `all(esp_idf_version_major = "6")`
  to two `cfg_if!` blocks in `ble_characteristic.rs` (via `build.rs`).
- **Pre-init guard mandatory:** `initialized: bool` in `BleManager`;
  `process()` / `is_connected()` early-return if not initialized.
- Never call `BLEDevice::take().get_server()` before `init()` — global
  statics with internal mutexes, will panic.
- **3-level zombie defense:**
  - L1: 5 consecutive notify failures → disconnect
  - L2: `connected_count() == 0` but local flag set → cleanup
  - L3: immediate kill on notify with zero connections but flag set

### 5.4 HTTP Server

- `stack_size: 12288` mandatory — prevents Rust handler stack overflow.
- Use WebSocket (`ws_handler` + `EspHttpWsConnection`) for real-time streaming.
- Use `Box::leak(Box::new(...))` for `'static` refs satisfying `fn_handler`'s
  `'static` bound — simpler than `Arc` for single non-`Send` values.
- Never store `*mut httpd_req_t` across handler returns (GR-5).

### 5.5 Coexistence (GR-4)

Default `ESP_COEX_PREFER_BALANCE`. Never prefer BT (GR-4).

---

## 6. 🔧 BUILD & CI

### 6.1 Commands

| Command | Purpose | Timeout |
|---|---|---|
| `scripts/build.sh` | Build for xtensa target | ≥ 300 s |
| `scripts/build.sh check` | Fast cargo check | 60 s |
| `scripts/build.sh clippy` | Clippy (xtensa) — 0 warnings | 60 s |
| `scripts/build.sh clippy-host` | Clippy (host, lib only) | 30 s |
| `scripts/build.sh test` | Host unit tests | 60 s |
| `scripts/build.sh fmt` | Format check | 30 s |
| `scripts/build.sh flash [port]` | Flash firmware | ≥ 180 s |
| `timeout 30 python3 scripts/serial_monitor.py` | Smoke test | 30 s |

**After sourcing `scripts/build.sh`**, verify paths with `type <tool>`
(e.g., `type esptool`, `type cargo`). **Never guess command names.**

**WDT must be disabled during debugging:** `ecotiter_fw::esp_safe::disable_wdt()`

### 6.2 sdkconfig Policy

- Edit **only** `sdkconfig.defaults` — never `sdkconfig` (auto-generated).
- Never run `idf.py menuconfig` (not reproducible).
- After changing defaults, run `idf.py reconfigure` to regenerate `sdkconfig`.
- Key defaults:
  - `CONFIG_ESP_MAIN_TASK_STACK_SIZE=32768`
  - `CONFIG_BROWNOUT_DET=n`
  - `CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=12288`

### 6.3 Partitions & Dependencies

- **Partition table** in `partitions.csv` — do not change without explicit
  approval (affects OTA, NVS layout).
- **Dependencies:** no `"*"` version specifiers. Lock file (`Cargo.lock`)
  committed. Patches via `[patch]` section only.

---

## 7. 🐛 CRASH INVESTIGATION

Any ESP32 crash (Guru Meditation, StoreProhibited, LoadProhibited, stack
overflow, abort, WDT) is a **RED ALERT**.

**Step 1:** Consult `docs/lessons_learned.yaml` FIRST (known patterns below).

**Step 2:** Invoke the debugger agent:
```
Task(@debugger, "Crash dump: <paste Guru Meditation text>
known_good: <last working commit hash>")
```

**Step 3:** Or run the crash analyzer:
```bash
cat crash_dump.txt | python3 scripts/crash_analyzer.py
```

### Known Patterns (from `docs/lessons_learned.yaml`)

| Signature | Real Cause | Fix |
|---|---|---|
| A2=0xFFFFFFFC, EXCVADDR=0x0, `tlsf_check`, `heap_caps_*free` | Stack overflow, NOT heap corruption (**LL-001**) | `CONFIG_ESP_MAIN_TASK_STACK_SIZE=32768`; check `uxTaskGetStackHighWaterMark(NULL)` FIRST |
| Unknown `rmt_encode_state_t` value: 3 | esp-idf-hal bitmask bug (**LL-002**) | Bitwise patch in `~/.cargo/git/checkouts/esp-idf-hal-*/24e99b8/src/rmt/encoder.rs` |
| `ESP_ERR_HTTPD_TASK` (45064) | DRAM fragmentation | Init order GR-3; move BLE to net_owner |
| `wifi:fail to alloc timer, type=9` | WiFi timer alloc after BLE+HTTP ate DRAM | Reduce WiFi buffer counts in `sdkconfig.defaults` |
| StoreProhibited EXCVADDR=0x28 | Dangling `httpd_req_t` (GR-5) | WebSocket API; never store C pointers |

### Debugger Agent (@debugger)

- **Methodology:** S1–S5 Occam's Razor Protocol
- **Protocols:**
  - `docs/protocols/embedded_boot_crash.md` — mandatory S1–S5
  - `docs/protocols/heap_corruption.md` — heap-specific triage
  - `docs/protocols/stack_overflow.md` — stack-specific triage
- **Tools:** `scripts/crash_analyzer.py`, `scripts/decode_backtrace.sh`
- **Knowledge base:** `docs/lessons_learned.yaml`

---

## 8. 🛠 PROJECT CONVENTIONS

### 8.1 Coding Style

- DRY, KISS, YAGNI.
- Prefer cohesive units over fragmented micro-modules.
- Extract non-obvious constants to config module or top of file.
- Never break existing functionality or business logic.
- Never invent methods/hooks/APIs — verify against official docs or source.
- Never fix symptoms; always find root cause.
- Never assert physical-world events — ask the user what they observe.
- Show only changed fragments with file paths.
- LF (`\n`) line endings. No trailing whitespace.
- All pipelines: 0 errors, 0 warnings — no "pre-existing" excuses.
- Two-strike rule: 2 attempts per task, then stop and consult user.

### 8.2 Error Handling

- Typed errors (`thiserror` + `enum`) in library code. `anyhow` for
  application glue.
- **No `unwrap()` / `expect()` / `panic!()` / `todo!()` / `unreachable!()`
  in production code.**
- Use `?` with `.context()` (`anyhow::Context`) for error propagation.
- No `esp-idf-*` imports in `domain/` layer.

### 8.3 Unsafe Policy

**Total unsafe blocks: 32** (Last audited: 2026-07-03, baseline in
`scripts/check_unsafe.py`)

**Modules with `#![forbid(unsafe_code)]`:**
See `docs/plans/pending/26_06_30_unsafe_code_audit.md` Step 4 for the
complete list. These modules must never contain `unsafe` code.

**Modules with controlled unsafe:**

| File | Blocks | Reason |
|---|---|---|
| `infrastructure/storage/nvs.rs` | 13 | NVS FFI wrappers inside safe public API |
| `esp_mutex.rs` | 7 | `pthread_mutex_lock`/`unlock`/`trylock` + `unsafe impl Sync/Send` + `UnsafeCell` deref |
| `esp_safe.rs` | 7 | Safe wrappers around ESP-IDF boot-time FFI calls |
| `infrastructure/network/http_server.rs` | 2 | WebSocket `httpd_ws_send_frame_async` |
| `infrastructure/drivers/limitswitch.rs` | 1 | GPIO ISR `subscribe()` callback |
| `infrastructure/drivers/onewire.rs` | 1 | `unsafe impl Send` for MMIO-based PinDriver |
| `logger.rs` | 1 | `esp_timer_get_time()` inside safe `Log::log()` fn |

**Enforcement:**
1. Every `unsafe { }` MUST have a preceding `// SAFETY:` comment with
   invariant, context, and risk.
2. New unsafe blocks require justification in the commit message.
3. `scripts/build.sh clippy` must pass (includes `undocumented_unsafe_blocks`
   lint and `unsafe_op_in_unsafe_fn`).
4. `scripts/check_unsafe.py` runs on every commit — checks documentation +
   count baseline.
5. Do NOT add `#[allow(unsafe_code)]` to override `forbid(unsafe_code)` in
   safe modules.
6. Avoid unsafe code at all whenever possible.

### 8.4 Logging & Firmware Versioning

- **Crate:** `log` + `esp_idf_svc::log::EspLogger`.
- **Levels:** `error!`, `warn!`, `info!`, `debug!`, `trace!`.
- **Tag:** module path or component name convention.
- **Firmware version:** `package.version` from `Cargo.toml`. Pushed to NVS
  `firmware_version` on first boot after OTA.

---

## 9. 🔌 SERIAL & PYTHON SAFETY

### 9.1 Serial Port Safety

- **ABSOLUTELY FORBIDDEN** to launch background processes that hold serial
  ports.
- Any blocking/serial tool (pio device monitor, serial terminal, etc.) MUST
  be run with an explicit timeout via the bash tool's `timeout` parameter or
  via `timeout <seconds>` prefix.
- Never leave a process occupying a serial port (ttyUSB/ttyACM) after exit.

```
# ✅ CORRECT
timeout 30 python3 scripts/serial_monitor.py

# ❌ FORBIDDEN — hangs forever
python3 -m serial.tools.miniterm COM5 115200
```

### 9.2 Python Script Rules

- NEVER use `python -c "..."` inline scripts — bash on Windows mangles
  quotes/backslashes.
- ALWAYS write Python code to a temp file first, then run it.
  Use `/tmp/opencode` for temp scripts.

```bash
# ✅ CORRECT
cat > "$TMPDIR/test_serial.py" << 'PYEOF'
import serial
PYEOF
python "$TMPDIR/test_serial.py"

# ❌ FORBIDDEN
python -c "import serial; print(serial.Serial('COM5'))"
```

---

## 10. ✅ FINAL COMMIT CHECKLIST

Before submitting any change, verify:

- [ ] `scripts/build.sh` — 0 errors, 0 warnings
- [ ] `scripts/build.sh clippy` — 0 warnings
- [ ] `scripts/build.sh test` — all host tests pass
- [ ] No `unwrap()`/`expect()`/`panic!()`/`todo!()`/`unreachable!()` in production
- [ ] No `Vec`/`String` in main loop or motor thread hot paths (use `heapless`)
- [ ] No `esp-idf-*` imports in `domain/` layer
- [ ] Main loop has NO blocking operations (GR-1)
- [ ] Every RMT motion has a stop flag (GR-2)
- [ ] Init order follows GR-3 triangle (WiFi → HTTP → BLE)
- [ ] `CONFIG_ESP_MAIN_TASK_STACK_SIZE=32768`
- [ ] Motor thread stack = 16384 (not 4096/8192)
- [ ] Pre-Flight Checklist (§2) was filled out BEFORE code generation
- [ ] No new unsafe without `// SAFETY:` comment + commit justification
- [ ] No raw ESP-IDF pointers cross thread boundaries (GR-5)
- [ ] Frozen `docs/API/SERIAL_API.md` contract respected
- [ ] 30-second serial smoke test: no Guru Meditation, no WDT, no panics

---

## Appendix A: Forbidden Patterns Quick Reference

| ❌ Forbidden | ✅ Required | Rule |
|---|---|---|
| `send_and_wait()` in main loop | Move to motor thread | GR-1 |
| `Mutex::lock()` in main loop | `try_lock()` with `Err` handling | GR-1 |
| `format!()` in hot paths | `heapless::String` + `write!` | GR-6 |
| `*mut httpd_req_t` across threads | WebSocket API (`ws_handler`) | GR-5 |
| `esp_coex_preference_set(PREFER_BT)` | Default `PREFER_BALANCE` | GR-4 |
| Homing in main thread | Motor task (16 KB stack) | GR-1, GR-6 |
| BLE init before HTTP | GR-3: WiFi → HTTP → BLE | GR-3 |
| `BLEDevice::take()` before `init()` | Pre-init guard + early return | §5.3 |
| `EspMdns::take()` before IP assigned | Wait for `GOT_IP` event | §5.2 |
| `python -c "..."` inline | Write temp file, then run | §9.2 |
| Guessing `stack_size` | Budget table in GR-6 | GR-6 |
| RMT motion without stop flag | Stop flag before start | GR-2 |
| `WRITE_PERI_REG` for brownout | `CONFIG_BROWNOUT_DET=n` | §3.3 |
| HTTP bind without IP | `wait_for_ip()` first | GR-3 |
| `esp_task_wdt_deinit()` direct | `ecotiter_fw::esp_safe::disable_wdt()` | §3.3 |

## Appendix B: Reference Documentation

| Document | Purpose |
|---|---|
| `docs/refs/project.md` | Hardware pinout, thread model, error hierarchy, state machines, NVS layout |
| `docs/refs/coding_style.md` | 4-layer architecture, enum over trait, heapless hot paths, concurrency rules |
| `docs/lessons_learned.yaml` | Crash patterns & fixes (LL-001, LL-002, etc.) |
| `docs/protocols/embedded_boot_crash.md` | S1–S5 Occam's Razor Protocol |
| `docs/protocols/heap_corruption.md` | Heap triage (often misdiagnosed stack overflow) |
| `docs/protocols/stack_overflow.md` | Stack triage + watermark checks |
| `docs/API/SERIAL_API.md` | Frozen serial/BLE API contract — do not change without approval |
| `docs/API/HTTP_API.md` | REST API contract |
| `docs/guides/testing.md` | 3-tier testing strategy |

---

*Last updated: 2026-07-03. Derived from post-mortems LL-001, LL-002,
homing-blocks-main (2026-07-03), BLE-coexistence (2026-07-01),
SSE-Guru-Meditation (2026-07-02), DRAM-triangle (2026-07-01).*
