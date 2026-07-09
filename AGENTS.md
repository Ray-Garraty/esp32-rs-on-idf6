# AGENTS.md — AI Agent Rules (ESP32-S3 + C++23 + ESP-IDF v6)

Agent-critical rules for this firmware. Reference details in `docs/refs/project.md`
(hardware, threads, network) and `docs/refs/coding_style.md` (C++23 conventions,
RAII, error handling). Violations of CRITICAL rules invalidate all changes.

## 1. CRITICAL RULES (Auto-Revert)

### GR-1: NEVER BLOCK THE MAIN LOOP

Main loop (FreeRTOS task `main`) must NEVER execute a blocking operation.
Calls that block > 10 ms MUST live in a dedicated thread.

**Forbidden:** `rmt_tx_wait_all_done()`, `vTaskDelay()` > 10 ms,
`xQueueReceive(portMAX_DELAY)`, `std::mutex::lock()`, synchronous I/O.

**Allowed:** `std::atomic` load/store/fetch, `std::mutex::try_lock()` with
false handling, poll/process functions, `vTaskDelay(pdMS_TO_TICKS(10))`,
non-blocking FD writes.

Failure (2026-07-03): Homing called `rmt_tx_wait_all_done()` in main,
blocking for 11 s. WiFi/BLE init never ran.

### GR-2: MANDATORY RMT STOP FLAGS

Every RMT motion function MUST accept a `std::atomic<bool>*` stop flag
and check it between chunks. If set — abort immediately.

```cpp
[[nodiscard]] std::expected<void, StepperError> move_steps_intervals(
    std::span<const uint32_t> intervals,
    std::atomic<bool>* stop_flag = nullptr
) {
    for (auto chunk : split_into_chunks(intervals, CHUNK_SIZE)) {
        if (stop_flag && stop_flag->load(std::memory_order_acquire)) {
            std::ignore = emergency_stop();
            return std::unexpected(StepperError::LimitSwitchTriggered);
        }
        auto result = rmt_transmit_wait(chunk);
        if (!result) return std::unexpected(result.error());
    }
    return {};
}
```

Failure (2026-07-03): Homing omitted stop flag. Motor ran through FULL limit switch.

### GR-3: DRAM INIT ORDER (The Triangle)

Three-way DRAM conflict. Order MUST be: WiFi → HTTP → BLE.

1. WiFi driver init (~3.5 KB DRAM)
2. `wifi.wait_for_ip(5s)` — obtain IP before binding
3. HTTP server bind to 0.0.0.0:80 (12 KB contiguous MALLOC_CAP_INTERNAL)
4. BLE NimBLE init (12 KB contiguous MALLOC_CAP_INTERNAL)

```cpp
auto result = wifi_.init();
if (!result) return std::unexpected(result.error());
result = wifi_.wait_for_ip(std::chrono::seconds(5));
if (!result) return std::unexpected(result.error());
http_server_ = HttpServer::create();
if (!http_server_) return std::unexpected(http_server_.error());
result = ble_.init();
if (!result) return std::unexpected(result.error());
```

Failures (2026-07-01): Each reordering (HTTP→BLE→WiFi, BLE→WiFi→HTTP,
WiFi→BLE→HTTP) fixed one issue but broke another.

### GR-4: COEXISTENCE POLICY — NEVER PREFER BT

Forbidden: `esp_coex_preference_set(ESP_COEX_PREFER_BT)`.
Use default `ESP_COEX_PREFER_BALANCE`.

Failure (2026-07-01): `ESP_COEX_PREFER_BT` starved WiFi L2 — STA got IP but
100 % TCP packet loss.

### GR-5: NO RAW ESP-IDF POINTERS ACROSS TASK BOUNDARIES

Forbidden: Storing `httpd_req_t*`, `httpd_ws_frame_t*`, or any ESP-IDF opaque
pointer in `std::shared_ptr` / `std::atomic<T*>` / across threads. ESP-IDF
frees the struct when the C handler returns.

