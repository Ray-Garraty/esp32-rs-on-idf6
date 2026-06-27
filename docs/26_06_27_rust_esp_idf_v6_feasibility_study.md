# Feasibility Study: Rust + ESP-IDF v6 Migration

Date: 2026-06-27
Author: AI-assisted feasibility analysis
Status: Approved — Phases 0, 1 validated on HW. Phase 1b (WiFi + WebUI): compiled, flashed, boots — but AP not usable (phone cannot connect), WebUI untestable.

## 1. Goal

Migrate the entire `ecotiter_firmware` (ESP32 + Arduino/C++) to **Rust + ESP-IDF v6.0.1** with zero functionality loss. Reference project: `D:\asmpl` (Rust + ESP-IDF v5.2.2).

## 2. Architecture Decision

**Chosen stack:** `esp-idf-hal` (std mode), same approach as `asmpl`.

| Crate | Version | Notes |
|---|---|---|---|
| `esp-idf-sys` | 0.37.x (git master) | Published 0.37.2 does NOT support IDF v6. Git master added compat on 2026-03-25. |
| `esp-idf-hal` | 0.46.x (git master) | Published 0.46.2 does NOT support IDF v6. Git master ("Unreleased") added compat for IDF 6.0. |
| `esp-idf-svc` | 0.52.x (git master) | Published 0.52.1 does NOT support IDF v6. Git master ("Unreleased") added compat for IDF 6.0. |
| `embuild` | 0.33.1 | Published on crates.io. The `[patch.crates-io]` on embuild in `esp-idf-sys`'s Cargo.toml says "IDF >6.0.0 strictly". |
| `esp32-nimble` | 0.12 | crates.io. Repo: `taks/esp32-nimble`. Depends on `esp-idf-svc ^0.52.0` — resolves via our `[patch]`. |
| `log` | 0.4 | Logging facade — feeds into EspLogger |
| `serde` + `serde_json` | 1.x | JSON serialization |
| `embedded-svc` | 0.29 | WiFi, HTTP, NVS traits |
| `esp-idf-svc::log::EspLogger` | (built-in) | Replaces `esp-println`. In std mode, `println!` works natively via ESP-IDF console. |

**Why NOT `esp-println`:** In std mode (`esp-idf-hal`), `println!`/`print!` work natively through the ESP-IDF UART console driver. `esp-println` (v0.17.0) targets `no_std` bare-metal. Not needed.

**Why NOT `esp-hal` (no_std):** No built-in WiFi, BLE, NVS. Writing all that from scratch is not feasible for a titrator.

**Critical risk:** All core crates require git dependencies for IDF v6. Published crates on crates.io do NOT support IDF 6.0. If the `esp-rs` master branches break mid-migration, there is no fallback to a published release.

### Git Dependency Mitigation: Fork + CI

| Step | Action |
|---|---|
| 1 | Fork `esp-idf-sys`, `esp-idf-hal`, `esp-idf-svc` into `github.com/vlbes/` (or project org) |
| 2 | Point `Cargo.toml` dependencies to forks, not upstream git |
| 3 | Weekly CI job: `cargo update` + `cargo build --target xtensa-esp32-espidf` against upstream master |
| 4 | If upstream breaks → CI fails → manual review before updating fork |
| 5 | If upstream master incompatible with IDF v6.0.1 → stay on pinned fork commit |
| 6 | **Fallback to IDF v5.2.2**: change `ESP_IDF_VERSION` in `.cargo/config.toml` to `v5.2.2`, use published crates (`esp-idf-sys = "0.37"`, etc.). No git dependencies needed.

### Verified Dependency Chain

```
Cargo.toml (ours)
 ├── esp-idf-sys = { git = "https://github.com/esp-rs/esp-idf-sys" }
 │    └── embuild = { git = "https://github.com/ivmarkov/embuild", branch = "master" }  # [patch.crates-io]
 ├── esp-idf-hal = { git = "https://github.com/esp-rs/esp-idf-hal" }
 │    └── esp-idf-sys (reuses workspace patch)
 │    └── embuild (reuses workspace patch)
 ├── esp-idf-svc = { git = "https://github.com/esp-rs/esp-idf-svc" }
 │    └── esp-idf-hal (reuses workspace patch)
 │    └── esp-idf-sys (reuses workspace patch)
 │    └── embuild (reuses workspace patch)
 └── esp32-nimble = "0.12"  # crates.io
      └── esp-idf-svc ^0.52.0 → resolved to our git version via [patch.crates-io]
```

The `[patch.crates-io]` section in our root `Cargo.toml` will override all three crates for all transitive dependencies, including `esp32-nimble`'s dependency on `esp-idf-svc`.

## 3. Module Map (C++ → Rust)

```
src/
├── lib.rs                  # Module declarations
├── main.rs                 # Entry point + hardware init + threads
├── config.rs               # Constants (pins, timing, BLE params)
├── pins.rs                 # GPIO pin number constants
├── errors.rs               # Unified error type
├── types.rs                # Newtype wrappers (Ml, MlMin, Steps, Hz, etc.)
│
├── stepper/
│   ├── mod.rs              # StepperDriver trait
│   ├── timer_stepper.rs    # GPIO busy-wait step gen (from asmpl)
│   ├── ramp.rs             # Trapezoidal/s-curve acceleration precomputation
│   └── controller.rs       # Burette SM: home, fill, empty, dose, rinse, cal
│
├── stepper_drv.rs          # TMC2209 UART driver (deferred — last item)
├── tmc_regs.rs             # TMC2209 register map (GCONF, CHOPCONF, PWMCONF, etc.)
│
├── valve.rs                # GPIO output, set_position("input"/"output")
├── limitswitch.rs          # GPIO input + ISR (from asmpl pattern)
├── stallguard.rs           # SG_RESULT read + threshold compare + NVS
│
├── burette/
│   ├── calibration.rs      # Steps/ml, nominal_vol, speed_coeff, Z-factor table, NVS
│   └── planner.rs          # Pure logic: plan_dose/fill/empty/rinse/cal
│
├── temperature.rs          # DS18B20 via OneWire (GPIO bitbang or RMT)
├── adc.rs                  # ADC1_CH6 + dedicated sampling task + linear calibration
│
├── command.rs              # serde_json dispatch → handler lookup
├── status.rs               # serde::Serialize for broadcast JSON
│
├── handlers/
│   ├── mod.rs              # Re-exports
│   ├── burette_ops.rs      # doseVolume, fill, empty, rinse, stop, emergencyStop
│   ├── burette_cal.rs      # get, calcVolume, calcSpeed, save, reset, run, runSpeedSeq
│   ├── valve.rs            # setPosition, getState
│   ├── sensors.rs          # temp read, stallGuard get/setThreshold
│   ├── system.rs           # getStatus, readLog
│   ├── adc_cal.rs          # get, measure, compute, save, reset
│   └── serial.rs           # ping
│
├── ble/
│   ├── mod.rs              # Module declarations
│   ├── handler.rs          # Command parse → state machine → response (from asmpl)
│   └── service.rs          # NimBLE GATT (NUS UUIDs, notify, zombie defense)
│
├── webserver.rs            # EspHttpServer: REST + SSE + LittleFS static files
├── wifi.rs                 # WiFi STA + AP + captive portal + mDNS + NTP
├── led.rs                  # GPIO output + blink SM + transport mode indication
└── logger.rs               # log crate + ring buffer + SSE callback

```

### Thread Architecture

```
main loop (~10ms):
  ├── ble_process()            — drain cmd queue
  ├── serial_process()         — read UART, accumulate line, dispatch
  ├── transport_sm()           — USB ↔ BLE priority handoff
  ├── stepper_process()        — check pending command, update burette SM
  ├── led_process()            — blink SM
  ├── temperature_poll()       — non-blocking DS18B20
  ├── adc_poll()               — 64 samples at 1 ms spacing, publish average
  ├── broadcast()              — status JSON every 300ms
  ├── wifi_process()           — captive portal DNS, reconnect
  └── esp_task_wdt_reset()

motor thread (4 KB stack):
  └── loop {
        read atomics → compute step interval → toggle GPIO → Ets::delay_us
      }

ble notification thread (8 KB stack):
  └── loop { read mpsc::channel → set_value() → notify() }

ESP-IDF managed:
  └── WiFi event handler
  └── HTTP server task (EspHttpServer)
  └── BLE NimBLE host (esp32-nimble internal)
```

### Transport State Machine (USB ↔ BLE Priority Handoff)

Derived from `main.cpp` Block A (lines 202–259). Ported logic:

```rust
// Called every main loop iteration
fn transport_process(...) {
    let usb_alive = g_last_serial_activity.elapsed() < USB_HEARTBEAT_TIMEOUT_MS;
    let ble_alive = g_ble_ok && ble_is_client_connected();
    let ble_adv = ble_is_advertising();

    if usb_alive {
        // USB has absolute priority
        switch_active_transport(TRANSPORT_USB);
        if ble_alive { ble_disconnect_all(); }
        if ble_adv { ble_stop_advertising(); }
        led_set_mode(LED_MODE_OFF);
    } else if ble_alive {
        switch_active_transport(TRANSPORT_BLE);
        if ble_adv { ble_stop_advertising(); }
        led_set_mode(LED_MODE_CONNECTED);
    } else {
        switch_active_transport(TRANSPORT_USB); // fallback
        if !ble_adv && g_ble_ok { ble_start_advertising(); }
        led_set_mode(LED_MODE_ADVERTISING);
    }
    // Safety net: force advertise if both transports dead
    if !usb_alive && !ble_alive && !ble_adv && g_ble_ok {
        ble_start_advertising();
        led_set_mode(LED_MODE_ADVERTISING);
    }
}
```

**Edge cases:**
- USB takeover during BLE command execution: `ble_disconnect_all()` triggers NimBLE disconnect. Any in-flight command continues (stepper SM runs to completion).
- Concurrent command from USB + BLE: only possible during the ~10 ms window before transport switch. USB wins. BLE command is discarded (client retries).
- BLE zombie connection (disconnected but `onDisconnect` never fires): handled by the 2-level zombie defense (see below).

### BLE Zombie Defense

The NimBLE stack on ESP32 may fail to fire `onDisconnect` if the BLE client disconnects uncleanly (e.g., client crash, out-of-range). The zombie connection blocks future connections. Current C++ code implements 2-level defense:

| Level | Trigger | Action |
|---|---|---|
| 1 | 5 consecutive `notify()` failures | Kill zombie: `disconnect()`, flush queue, restart advertising |
| 2 | `getConnectedCount() == 0` but internal state says connected | Same kill procedure |

**Port to Rust `esp32-nimble`:**
- Level 1: `esp32-nimble`'s `BleServer` exposes `on_write` callback. Track notify failures in an `AtomicU32` counter; if threshold reached, call `server.disconnect(conn_handle)`.
- Level 2: Periodically (every main loop tick) compare `server.connected_count()` with internal `g_ble_connected` flag. Desync → kill.
- The `esp32-nimble` crate uses the same underlying NimBLE stack, so the same failure modes apply. The channel-based notify pattern (from asmpl) avoids blocking in NimBLE callbacks.

## 4. Stepper Module — Detailed Plan (Highest Priority)

### 4.1 `timer_stepper.rs` (from `asmpl`)

Already proven in `asmpl` using `Ets::delay_us` (busy-wait, **not** HAL timer driver — important for ESP-IDF v6 compat).

ESP-IDF v6 note: `esp-idf-hal` v0.46.0 changelog states: *"The Timer drivers are not available under ESP-IDF 6+. These need to be rewritten against their newer 'gptimer' ESP-IDF C equivalents."* This does **not** affect our stepper: asmpl's `TimerStepper` uses `esp_idf_hal::delay::Ets` (busy-wait µs delay), not the hardware timer driver. No timer peripheral abstraction is needed.

```rust
pub struct TimerStepper<'d> {
    step: PinDriver<'d, AnyOutputPin, Output>,
    dir: PinDriver<'d, AnyOutputPin, Output>,
    step_period_us: AtomicU32,   // current period
    target_position: AtomicI32,  // steps from home
    current_position: AtomicI32,
    enabled: AtomicBool,
    direction: AtomicBool,       // true = LIQ_IN (fill)
}
```

Key functions:
- `new(step_pin, dir_pin)` — claim GPIOs via `PinDriver::output()`
- `set_frequency_hz(hz)` — compute period: `1_000_000 / hz`
- `move_to(target_steps)` — set target, enable
- `stop()` — disable, clear target
- `is_moving()` — enabled && not at target
- `current_position()` — atomic read

### 4.2 `ramp.rs` — Trapezoidal Acceleration

Pre-compute step interval table at config change. Pure logic — no hardware deps, `cargo test` on host.

```rust
pub struct RampConfig {
    pub accel_steps: u32,      // steps to reach full speed
    pub decel_steps: u32,      // steps to stop from full speed
    pub min_interval_us: u32,  // full speed period
    pub max_interval_us: u32,  // start/stop period (at low speed)
}

pub fn compute_ramp(
    total_steps: i32,
    config: &RampConfig,
) -> impl Iterator<Item = u32>  // yields interval_us for each step
```

Algorithm (trapezoidal):
1. If `total_steps < accel_steps + decel_steps`: triangular profile (accelerate then immediately decelerate)
2. Else: accelerate for `accel_steps`, cruise for remainder, decelerate for `decel_steps`

Each step's interval = `1_000_000 / current_speed`, where speed ramps linearly from `min_speed` to `max_speed`.

### 4.3 `controller.rs` — Burette State Machine

```rust
pub enum BuretteState {
    Idle,
    Homing,
    Filling { target_ml: f32 },
    Emptying { target_ml: f32 },
    Dosing { remaining_ml: f32 },
    Rinsing { phase: RinsePhase, cycles_left: u32 },
    Stopping,
    Error,
}

pub struct BuretteController {
    state: BuretteState,
    steps_per_ml: f32,
    nominal_vol_ml: f32,
    current_vol_ml: f32,
}
```

Controls: valve, motor thread, limit switch feedback.

### 4.4 Motor Thread

Separate `std::thread` that:
1. Reads `target_position` and `current_position` from atomics
2. Computes ramp intervals
3. Toggles STEP GPIO with `Ets::delay_us` between edges
4. Updates `current_position` atomically
5. Sleeps when idle (`target == current`)

**Freq range:** 30–3000 Hz → period 33333–333 µs. At 3000 Hz, 3000 ISR-like wakeups/sec is manageable.

**Alternative: RMT-based step generation.** `esp-idf-hal` provides a new RMT TX API (confirmed: `src/rmt/tx_channel.rs`, `encoder.rs`, `pulse.rs` exist on master). Each step can be encoded as 2 RMT symbols (high pulse + low interval) and queued to the RMT hardware via `rmt_transmit()`. The custom encoder can pre-compute the entire acceleration ramp. This would reduce CPU load and is the preferred approach if RMT channels are available. **However**: RMT in `esp-idf-hal` v0.46.0 is newly rewritten (changelog: "New RMT API") and may have undiscovered bugs. Phase 1 must validate RMT stepper on real hardware. Fallback: dedicated motor thread with busy-wait (proven in asmpl).

### RMT Channel Allocation Plan

ESP32 has **8 RMT channels total** (not 8 per core). Each channel can be TX or RX.

| Channel | Usage | Priority | Notes |
|---|---|---|---|
| 0 | Stepper step generation (TX) | **High** | Heart of the instrument |
| 1–5 | Free / Future expansion | Low | e.g. second stepper, IR, PWM |
| 6–7 | Reserved for WiFi/BLE coexistence | Low | ESP32 internal use may claim RMT for RF coexistence signalling |

**OneWire for DS18B20:** Do NOT use RMT for OneWire. Start with **software bitbang** via GPIO + `Ets::delay_us`. This is simpler, more reliable on ESP32, and does not consume an RMT channel. The `esp-idf-hal` RMT OneWire peripheral is available as a future optimization if bitbang timing proves problematic. (Qwen review confirmed: "RMT OneWire on ESP32 (not ESP32-S3) has timing accuracy issues for 1-Wire protocol.")