Required: WebSocket API (`httpd_ws_send_frame_async`) or copy data into
`std::array<char, N>` before leaving the C callback.

Failure (2026-07-02): SSE stored `std::shared_ptr<std::atomic<httpd_req_t*>>`
in main loop. Crash with EXCVADDR=0x28.

### GR-6: STACK BUDGET IS LAW

Every task MUST specify stack size from the budget table in `docs/refs/project.md`.
No exceptions. No defaults.

Forbidden in threads with stack ≤ 8 KB:
- `std::format()` / `std::print()` in loops
- `nlohmann::json::dump()` without pre-allocated buffer
- Large stack-local arrays (`uint8_t buf[4096]`)
- Deep recursion

Mandatory: After moving code between threads, verify with
`uxTaskGetStackHighWaterMark()` or log watermark before/after.

Failure (2026-07-03): Moving homing to 8 KB motor task caused stack overflow.
Increased to 16 KB (see budget table).

### GR-7: MANDATORY DIAGNOSTIC INSTRUMENTATION

Every new function MUST have:

| What | Instrumentation |
|------|----------------|
| FFI boundary | `FfiGuard guard(boundary_id)` RAII wrapper |
| RMT motion | `assert_rmt_preconditions()` before `move_steps_intervals()` |
| New thread | `StackMonitor::register_thread(name, stack_size)` |
| State transition | `StateTracer::log_burette_transition(old, new)` |
| Large alloc (>4 KB) | `HeapSnapshot::assert_can_allocate(size)` |
| Main loop body | `TickWatchdog watchdog` RAII wrapper |

Code without these instrumentation points is INCOMPLETE.
Rationale: Every past crash was detectible pre-mortem by a diagnostic event.

---

### GR-10: ONLY THE USER ASSESSES PHYSICAL STATE

The AI agent MUST NEVER make claims about physical-world observations
(LED color, motor movement, relay clicks, sensor readings, device
visibility on scanners, or any other observable phenomenon). Only the
user can see, hear, or otherwise perceive hardware behavior.