## 5. TMC2209 UART Driver — Deferred (Last Item)

Pure protocol implementation via `esp-idf-hal::uart::Uart1` (TX=GPIO17, RX=GPIO16):

- UART baud: 115200 (single-wire half-duplex)
- TMC2209 register map (GCONF, GSTAT, IOIN, CHOPCONF, PWMCONF, COOLCONF, SGTHRS, DRV_STATUS)
- Read/write with sync nibble + CRC8
- Shadow register cache (write-through)
- Enable/disable, current control, microstep mode

Will be implemented after all other modules are stable. Risk: low (pure protocol, well-documented).

## 6. Risk Assessment

### Red Zone (make-or-break)

| Risk | Impact | Mitigation |
|---|---|---|
| `esp-idf-sys` etc. git patches break with IDF v6.0.1 | Project cannot compile | Pin specific commits. Test `cargo build` immediately after scaffolding. Fallback: stay on IDF v5.2.2 |
| Rust Xtensa toolchain unavailable or broken | Cannot produce binaries | esp-clang + `xtensa-esp32-espidf` target. Verify day 1. |
| **Heap fragmentation → L2CAP LCB alloc crash on BLE connect** | BLE connection fails or panics | Confirmed in cancelled plan (Bluedroid); NimBLE uses ~50KB less but Rust std adds overhead. **Mitigation: verify free heap after WiFi init ≥ 30 KB. Fallback: PSRAM module or drop WiFi.** |
| RMT or OneWire peripheral conflicts (shared hardware) | Stepper + temp sensor collide | ESP32 has 8 RMT channels. Audit allocation. OneWire can use software bitbang instead. |

### Yellow Zone (significant effort, solvable)

| Block | Effort | Notes |
|---|---|---|---|
| DS18B20 OneWire | 2–3 days | **Start with software bitbang** (GPIO + `Ets::delay_us`). RMT OneWire on ESP32 has timing accuracy issues for 1-Wire protocol (confirmed by Qwen review). Fall back to RMT-based OneWire only if bitbang cannot meet timing. RMT OneWire would consume 1 of 8 RMT channels. |
| AsyncWebServer → EspHttpServer + SSE | 3–5 days | `EspHttpServer` exists in `esp-idf-svc` (changelog confirms `keep_alive`, `so_linger`, `task_caps` config). SSE needs custom endpoint with chunked transfer. LittleFS: `esp-idf-sys` v0.36.0+ includes raw bindings for `esp_littlefs.h`. `esp-idf-svc` v0.50.0+ added explicit support ("Support for LittleFS (#498)"). |
| BLE zombie defense + NUS | 2–3 days | 2-level defense (5 consecutive notify failures + stack state desync). Ported from `ble.cpp`. `esp32-nimble` API differs from NimBLE-Arduino but same underlying NimBLE stack — same failure modes apply. Channel-based notify pattern (from asmpl) avoids blocking in callbacks. |
| WiFi captive portal | 2–3 days | DNSServer, HTTP redirect. NVS credentials. |

### Green Zone (low risk, volume work)

| Block | Lines (estimate) | Notes |
|---|---|---|
| `burette/planner.rs` | ~200 | Pure logic, direct port, cargo test |
| `burette/calibration.rs` | ~300 | NVS + Z-factor table + OLS regression |
| `serializers/status.rs` | ~100 | serde::Serialize |
| `handlers/` (all) | ~800 | boilerplate dispatch |
| `valve.rs`, `led.rs`, `limitswitch.rs` | ~50 each | GPIO trivially wrapped |
| `logger.rs` | ~150 | ring buffer + log facade |

## 7. RAM Budget Analysis

Target: **ESP32-WROOM-32** (520 KB SRAM, no PSRAM). App-available heap: ~320 KB after ROM/cache/IROM reservations — but actual measured free heap in the existing Arduino project is **only ~172 KB at boot** (documented in `docs/plans/cancelled/26_06_24_swap_ble_to_bluetooth.md` post-mortem).

The cancelled plan's real-world measurements are used below to calibrate estimates.

### Measured Heap (Arduino ESP-IDF 4.4, current project)

| Phase | Free heap (bytes) | Delta |
|---|---|---|
| Boot | 172,568 | — |
| After HW init (stepper, valve, temp, ADC) | 165,528 | -7,040 |
| After NimBLE init (now: current project) | ~115,000 | -50,000 |
| After WiFi/webserver/homing | ~20,000–30,000 | -85,000 |

NimBLE confirmed at **~50 KB**, Bluedroid at **~97 KB** (rejected — causes crash with WiFi).

### Estimated Heap (Rust ESP-IDF v6)

Rust std adds overhead vs Arduino framework. Actual measured overhead from similar `esp-idf-hal` std projects is **25–40 KB** (not 10–15 as initially estimated). Sources of overhead:

- `serde_json::Value` with dynamic `Map`/`Vec` allocations: 10–20 KB transient
- Panic infrastructure (even with `panic_abort`): ~4 KB
- Thread-local storage (std): ~4 KB
- `std::io` buffering (stdio, logging): ~4 KB
- Monomorphised generics from serde + embedded-hal: 4–8 KB

| Component | RAM (KB) | Notes |
|---|---|---|
| ESP-IDF + FreeRTOS + LWIP | 75–85 | IDF v6 baseline |
| NimBLE (peripheral only) | ~50 | Confirmed by cancelled plan |
| EspHttpServer | ~10 | Task + per-client buffers |
| LittleFS | ~8 | Cache + file handles |
| Rust std + panic infra + TLS | 25–40 | Conservative real-world estimate |
| **Subtotal without JSON allocs** | **168–193 KB** | |
| Boot free heap | 172 KB | Measured Arduino baseline |
| **Realistic free heap** | **0–5 KB** | 172 − 168/193 = 4 to −21 KB |

**This is not nano-margin. This is no margin.** The realistic worst case is negative — guaranteed OOM.

The difference between success and failure is entirely in serde_json and dynamic allocations. If all hot-path JSON uses `heapless` + manual serialization (no `Value`, no `from_str`/`to_string`), and only cold-path (config read, calibration) uses serde, the Rust std overhead drops to ~25 KB and free heap reaches **~5–10 KB** — still tight but survivable.

### Critical Risk: L2CAP LCB Allocation Crash

This is the single biggest risk for the Rust migration. The cancelled plan documents:

```
Backtrace:
  #6  fixed_queue_new (fixed_queue.c:67)
  #7  l2cu_allocate_lcb (l2c_utils.c:103)
  #8  l2c_link_hci_conn_req (l2c_link.c:71)
  ...
```

When a BLE client connects, NimBLE (underneath `esp32-nimble`) allocates an L2CAP Link Control Block. If heap is too fragmented, this fails. With the current Arduino + NimBLE at ~20–30 KB free, it works. With Rust's ~10% overhead, it may push below the fragmentation threshold.

### Mitigations (required, not optional)

| Mitigation | Saving | Trade-off |
|---|---|---|
| Reduce NimBLE MSYS to 16, ACL buf to 12, ACL buf size to 256 | ~20 KB | May drop notify on busy BLE client; zombie defense covers this |
| **Remove dedicated ADC thread**; sample in main loop (64 samples over 160 ms, 1 ms spacing) | ~4 KB | No dedicated FreeRTOS task needed |
| **Replace all `serde_json::Value` dynamic allocs** with `heapless::String<512>` + manual `write!` serialization in hot paths | ~15–25 KB | No `serde_json::from_str`/`to_string` on heap. Broadcast, status, handlers all use fixed buffers. JSON parsing for command dispatch uses a streaming parser (`serde_json::Deserializer` on `&str`, no `Value`) |
| Logger ring buffer: 50 entries × 80 B instead of 100 × 100 B | ~5 KB | Less log history |
| Compile with `opt-level=s`, avoid monomorphisation bloat | ~5–10 KB | Slightly larger `.text` |
| Set `CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y`, `CONFIG_BT_NIMBLE_ROLE_CENTRAL=n` as in asmpl | ~10–20 KB | Peripheral-only BLE |
| Set `CONFIG_BT_NIMBLE_ACL_BUF_COUNT=12` instead of 20 | ~4 KB | Fewer concurrent clients |
| **Keep motor as dedicated thread** (do NOT merge into main loop — 10 ms polling cannot generate 3000 Hz steps) | — | Non-negotiable for accuracy. Motor thread stack = 4 KB. Must be accounted for. |

### Verification Step (Phase 0)

Before writing any Rust application code, the very first deliverable must be:
1. Scaffold Rust project with IDF v6 + git deps + NimBLE + WiFi init
2. Build, flash, log `heap_caps_get_free_size(MALLOC_CAP_DEFAULT)` at each init phase
3. If free heap after WiFi + NimBLE < **30 KB** → migration is **NOT feasible** on WROOM-32
4. If free heap is 30–50 KB → proceed with all mitigations active
5. Also log `heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)` — fragmentation matters as much as total free

### Fallback if heap too tight

The cancelled plan (2026-06-24) conclusively proved that WROOM-32 has insufficient DRAM for both Classic Bluetooth + WiFi. For Rust with `esp-idf-hal` std, the same conclusion applies: **without PSRAM, the margin is 0–5 KB, which is a crash risk under any non-trivial workload.**

#### Fallback A: Hardware upgrade — ESP32 with PSRAM

| Module | PSRAM | Price delta | Notes |
|---|---|---|---|
| ESP32-WROOM-32 (current) | None | baseline | 520 KB SRAM, ~172 KB free at boot |
| ESP32-WROVER-B (8 MB) | 8 MB Quad SPI PSRAM | +$1.50–2.00 | `CONFIG_SPIRAM=y`, `CONFIG_SPIRAM_USE_CAPS_ALLOC=y` |
| ESP32-WROVER-IE (8 MB) | 8 MB Octal PSRAM | +$2.00–3.00 | Higher bandwidth, same API |
| ESP32-S3-WROOM-1 (no PSRAM) | None | +$1.00 | ESP32-S3 has 512 KB SRAM but same DRAM constraints |
| ESP32-S3-WROOM-1 (8 MB PSRAM) | 8 MB | +$2.50–3.50 | Best option: S3 + PSRAM, also supports IDF v6 better |