**Forbidden:**
- "LED turned green" (you didn't see it — the user did)
- "Device appears in scan" (you don't have a BLE scanner)
- "Motor moved successfully" (you can't observe it)
- "The board is powered on" (you don't know)

**Required:**
- Present raw data (serial logs, script output, terminal text)
- Ask the user what they observe via the `question` tool
- Record the user's exact words as evidence
- Use user-reported observations as ground truth

Failure (2026-07-08): Agent claimed "LED turned green ✅" based on serial
log interpretation. The physical LED had not yet changed because the fix
wasn't fully applied.

---

## 2. PRE-FLIGHT CHECKLIST (Copy Before Codegen)

Before generating code touching: threads, network, RMT, FFI, mutexes, queues,
GPIO ISR, NVS, WiFi, BLE, or HTTP — copy and fill:

1. **Thread:** \_\_\_\_\_\_\_\_ (Main/Motor/Temp/BLE/HTTP/net_owner)
2. **Blocking >10 ms?** \_\_\_\_\_\_\_\_ (Yes → move to worker: \_\_\_\_\_\_\_\_)
3. **Stack impact:** format/string/arrays/recursion? \_\_\_\_ Budget: \_\_\_\_ KB
4. **Init order dep:** WiFi IP / HTTP / BLE / none? \_\_\_\_
5. **FFI boundary:** Stores C pointers? \_\_\_\_ Copies before return? \_\_\_\_
6. **Stop flag:** RMT/motion? \_\_\_\_ (if yes: GR-2 REQUIRED)
7. **DRAM:** MALLOC_CAP_INTERNAL? \_\_\_\_ Position in init order? \_\_\_\_

---

## 3. EMBEDDED HARDWARE INVARIANTS

### 3.1 GPIO Pinout

⚠️ **CRITICAL: PSRAM/Flash bus pins (26-37) are STRICTLY FORBIDDEN for gpio_set_direction/gpio_config.**
See `docs/refs/unsafe_gpio_pins.md` for full list, explanations, and safe alternatives.

| GPIO | Function | Constraint |
|------|----------|-----------|
| 1 | U0TXD | Serial — DO NOT TOUCH |
| 3 | U0RXD | Serial — DO NOT TOUCH |
| 2 | Status LED | `gpio_set_level()` — Active HIGH |
| 4 | ADC (pH electrode) | `adc_oneshot_read()` (ADC1_CH3) — 0-2900 mV |
| 14 | Valve | `gpio_set_level()` — LOW=input, HIGH=output |
| 21 | TMC2209 STEP | `rmt_new_tx_channel()` — pulse train |
| 5 | TMC2209 DIR | `gpio_set_level()` — HIGH=CW (moved from GPIO26: LL-027 PSRAM CS1) |
| 27 | TMC2209 EN | `gpio_set_level()` — Active LOW; **gpio_set_direction will hang** (PSRAM data bus) |
| 7 | Endstop FULL | GPIO ISR pos-edge → `std::atomic<bool>` (NOT GPIO34 — PSRAM D5, LL-027) |
| 6 | DS18B20 | OneWire bitbang — 4.7k pull-up (NOT GPIO33 — PSRAM D4, LL-027) |
| 15 | Endstop EMPTY | GPIO ISR pos-edge → `std::atomic<bool>` (NOT GPIO35 — PSRAM D6, LL-027) |

### 3.2 RMT

`rmt_tx_wait_all_done()` in **motor thread only** — never in main loop.
EN pin active LOW: `gpio_set_level(en, 0)` in constructor.

### 3.3 Peripheral Drivers

| Driver | File |
|--------|------|
| TMC2209 | infrastructure/drivers/stepper.cpp |
| Endstops | infrastructure/drivers/limitswitch.cpp |
| DS18B20 | infrastructure/drivers/onewire.cpp |
| NVS | infrastructure/storage/nvs.cpp |

---

## 4. BUILD & CI

### 4.1 Commands

| Command | Purpose | Timeout |
|---------|---------|---------|
| `scripts/build.sh build` | Build firmware (auto‑removes stale `sdkconfig`) | ≥ 120 s |
| `scripts/build.sh flash` | Flash firmware | ≥ 60 s |
| `scripts/build.sh monitor` | Smoke test | 30 s |
| `scripts/build.sh uart` | UART command test | 60 s |
| `scripts/build.sh reconfigure` | Remove `sdkconfig` + `idf.py reconfigure` | ≥ 60 s |
| `scripts/build.sh test` | Host unit tests (Catch2) | 60 s |
| `scripts/build.sh tidy` | clang-tidy | 60 s |
| `scripts/build.sh clean` | Remove build dirs | 15 s |

### 4.2 Build Script Mandatory

**NEVER call `idf.py` directly.** Always use `scripts/build.sh` — it wraps
ESP-IDF environment setup and provides consistent error handling.

### 4.3 sdkconfig Policy

Edit only `sdkconfig.defaults` — never `sdkconfig` (auto-generated).
Never run `idf.py menuconfig`.

**`scripts/build.sh build` always removes stale `sdkconfig` first** — this
forces CMake to regenerate from `sdkconfig.defaults`, exposing config
mismatches that a stale file would silently hide.

After changing `sdkconfig.defaults`, also available:
`scripts/build.sh reconfigure` (remove + `idf.py reconfigure` without full
build).

Key defaults: `CONFIG_ESP_MAIN_TASK_STACK_SIZE=32768`,
`CONFIG_BROWNOUT_DET=n`, `CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=12288`,
`CONFIG_FREERTOS_HZ=1000`,
`CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=6`.
**Constraint:** `WIFI_DYNAMIC_RX_BUFFER_NUM` MUST be ≥ `WIFI_RX_BA_WIN`
(see Known Patterns §5).

### 4.4 Partitions & Dependencies

Partition table in `partitions.csv` — do not change without approval.
Component deps via CMake: `main`, `nlohmann_json`, `catch2`.

---

## 5. CRASH INVESTIGATION

Any ESP32 crash is a RED ALERT. The diagnostic subsystem
(`components/diag/`) intercepts ALL crashes via `__wrap_esp_panic_handler`.

### Crash Output Format

```
=== CRASH ===
exccause=0 name=IllegalInstruction pc=0x40091100 ...
=== REGISTERS ===
a0=0x800910c8 ...
=== BACKTRACE ===
0x400910fd:0x3fffcee0 ...
=== BLACK BOX (64 events, newest first) ===
[822us] t4 FfiExit { boundary: 20, result: 0 }
=== STACK ===
t0 main watermark=0  t1 motor watermark=0 ...
```

### Triage Pipeline

| Scenario | Command |
|----------|---------|
| Serial log exists | `./scripts/analyze_last_crash.sh` |
| Live capture | `timeout 60 python3 scripts/serial_monitor.py` |
| Raw crash text | `python3 scripts/crash_analyzer.py < crash.txt` |

### Known Patterns (from docs/lessons_learned/)

| Signature | Real Cause | Fix |
|-----------|-----------|------|
| A2=0xFFFFFFFC, tlsf_check, heap_caps_*free | Stack overflow, NOT heap (LL-001) | Increase stack, check watermark FIRST |
| ESP_ERR_HTTPD_TASK (45064) | DRAM fragmentation | GR-3 init order; move BLE to net_owner |
| wifi:fail to alloc timer, type=9 | WiFi timer after BLE+HTTP ate DRAM | Reduce WiFi buffer counts |
| StoreProhibited EXCVADDR=0x28 | Dangling httpd_req_t (GR-5) | WebSocket API; no stored C pointers |
| IllegalInstruction + heap 6 KB largest | DRAM fragment → HTTP alloc fail (LL-004) | Keep event loop handle alive |
| `wifi_init.c:52` `#error "WIFI_RX_BA_WIN > WIFI_DYNAMIC_RX_BUFFER_NUM"` | Stale `sdkconfig` hid changed `sdkconfig.defaults` | `scripts/build.sh build` auto‑removes `sdkconfig`; also fix `WIFI_DYNAMIC_RX_BUFFER_NUM` in defaults |

---

## 6. SERIAL & PYTHON SAFETY

### 6.1 Serial Port Safety

- FORBIDDEN to launch background processes holding serial ports
- Any blocking/serial tool MUST have explicit `timeout <seconds>` prefix
- Never leave a process occupying ttyUSB/ttyACM after exit

```
# CORRECT
timeout 30 python3 scripts/serial_monitor.py

# FORBIDDEN
python3 -m serial.tools.miniterm /dev/ttyACM0 115200
```

### 6.2 Python Script Rules

- NEVER use `python -c "..."` inline — bash on Windows mangles quotes
- ALWAYS write Python to a temp file first, then run it
- Use `/tmp/opencode` for temp scripts

---

## 7. .opencode/ DIRECTORY RULES

### GR-8: GLOB TOOL RESTRICTION

The `Glob` tool does **not** work inside `.opencode/`. Use `Read` to list
and inspect files under `.opencode/`.

### GR-9: SUB-AGENT DOCUMENT CREATION RESTRICTION

Sub-agents are **forbidden** from creating `.md` or `.yaml` files inside
`.opencode/`. Exception: `.opencode/tmp/` — temporary files allowed.

**Violation → revert immediately.**

---

## 8. FINAL COMMIT CHECKLIST

- [ ] `scripts/build.sh build` — 0 errors, 0 warnings
- [ ] `scripts/build.sh tidy` — 0 warnings
- [ ] `scripts/build.sh test` — all pass
- [ ] No `std::abort()` / `std::terminate()` / `assert()` in production
- [ ] No `std::string` / `std::vector` in main loop or motor hot paths
- [ ] Every `// NOLINT` has `// CONTRACT:` within preceding 3 lines
- [ ] Main loop has NO blocking operations (GR-1)
- [ ] Every RMT motion has a stop flag (GR-2)
- [ ] Init order follows GR-3 (WiFi → HTTP → BLE)
- [ ] Pre-Flight Checklist filled before codegen
- [ ] No raw ESP-IDF pointers cross thread boundaries (GR-5)
- [ ] Frozen docs/API/SERIAL_API.md contract respected
- [ ] 30 s serial smoke test: no Guru Meditation, no WDT, no panics
- [ ] Code style follows `docs/refs/coding_style.md`
- [ ] Hardware refs match `docs/refs/project.md`
- [ ] `sdkconfig.defaults` constraint: `WIFI_DYNAMIC_RX_BUFFER_NUM` ≥ `WIFI_RX_BA_WIN`

---

## Appendix A: Forbidden Patterns Quick Reference

| Forbidden | Required | Rule |
|-----------|----------|------|
| `rmt_tx_wait_all_done()` in main loop | Move to motor thread | GR-1 |
| `std::mutex::lock()` in main loop | `try_lock()` with false handling | GR-1 |
| `std::format()` in hot paths | `std::array<char, N>` + `std::format_to` | GR-6 |
| `httpd_req_t*` across threads | WebSocket API | GR-5 |
| `esp_coex_preference_set(PREFER_BT)` | Default PREFER_BALANCE | GR-4 |
| Homing in main thread | Motor task (16 KB stack) | GR-1, GR-6 |
| BLE init before HTTP | GR-3: WiFi → HTTP → BLE | GR-3 |
| NimBLE init before service registration | Register all GATT services first | docs/refs/project.md §BLE Implementation |
| `mdns_init()` before IP assigned | Wait for `IP_EVENT_STA_GOT_IP` | docs/refs/project.md |
| `python -c "..."` inline | Write temp file, then run | §6.2 |
| RMT motion without stop flag | Stop flag before start | GR-2 |
| `WRITE_PERI_REG` for brownout | `CONFIG_BROWNOUT_DET=n` | docs/refs/project.md |
| HTTP bind without IP | `wait_for_ip()` first | GR-3 |
| `esp_task_wdt_deinit()` direct | `esp_safe::disable_wdt()` | docs/refs/project.md |
| Naked `rmt_channel_handle_t` | RAII wrapper class | coding_style.md §9.5 |
| Naked `httpd_handle_t` / etc. | RAII wrapper class | coding_style.md §9.5 |
| Functions returning -1 on error | `std::expected<T, Error>` | coding_style.md §2 |
| Stale `sdkconfig` hiding config mismatches | `scripts/build.sh build` auto‑removes it | §4.3 |

---

## Appendix B: Reference Documentation

| Document | Purpose |
|----------|---------|
| docs/refs/project.md | HW pinout, thread/stack budget, init order, network stack, NVS, API |
| docs/refs/coding_style.md | C++23 conventions, error hierarchy, RAII, memory budget, low-level ops |
| docs/lessons_learned/ | Crash patterns & fixes (LL-001–LL-004) |
| docs/protocols/embedded_boot_crash.md | S1–S5 Occam's Razor Protocol |
| docs/protocols/heap_corruption.md | Heap triage (often misdiagnosed stack overflow) |
| docs/protocols/stack_overflow.md | Stack triage + watermark checks |
| docs/refs/unsafe_gpio_pins.md | Unsafe GPIO pins for ESP32-S3 with Octal PSRAM |
| docs/guides/testing.md | 3-tier testing strategy (Catch2) |