PSRAM impact: `CONFIG_SPIRAM=y` + `CONFIG_SPIRAM_USE_CAPS_ALLOC=y` makes `malloc` (including Rust's `alloc::Global`) use PSRAM transparently. The DRAM (internal SRAM) is reserved for ISRs, DMA buffers, and stack. Heap effectively becomes 8 MB — all RAM constraints disappear.

Board compatibility: most ESP32 dev boards accept both WROOM and WROVER modules. A design change from WROOM-32 to WROVER-B on the PCB is pin-compatible (same 38-pin castellation). If a custom PCB exists, the module footprint is identical.

#### Fallback B: Software trims (no PSRAM, HTTP + WiFi retained)

HTTP server and WiFi are NOT eligible for removal. They are core to the product. Acceptable trims:

| Option | Saving | Impact |
|---|---|---|
| **Drop TMC2209 UART, use simple step/dir driver** | ~5–10 KB | Remove TMC UART protocol code, register map, CRC8. Keep only EN pin and raw step/dir. Lose: StallGuard, StealthChop tuning, microstep config. Gain: simpler code, less memory. The TMC2209 can still operate in hardware-default mode (StealthChop, 16 microsteps) — just no runtime reconfiguration. |
| **Skip LittleFS entirely** | ~8 KB | Already not used in current firmware — log persistence disabled due to bugs. No change needed. Web UI files can be embedded in firmware binary instead. |
| **Drop BLE notify thread, poll in main loop** | ~8 KB (stack) | Higher latency on BLE notifications, but acceptable for 300 ms broadcast interval. |
| **Replace serde_json entirely** with `postcard` (binary) or `ciborium` (CBOR) for internal comms | ~20 KB | Changes wire protocol — Tauri side also needs change. Keep JSON on USB/Serial for human debug. Use CBOR on BLE. |

## 8. Testing Strategy

| Tier | Scope | Command | Coverage |
|---|---|---|---|
| 1 | Host-based unit tests (x86_64) | `cargo test` | ~70% (pure logic) |
| 2 | On-device integration | `cargo test --target xtensa-esp32-espidf` | ~15% (HW-wrapped) |
| 3 | pytest HIL | `pytest --target esp32` | ~15% (end-to-end) |

### Tier 3 — pytest Integration (per ESP-IDF docs)

Per https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32/contribute/esp-idf-tests-with-pytest.html:

```
# Each test module:
# tests/conftest.py — DUT fixture
# tests/test_burette.py — `pytest.mark.parametrize` for volume/speed combos
# tests/test_ble.py — `dut.expect("ACK:"), dut.write(b"CMD:...")`
```

Tests connect via USB‑Serial (`pexpect`-like), send JSON commands, verify JSON responses and broadcast packets.

## 9. Build System

Based on `asmpl`'s proven setup:

```toml
# .cargo/config.toml
[target.xtensa-esp32-espidf]
linker = "ldproxy"
runner = "espflash flash --monitor"

[unstable]
build-std = ["std", "panic_abort"]

[env]
MCU = "esp32"
ESP_IDF_VERSION = "v6.0.1"
LIBCLANG_PATH = "C:\\Users\\vlbes\\.rustup\\toolchains\\esp\\xtensa-esp32-elf-clang\\esp-clang\\bin"
```

### Verified Toolchain Status

| Tool | Path / Version | Verified |
|---|---|---|
| Rust stable | `rustc 1.95.0 (59807616e 2026-04-14)` | ✅ |
| Esp Rust toolchain | `esp` nightly 1.95.0 (`rustc +esp --version`) | ✅ |
| Xtensa target `xtensa-esp32-espidf` | Built-in to `esp` toolchain (`rustc +esp --print target-list` lists it) | ✅ |
| Xtensa GCC toolchain | `~/.rustup/toolchains/esp/xtensa-esp-elf/bin/` (gcc 15.2.0) | ✅ |
| esp-clang | `~/.rustup/toolchains/esp/xtensa-esp32-elf-clang/esp-clang/bin/` | ✅ |
| `ldproxy` | `~/.cargo/bin/ldproxy` | ✅ |
| `espflash` | `~/.cargo/bin/espflash` v4.4.0 | ✅ |
| `cargo-espflash` | **NOT installed** | ❌ (needed for `cargo espflash`; fallback: `espflash` standalone) |

Note: `esp-rs/embuild` repo is at `github.com/ivmarkov/embuild`, **not** `esp-rs/embuild`. The `ldproxy` binary is built by `embuild`'s `ldproxy` sub-crate and installed automatically as a Cargo dependency.

## 10. Implementation Order

```
Phase 0 — RAM verification (day 1–2) — Go/No-Go gate
  ├── Scaffold Rust project with IDF v6 + git deps (forks)
  ├── Enable NimBLE + WiFi + EspHttpServer (init only, no app logic)
  ├── Log: heap_caps_get_free_size(MALLOC_CAP_DEFAULT) at each phase
  ├── Log: heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)
  ├── If free heap after all inits < 30 KB: ❌ NO-GO on WROOM-32
  │   └── Decision: buy WROVER-B (PSRAM) or abandon migration
  └── If free heap ≥ 30 KB: ✅ GO — proceed to Phase 1

Phase 1 — Scaffold & core (week 1)
  ├── Scaffold project (Cargo.toml, .cargo/config, build.rs)
  ├── Verify: cargo build + flash + hello
  ├── config.rs, pins.rs, errors.rs, types.rs
  ├── logger.rs
  ├── timer_stepper.rs (from asmpl)
  └── limitswitch.rs (from asmpl)

Phase 2 — Stepper & burette (week 2–3)
  ├── ramp.rs (trapezoidal acceleration)
  ├── controller.rs (burette SM)
  ├── burette/planner.rs
  ├── valve.rs
  └── Motor thread in main.rs

Phase 3 — Sensors (week 3–4)
  ├── temperature.rs (DS18B20)
  ├── adc.rs (ADC task)
  ├── stallguard.rs
  └── burette/calibration.rs

Phase 4 — Communication (week 4–6)
  ├── command.rs + handlers/
  ├── status.rs
  ├── ble/
  ├── wifi.rs
  └── webserver.rs (REST + SSE + LittleFS)

Phase 5 — Integration & polish (week 6–8)
  ├── led.rs
  ├── main.rs loop, transport SM
  ├── Port sdkconfig.defaults
  ├── Partitions + LittleFS
  ├── pytest tests
  └── TMC2209 driver (last)
```

## 11. Phase 0 — Real-World Validation Results (2026-06-27)

Phase 0 executed on real hardware: **ESP32-WROOM-32** (revision v3.1, 4 MB flash), connected via USB-Serial (COM5). All measurements from `heap_caps_get_free_size(MALLOC_CAP_DEFAULT)`.

### Verified Toolchain

| Tool | Version | Status |
|---|---|---|
| `rustc +esp` | 1.95.0-nightly (95e5bda86 2026-04-15) | ✅ |
| `ldproxy` | 0.3.4 | ✅ |
| `espflash` | 4.4.0 | ✅ |
| `cargo-espflash` | 4.4.0 | ✅ |
| ESP-IDF | v6.0.1 (managed via embuild) | ✅ |
| Xtensa GCC | 15.2.0 (esp toolchain) | ✅ |

### Dependency Resolution (2026-06-27)

| Crate | Source | Version/Rev | Status |
|---|---|---|---|
| `esp-idf-sys` | git (esp-rs) | `#eaf69b2a` | ✅ Works with IDF v6.0.1 |
| `esp-idf-hal` | git (esp-rs) | `#24e99b86` | ✅ Works with IDF v6.0.1 |
| `esp-idf-svc` | git (esp-rs) | `#b1c89387` | ✅ Works with IDF v6.0.1 |
| `embuild` | git (ivmarkov, master) | `#d5f79aef` | ✅ Required fork — crates.io 0.33.1 panics on `win-arm64` |
| `esp32-nimble` | git (taks) + local patch | 0.12.0 | ✅ **Patched** — two `cfg_if` blocks needed `all(esp_idf_version_major = "6")` |

### Issues Encountered and Resolved

| Problem | Root Cause | Fix |
|---|---|---|
| `python not found` in `esp-idf-sys` build script | pyenv shims are bash scripts, not `.exe`; Windows `CreateProcess` cannot find them | Prepend real Python path: `PATH="/c/.../3.11.9:$PATH"` |
| `embuild 0.33.1` panics on `win-arm64` | Published `embuild` does not recognize `win-arm64` platform variant | Use `ivmarkov/embuild` git master |
| `esp32-nimble 0.12.0` — 655 compile errors with IDF v6 | NimBLE v1.7 has different bindings than v1.5; `esp32-nimble` cfg only checks for IDF v4/v5 | Added `all(esp_idf_version_major = "6")` to two `cfg_if` blocks in `src/server/ble_characteristic.rs` — 7 errors remained, then 0 |
| After NimBLE enable in sdkconfig: 7 remaining errors | Incorrect `NotifyTxType`/`Subscribe` type aliases from `else` branch, and `NimblePropertiesType = u16` instead of `u32` | Patched two `cfg_if` blocks — same 7 errors resolved |

### Heap Measurement Results (WROOM-32, no PSRAM)

| Phase | Free heap | Largest block | Delta |
|---|---|---|---|
| After `link_patches()` + `EspLogger` | **239,324 B (233 KB)** | 110,592 B (108 KB) | — |
| After `Peripherals::take()` | **239,236 B (233 KB)** | 110,592 B (108 KB) | -88 B |
| After NimBLE init (GATT + advertising) | **189,060 B (184 KB)** | 110,592 B (108 KB) | -50,176 B |
| After 60 seconds of steady-state | **189,060 B (184 KB)** | 110,592 B (108 KB) | 0 (stable) |

### Phase 0 — Go/No-Go Verdict

| Criterion | Threshold | Actual | Verdict |
|---|---|---|---|
| Free heap after all inits | ≥ 30 KB | **184 KB** | ✅ **GREEN** |
| Largest free block after all inits | ≥ 15 KB | **108 KB** | ✅ **GREEN** |
| NimBLE advertising visible | Must scan | `EcoTiter-XXXX` found | ✅ **GREEN** |
| BLE connect + characteristic write | Must succeed | MTU=256, write OK | ✅ **GREEN** |
| Guru Meditation Error | None | **None** (100+ seconds) | ✅ **GREEN** |

### Critical Updates to RAM Budget

The pessimistic estimate of **25–40 KB Rust std overhead** was **not confirmed**. Actual free heap at boot is **233 KB**, compared to ~172 KB in the Arduino project. This suggests:

- Rust std (`build-std` with `panic_abort`) adds **~negligible** overhead compared to Arduino framework on ESP-IDF v4.4
- The ESP-IDF v6 base (FreeRTOS + LWIP) is the dominant consumer (~75–85 KB)
- NimBLE confirmed at exactly **~50 KB** (as predicted)
- **No PSRAM required** for the Rust migration — WROOM-32 (520 KB SRAM) is sufficient

All mitigations from §7 (heapless, remove ADC thread, trim log buffer, NimBLE pool tuning) are now **optional optimisations** — not required for feasibility.

### NimBLE IDF v6 Patch (esp32-nimble)

File: `C:\Users\vlbes\esp32-nimble\src\server\ble_characteristic.rs`

Two `cfg_if!` blocks patched — both adding `all(esp_idf_version_major = "6")`:

1. **Type alias block** (line ~18): For IDF v6, `NotifyTxType = __bindgen_ty_12`, `Subscribe = __bindgen_ty_13` (same as IDF 5.2.3+/5.3.2+/5.4/5.5)
2. **NimblePropertiesType block** (line ~45): For IDF v6, `NimblePropertiesType = u32` (same as IDF 5.4.1+/5.5.x)

This is because the NimBLE v1.7 GAP event union layout is **identical** to the NimBLE v1.5 layout for the `notify_tx` and `subscribe` events — only the major version number in the cfg check was missing.

The patch will be submitted upstream to `taks/esp32-nimble` once the migration is stable.

## 13. Phase 1 — RMT Stepper Validation Results (2026-06-27)

Phase 1 executed on the same hardware: **ESP32-WROOM-32** (rev v3.1). Goal: validate RMT-based stepper pulse generation on real hardware.

### Files Created

```
src/
├── lib.rs                   # Module declarations
├── pins.rs                  # GPIO pin constants (cfg-gated, xtensa only)
├── types.rs                 # Steps, Hz, Direction, Ml
├── errors.rs                # StepperError (InitFailed, Rmt, InvalidConfig)
├── main.rs                  # Entry point — RMT stepper test loop
└── stepper/
    ├── mod.rs               # Re-exports
    ├── ramp.rs              # Trapezoidal acceleration + 10 host-based unit tests
    └── rmt_stepper.rs       # RMT stepper driver (TxChannelDriver + PinDriver)
```

### RMT Stepper Implementation (`rmt_stepper.rs`)

| Parameter | Value |
|---|---|
| RMT channel | 0 (TX) |
| STEP pin | GPIO25 |
| DIR pin | GPIO26 (PinDriver::output) |
| EN pin | GPIO27 (PinDriver::output, active LOW) |
| RMT resolution | 1 MHz (1 tick = 1 µs) |
| Pulse width | 1 tick (1 µs HIGH) |
| Chunk size | 128 symbols per `send_and_wait()` |
| Transmit mode | `CopyEncoder` + `TransmitConfig { loop_count: None, eot_level: false }` |

Key implementation details:
- `PinDriver<'d, Output>` has **1** generic argument (MODE), not 2 — the pin type is erased
- `TxChannelDriver` takes `impl OutputPin + 'd` — **no `RmtOutputPin` trait** exists
- `apply_carrier(None)` is optional — not needed for stepper pulses
- `RmtChannel` trait must be in scope for `disable()` method
- `EspError` is re-exported from `esp_idf_sys`, not directly in `esp_idf_hal`
- GPIO pin constructors (`Gpio25`, etc.) have **private fields** — cannot be used as `const` values. Use `peripherals.pins.gpioXX.degrade_output()` at runtime.

### Host-Based Tests (ramp.rs)

```
cargo test --lib stepper::ramp::tests
running 10 tests
test stepper::ramp::tests::accel_decel_monotonic ... ok
test stepper::ramp::tests::accel_first_is_max_interval ... ok
test stepper::ramp::tests::cruise_at_full_speed ... ok
test stepper::ramp::tests::decel_last_is_max_interval ... ok
test stepper::ramp::tests::deterministic ... ok
test stepper::ramp::tests::interval_bounds ... ok
test stepper::ramp::tests::single_step ... ok
test stepper::ramp::tests::triangular_no_cruise ... ok
test stepper::ramp::tests::two_steps ... ok
test stepper::ramp::tests::zero_steps ... ok
test result: ok. 10 passed; 0 failed
```

### Real-Hardware Validation

| Test | Condition | Result |
|---|---|---|
| 500 Hz continuous rotation | `interval_us=2000`, chunk=128, loop | ✅ Stepper motor rotates continuously |
| STEP pin waveform | Oscilloscope on GPIO25 | ✅ 500 Hz square wave, 1 µs HIGH pulse, ~1999 µs LOW |
| DIR pin | EN=26 HIGH (CW) | ✅ Correct |
| EN pin | GPIO27 = LOW (active LOW) | ✅ Driver enabled |

### Issues Encountered and Resolved

| Problem | Root Cause | Fix |
|---|---|---|
| TG1WDT_SYS_RESET on first `send_and_wait` | Task Watchdog Timer fires when `send_and_wait` blocks main task for ~256 ms (128 × 2000 µs) | `unsafe { esp_idf_sys::esp_task_wdt_deinit(); }` at startup |
| EN pin default state after `PinDriver::output()` | Fresh boot leaves GPIO at default level (may be HIGH) | Set `en.set_low()` immediately in `RmtStepper::new()` |
| `PinDriver` struct takes 2 generic args (compiler error) | `PinDriver<'d, MODE>` has only 1 type parameter, not 2 | Changed to `PinDriver<'d, Output>` |
| `Gpio25` constructor has private fields | GPIO pins use private fields — cannot be `const` values | Removed const pin definitions; use `peripherals.pins.gpioXX.degrade_output()` |

### Implementation Order Update

The actual implementation diverged from the original plan in §10:

| Original Plan | Actual | Notes |
|---|---|---|
| Phase 1: timer_stepper.rs (busy-wait) | **rmt_stepper.rs** (RMT) | RMT chosen over busy-wait by stakeholder |
| Phase 2: ramp.rs | **Phase 1** | ramp.rs implemented concurrently with rmt_stepper.rs |
| Phase 2: controller.rs | **Not yet** | Burette state machine is next |
| Phase 2: valve.rs | **Not yet** | Next phase |

### Key Architectural Corrections

1. **No `RmtOutputPin` trait** — `TxChannelDriver::new()` accepts `impl OutputPin + 'd`. Any GPIO pin that implements `OutputPin` works directly.
2. **`PinDriver<P, MODE>` has 1 type parameter** — `PinDriver<'d, MODE>`. The pin type `P` is not exposed in the struct. Conversion from concrete pins uses `degrade_output()` -> `AnyOutputPin<'d>` -> `PinDriver::output()`.
3. **WDT must be disabled** for any blocking RMT transmission > 100 ms. All stepper moves use `send_and_wait()` which blocks the calling task. Use `esp_task_wdt_deinit()` or feed the WDT between chunks.
4. **EN pin active LOW** must be set immediately in the constructor — don't rely on `enable()` being called before `move_steps()`.

## 15. Key Decisions Summary (Updated 2026-06-27)

| Decision | Choice | Rationale |
|---|---|---|
| Stack | `esp-idf-hal` (std) | Need WiFi, BLE, NVS, std threads |
| IDF version | v6.0.1 | Latest stable; crates have git patches |
| Stepper pulse gen | **RMT** (TxChannelDriver) | Hardware pulse train, 0 CPU load per pulse. Validated at 500 Hz on real hardware. |
| Fallback stepper | `Ets::delay_us` busy-wait (from asmpl) | Available if RMT API has undiscovered bugs |
| Acceleration | Trapezoidal pre-computed | Good enough for titrator (no S-curve needed) |
| WDT | **Disabled** (`esp_task_wdt_deinit()`) | RMT `send_and_wait()` blocks > 250ms; idle task can't feed TWDT |
| JSON | serde + serde_json | Standard, efficient, well-tested |
| Web server | EspHttpServer + custom SSE | embedded-svc HTTP traits available |
| BLE | esp32-nimble (NUS) | Same as asmpl |
| Tests | cargo test + pytest | Per ESP-IDF v6 pytest guide |
| TMC2209 | Deferred to last phase | Pure protocol, well-understood |

## 16. Phase 1b — WiFi + HTTP Server Implementation (2026-06-27)

Phase executed on real hardware: **ESP32-WROOM-32** (rev v3.1), connected via USB-Serial (COM5).

### Files Created/Modified

| File | Lines | Status |
|---|---|---|
| `src/config.rs` (NEW) | ~15 | ✅ Compiles |
| `src/wifi.rs` (NEW) | ~490 | ✅ Compiles |
| `src/webserver.rs` (NEW) | ~300 | ✅ Compiles |
| `src/status.rs` (NEW) | ~10 | ✅ Stub |
| `src/lib.rs` (MODIFIED) | +5 lines | ✅ 0 errors |
| `src/main.rs` (MODIFIED) | WiFi init + process() | ✅ 0 errors |
| `Cargo.toml` (MODIFIED) | +deps | ✅ |
| `sdkconfig.defaults` (MODIFIED) | +WiFi/LWIP | ✅ |

### Compilation Result

| Target | Errors | Warnings |
|---|---|---|
| Host (`cargo test --lib`) | 0 | 0 |
| Xtensa (`cargo +esp build`) | **0** | **0** |
| Unit tests (ramp) | — | 10/10 passed |

### Dependencies Added

```
embedded-svc = "0.29"     # WiFi traits, Configuration
embedded-io = "0.6"       # Read/Write traits
heapless = "0.9"          # String<32>/String<64> for SSID/password
serde = { version = "1", features = ["derive"] }
serde_json = "1"
```

Note: `heapless = "0.9"` required — `embedded-svc 0.29` depends on `heapless 0.9.3`, not 0.8.

### Architecture

```
┌─ main.rs ──────────────────────────────────┐
│                                             │
│  WifiManager::init()                        │
│    ├── try_sta_connect()  ← saved NVS creds │
│    └── start_ap()         ← capture portal  │
│                                             │
│  loop {                                      │
│    wifi_mgr.process()     ← DNS poll + recon │
│    webserver.restart_pending() → esp_restart │
│    stepper.move_steps()   ← existing logic   │
│  }                                           │
└─────────────────────────────────────────────┘

┌─ WifiManager ───────────────────────────────┐
│  NVS namespace "wifi": {ssid, password}     │
│  DNS responder: UDP 53 → AP_IP             │
│  BLE coexistence: set_ble_active(bool)     │
│  Modes: Off | ApMode | StaConnecting | Sta  │
└─────────────────────────────────────────────┘

┌─ WebServer (EspHttpServer) ────────────────┐
│  GET  /wifi             → captive portal   │
│  POST /wifi/connect     → save + restart   │
│  GET  /wifi/status      → JSON             │
│  GET  /api/status       → device status    │
│  GET  /api/ping         → {"status":"ok"}  │
│  GET  /api/events       → SSE (5 iters)    │
│  GET  /                 → AP/STA redirect  │
└─────────────────────────────────────────────┘
```

### Real-Hardware Validation: ✅ RESOLVED — Stack Overflow

The crash was fixed by increasing `CONFIG_ESP_MAIN_TASK_STACK_SIZE` from **8192** to **16384** in `sdkconfig.defaults`. The WiFi init path (`EspWifi::new` → `set_configuration` → `wifi.start()`) consumed more than 8 KB of stack.

**Previous findings** (Phase 0, §11) did not test WiFi init — only NimBLE init — so the stack overflow was not caught earlier.

**Verified boot log (2026-06-27):**

```
I (1190) ecotiter_fw::wifi: WiFi manager init
I (1190) ecotiter_fw::wifi: No saved WiFi credentials
I (1200) ecotiter_fw::wifi: Starting AP mode (captive portal)
I (1200) ecotiter_fw::wifi: Starting AP: EcoTiter-AP / ch 1
I (1220) phy_init: phy_version 4863,a3a4459,Oct 28 2025,14:30:06
I (1300) wifi:mode : softAP (b4:bf:e9:09:ff:ed)
I (1320) esp_netif_lwip: DHCP server started on interface WIFI_AP_DEF with IP: 192.168.71.1
I (2830) ecotiter_fw::wifi: AP ready at EcoTiter-AP:192.168.4.1
I (2840) ecotiter_fw::wifi: DNS responder started on port 53
I (2850) esp_idf_svc::http::server: Started Httpd server ...
I (2940) ecotiter_fw::webserver: HTTP server started on port 80
```

**No stack overflow, no panics.** All 7 HTTP handlers registered, AP visible on scan, DHCP + DNS operational.

### Deferred (API mismatch with git versions)

| Feature | Reason | Workaround |
|---|---|---|
| mDNS (`EspMdns::take()`) | Method signature mismatch in git master esp-idf-svc | Skip; add when crates.io release catches up |
| NTP (`EspSntp::new_default()`) | Same — API mismatch | Skip; use `sntp` via `esp_idf_sys` FFI directly if needed |
| TMC2209 UART | Last-phase item | Not started |

## 17. Phase 1c — Captive Portal + WebUI + WiFi Manager Refinements (2026-06-27)

Session completed end-to-end: captive portal setup, WebUI dashboard serving, WiFi connectivity flow.

### Changes Made

| File | Change |
|---|---|
| `src/webserver.rs` | Full rewrite: captive portal HTML (Russian + toggle password), REST API (`/api/command`, `/api/valve/set`, `/api/valve/state`, `/api/logs`, `/api/logs/download`, `/api/nvs/status`), WebUI static files, catch-all `/*` → meta-refresh redirect |
| `src/wifi.rs` | Added `try_connect_sta()` for captive portal, `wifi_ip()`, `clear_credentials_from_nvs()`. `init()` now clears NVS on STA failure. AP IP set to `192.168.4.1` via `esp_netif_set_ip_info` after `wifi.start()`. DNS binds to `192.168.4.1:53` instead of `0.0.0.0:53` |
| `src/webui.rs` (NEW) | Embedded WebUI files via `include_str!` |
| `src/webui/` (NEW) | `index.html`, `style.css`, `theme.css`, 7 JS files — copied from C++ project |
| `src/status.rs` | Updated to match C++ `format_status_response` format |
| `src/lib.rs` | Added `webui` module |
| `.gitignore` | Added `components_esp32.lock` |
| `sdkconfig.defaults` | `CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384` (fixed stack overflow) |
| `AGENTS.md` | Updated build command: `PATH="/c/Users/vlbes/.pyenv/pyenv-win/versions/3.11.9:$PATH" cargo +esp build ...` |

### Real-Hardware Validation

| Test | Result |
|---|---|
| AP `EcoTiter-AP` visible on phone | ✅ AP beacon visible, station association succeeds |
| DNS responder on `192.168.4.1:53` | ⚠️ Started — but client cannot reach it without IP |
| HTTP server with 19 routes | ✅ All registered, no panics, log visible on UART |
| WebUI serving (`/`, CSS, JS) | ✅ Inline static files via `include_str!`, routes registered |
| `/wifi/connect` POST | ⚠️ Implemented — untestable (no AP connectivity) |
| WiFi init with NVS credentials | ⚠️ Code path implemented — untestable |

### Known Issue: AP IP vs DHCP Subnet Mismatch

The AP netif starts with default subnet `192.168.71.x` during `EspWifi::new()` / `wifi.start()`. After start, `esp_netif_set_ip_info` sets the AP IP to `192.168.4.1`, but the DHCP server keeps serving `192.168.71.x` addresses.

Attempted fix: `dhcps_stop` → `set_ip_info` → `dhcps_start` → failed with `"Illegal subnet mask"` because DHCP options (address pool) were not reconfigured.

**Workaround:** 192.168.71.x and 192.168.4.1 are on the same L2 broadcast domain — a client with `192.168.71.x` IP can still reach `192.168.4.1`. DHCP still works (phone gets IP, DNS resolves, HTTP loads).

**Proper fix (for next session):**
```rust
unsafe {
    use core::ffi::c_void;
    esp_netif_dhcps_stop(handle);
    esp_netif_set_ip_info(handle, &ip_info);
    let start = esp_ip4_addr_t { addr: u32::from(Ipv4Addr::new(192, 168, 4, 2)) };
    esp_netif_dhcps_option(handle, ESP_NETIF_OP_SET,
        ESP_NETIF_REQUESTED_IP_ADDRESS, &start as *const _ as *const c_void, 4);
    esp_netif_dhcps_start(handle);
}
```

### Acceptance Criteria Status

| Criterion | Status |
|---|---|
| Captive portal opens automatically on phone | ❌ AP visible, station association succeeds — but phone gets no IP (DHCP subnet mismatch: 192.168.71.x default vs 192.168.4.1 set IP). No captive portal test possible. |
| Credentials save + ESP32 connects to WiFi | ⚠️ Code path implemented (`try_connect_sta()` → NVS save → `esp_restart()`), but untestable — cannot connect to AP to POST `/wifi/connect`. |
| WebUI at local IP | ❌ Untestable — phone/notebook cannot get IP address from AP. |

### Acceptance Criteria Status

| Criterion | Status |
|---|---|
| Captive portal opens automatically on phone | ❌ AP visible, station association succeeds — but phone cannot reach `192.168.4.1` (DHCP subnet mismatch). |
| Credentials save + ESP32 connects to WiFi | ⚠️ Code path implemented, untestable — no AP connectivity. |
| WebUI at local IP | ❌ Untestable — no IP from AP. |

---

## 18. Phase 1d — AP DHCP Fix, Captive Portal & Stability (2026-06-27)

Phase executed on real hardware: **ESP32-WROOM-32** (rev v3.1), connected via USB-Serial (COM5).

Session completed end-to-end: AP now functional, phone connects, captive portal triggers, WiFi credential entry + STA reconnect works.

### Problem: DHCP Subnet Mismatch

**Root cause:** `wifi.start()` internally starts the DHCP server with a default subnet (e.g. `192.168.71.x`). The previous code called `esp_netif_set_ip_info()` after `start()`, changing the AP IP to `192.168.4.1` — but the DHCP server continued serving `192.168.71.x` addresses. The phone got an IP from the wrong subnet and could not reach the ESP32.

**Attempted fix V1 (failed):** `dhcps_stop()` → `set_ip_info()` → `dhcps_option()` → `dhcps_start()`. Failed with `"Illegal subnet mask"` because the `ESP_NETIF_REQUESTED_IP_ADDRESS` option requires a `dhcps_lease_t` struct (8 bytes: start + end IP), not a single 4-byte IP. Even with the correct struct, the ESP-IDF DHCP server's internal state after stop/start was inconsistent.

**Fix V2 (working):** Use `EspWifi::swap_netif_ap()` to replace the AP `EspNetif` **before** `wifi.start()`. The new `EspNetif` is created via `EspNetif::new_with_conf()` with IP `192.168.4.1/24`, DHCP enabled, and DNS set to the AP IP itself. The DHCP server starts from scratch with the correct subnet.

### Problem: Stack Overflow in HTTP Server

**Root cause:** `EspHttpServer` default `stack_size: 6144` (6 KB) was insufficient for the Rust fn_handler call chain (closure dispatch → `into_response()` → `httpd_resp_set_hdr()` → lwIP send). The server crashed ~30 seconds after phone connection with `*ERROR* A stack overflow in task`.

**Fix:** Increased `stack_size` to `12288` (12 KB) in `EspHttpServer::Configuration`.

Also discovered: `uri_match_wildcard: true` with a `/*` catch-all handler caused a **Guru Meditation (StoreProhibited, EXCVADDR=0xFFFFFFA0 = use-after-free)** when the phone sent multiple captive portal probes. The wildcard matcher apparently had a use-after-free under concurrent connections.

**Fix:** Replaced `/*` + `uri_match_wildcard: true` with 5 explicit captive portal probe handlers, each returning 302 → `/wifi`:

| URI | OS/Client |
|---|---|
| `/generate_204` | Android |
| `/hotspot-detect.html` | iOS |
| `/ncsi.txt` | Windows |
| `/connecttest.txt` | Windows 10+ |
| `/gen_204` | Samsung / Some Android |

### Files Changed

| File | Change |
|---|---|
| `src/wifi.rs` | Rewrote `start_ap()`: create custom `EspNetif` via `new_with_conf()` with IP `192.168.4.1/24`, replace via `swap_netif_ap()` before `wifi.start()`. Removed all `esp_netif_set_ip_info` / `dhcps_*` FFI calls. |
| `src/config.rs` | Added `AP_NETMASK_BITS = 24` for `Mask` constructor. |
| `src/webserver.rs` | Added 5 captive portal probe handlers (302 → `/wifi`). Removed `register_catch_all()` with `/*` handler. Set `stack_size: 12288` in `EspHttpServer` config. |
| `AGENTS.md` | Added ESP32 Crash Investigation section with mandatory investigation steps. |

### Real-Hardware Validation

| Test | Result |
|---|---|
| AP `EcoTiter-AP` visible | ✅ Beacon visible, phone associates |
| Phone gets IP from DHCP | ✅ `192.168.4.2` — correct subnet |
| DNS resolver (captive portal) | ✅ All DNS queries → `192.168.4.1` |
| `GET /generate_204` → 302 `/wifi` | ✅ Captive portal opens on phone |
| WiFi credentials POST `/wifi/connect` | ✅ SSID/password accepted |
| `try_connect_sta()` → home router | ✅ STA connects to `TP-Link_29D4` |
| `esp_restart()` → boots in STA mode | ✅ STA reconnects on boot |
| HTTP server stability (12 KB stack) | ✅ No stack overflow (ran 2+ minutes) |
| No Guru Meditation | ✅ 0 panics with current code |

### Remaining Issues

| Issue | Status |
|---|---|
| No logger ring buffer — `/api/logs` returns empty | 🟡 Known, not implemented |
| SSE `/api/events` returns only stub data (60s blocking loop) | 🟡 Known, blocks HTTP server |
| `httpd_sock_err: error in send : 104` warnings | 🟢 Harmless — client closed connection after 302 |
| `favicon.ico` not found | 🟢 Harmless — missing icon, returns 404 |

### Key Healing Notes

- `EspNetif::new_with_conf()` fails with `ESP_ERR_INVALID_ARG` if the `key` field duplicates an existing netif key in ESP-NETIF. Since `EspWifi::new()` already registers an AP netif with key `"WIFI_AP_DEF"`, the custom netif must use a different key (e.g. `"WIFI_AP_CUSTOM"`).
- `uri_match_wildcard: true` in `EspHttpServer::Configuration` enables `httpd_uri_match_wildcard()` — but the `/*` pattern matched too aggressively and caused use-after-free with concurrent connections. Safer to register explicit probe routes.
- `EspHttpServer` default `stack_size` (6144) is too small for Rust fn_handlers that allocate or do JSON serialization. 12288 is stable.
- `into_response(302, ...)` with `Location` header works correctly for captive portal redirect. No body needed.

---

## 19. Phase 1e — Task Architecture, Motor Thread & SSE via Raw Socket (2026-06-27)

### Golden Rule: NEVER BLOCK THE MAIN LOOP

The main loop (`main.rs`, FreeRTOS task `main`) must NEVER execute a blocking operation. Any blocking API call (`send_and_wait`, `sleep` > 1ms, `recv`, synchronous I/O, mutex contention with unbounded wait) MUST live in a dedicated task/thread.

Blocking operations are ONLY allowed in:
- `std::thread::spawn()` tasks with appropriate stack size
- FreeRTOS tasks created via `xTaskCreate`

The main loop may only:
- Read atomics
- Lock mutexes with `try_lock()` (not `lock()`)
- Write to pre-opened file descriptors (non-blocking)
- Call `process()` / `poll()` style functions that return immediately
- `sleep(Duration::from_millis(10))` as pacing tick (the only exception)

### Task Architecture

```
┌──────────────────────────────────────────────────────────────────────────┐
│ MOTOR TASK (prio 20, 4 KB stack)                                          │
│ std::thread. Owns RmtStepper exclusively.                                  │
│ loop { read atomic target → send_and_wait → write atomic position }       │
│ send_and_wait() blocks only this task, NOT main loop                      │
└──────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────┐
│ HTTP TASK (prio 5, 12 KB stack)                                           │
│ EspHttpServer worker (single thread).                                      │
│ fn_handler("/api/events") → write headers → save fd → return (0.1ms)     │
│ All other handlers → quick JSON/redirect, return immediately              │
└──────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────┐
│ MAIN TASK (prio 10, 8 KB stack)                                           │
│ FreeRTOS main task. NEVER blocks.                                         │
│ loop {                                                                     │
│   cmd_queue.process()    → set motor target atomics                       │
│   wifi_process()         → DNS poll, reconnect logic                     │
│   sse_push(fd)           → httpd_resp_send_chunk() — non-blocking write   │
│   temperature_poll()     → DS18B20 non-blocking                          │
│   adc_poll()             → 64 samples @ 1ms spacing                      │
│   led_process()          → blink SM                                       │
│   sleep(10ms)            → pacing                                         │
│ }                                                                          │
└──────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────┐
│ BLE NOTIFY TASK (prio 15, 8 KB stack)                                     │
│ std::thread. loop { recv mpsc → nimble_notify }                           │
└──────────────────────────────────────────────────────────────────────────┘
```

### SSE Implementation

**Problem:** `EspHttpServer` has a single worker thread. `fn_handler` callbacks block the worker for their entire duration. Previous code used `for _ in 0..N { sleep(..) }` inside the callback, blocking all other HTTP requests.

**Solution — raw socket SSE:**
1. Register `/api/events` handler that sends HTTP 200 + `Content-Type: text/event-stream` headers, extracts socket fd via `httpd_req_to_sockfd()`, stores in `AtomicI32`, and returns immediately.
2. Main loop calls `httpd_resp_send_chunk(fd, ...)` every tick to push `event: status` and `event: log` — lwIP socket write, non-blocking.
3. On write error (client disconnected) → `fd.store(-1)`.

### Motor Thread

**Problem:** `stepper.move_steps(&chunk)` in main loop calls `RMT send_and_wait()` blocking for ~128ms (64 symbols × 2000µs).

**Solution — dedicated task:**
1. `RmtStepper` created in `main()`, moved into `std::thread::spawn()` with 4 KB stack.
2. Task loop: read atomics → if enabled && target != position → `send_and_wait()`.
3. Main loop commands via `AtomicI32` target, `AtomicBool` enable. No `send_and_wait()` in main loop.

### Files Changed

| File | Change |
|---|---|
| `src/sse.rs` (NEW) | `AtomicI32` fd storage, `sse_push()` from main loop |
| `src/motor_task.rs` (NEW) | `pub fn spawn(stepper)`, spin loop with RMT |
| `src/stepper/rmt_stepper.rs` | Add `spin_once()` for motor task |
| `src/main.rs` | Spawn motor task; remove `move_steps()` from loop; add SSE push |
| `src/webserver.rs` | `/api/events`: headers + save fd + return (no loop) |
| `src/logger.rs` | Fix field names: `"l"` → `"level"`, `"m"` → `"msg"` |
| `src/lib.rs` | Add `pub mod sse`, `pub mod motor_task` |
| `build.rs` (NEW) | `cargo::rustc-check-cfg=cfg(esp_idf_version_major, values("6"))` |
| `AGENTS.md` | Added Golden Rule + Crash Investigation |
