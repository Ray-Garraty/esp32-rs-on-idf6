# ESP32-S3 Firmware Reference (C++23 + ESP-IDF v6)

**ESP32-S3 + C++23 (`-std=c++23`) + ESP-IDF v6 (dev branch) — dual-core FreeRTOS, 8 MB Octal PSRAM**

An embedded firmware stack demonstrating production-oriented patterns for ESP32-S3: RMT stepper control, analog/digital sensors, BLE/WiFi coexistence, HTTP REST + WebSocket + embedded WebUI, and a comprehensive diagnostic subsystem. 25+ documented crash post-mortems with root cause analysis.

---

## What's Inside

- **Peripherals**: RMT stepper driver (TMC2209), ADC (pH electrode), DS18B20 (1-Wire bitbang), GPIO ISR (limit switches), solenoid valve, RGB LED (WS2812)
- **Communication**: USB-Serial (JSON protocol), BLE GATT (NimBLE NUS), WiFi AP/STA (REST API + WebSocket + embedded WebUI)
- **Architecture**: 8 FreeRTOS tasks with documented stack budgets, dual-core mandatory (`CONFIG_FREERTOS_UNICORE=n`), 4-layer dependency (domain → application → infrastructure → interface), task sovereignty via queues + atomics, non-blocking main loop
- **Diagnostics**: BlackBox event ring (64 events, lock-free), StackMonitor (per-task watermark), HeapSnapshot (DRAM pre-check), TickWatchdog (main loop latency), StateTracer, FfiGuard, RWDT (6s hardware reset), core dump pipeline
- **Memory strategy**: Explicit allocation via `CONFIG_SPIRAM_USE_CAPS_ALLOC` — PSRAM for bulk data (JSON, HTTP buffers, logs, ramps), DRAM for ISR/hot-path (atomics, stacks, RMT DMA)
- **Documentation**: [Architectural invariants](docs/refs/CONSTITUTION.md) (8 articles), [GPIO pinout spec](docs/refs/gpio_pins_spec.md) (PSRAM bus safety), [Memory spec](docs/refs/memory_spec.md) (PSRAM/DRAM strategy), [Crash triage](docs/protocols/crash_triage.md) (known patterns), [Diagnostic subsystem](docs/refs/diagnostic_spec.md)

---

## War Stories

### The Frozen Tick — UNICORE Spinlock Deadlock (LL-045)

After fixing one crash, a new one emerged: `rst:0x9 (RTCWDT_SYS_RST)` ~5.6 seconds after WiFi AP starts. No Guru Meditation, no backtrace, no panic dump — just silence, then reboot. The tick counter froze mid-air; the CPU was trapped inside a level-1 interrupt dispatcher (`_xt_lowint1`) and never returned to user code. The IWDT (500 ms) never fired because interrupts were being serviced. Only the RWDT (6 s) finally killed it.

**3 debugger sessions, 10+ build-smoke cycles, ~3 hours** wasted on false hypotheses:
- PSRAM bus conflict (GPIO moved — no change)
- RF noise on floating input (pull-down added — no change)
- RMT ISR cache stall (homing disabled — no change)
- PSRAM code fetch inside WiFi ISR (FETCH_INSTRUCTIONS disabled — no change)

The root cause was `CONFIG_FREERTOS_UNICORE=y`. On single-core, the WiFi driver holds a spinlock while the WiFi MAC ISR tries to acquire the same spinlock — deterministic deadlock. This was discoverable in a **3-second grep** of the ESP-IDF Kconfig help, which states verbatim: *"It is not recommended to use single-core mode when Wi-Fi or Bluetooth are enabled."*

**Result:** LL-045 → `CONFIG_FREERTOS_UNICORE=n` mandated in CONSTITUTION.md Art. III, debugger workflow overhaul (hypothesis falsification, structured output, 3-experiment limit → GR-13).

All 25+ crash post-mortems with root cause analysis and applied fixes are in [`docs/lessons_learned/`](docs/lessons_learned/).

---

## Quick Start

```bash
scripts/idf.sh build           # C++23, ESP-IDF v6 (clean build, auto-removes stale sdkconfig)
scripts/idf.sh flash           # flash firmware (auto-detect port)
scripts/idf.sh monitor         # serial monitor (live log)
scripts/idf.sh smoke           # automated smoke test (build + flash + 30s monitor)
scripts/idf.sh test            # host unit tests (Catch2)
```

**Prerequisites:** ESP-IDF v6 toolchain, Python 3.11+.

---

## Pin Assignment

| GPIO | Function | Driver | Constraint |
|------|----------|--------|-----------|
| 1 | U0TXD | Serial | **DO NOT TOUCH** |
| 3 | U0RXD | Serial | **DO NOT TOUCH** |
| 4 | pH electrode (ADC) | `adc_oneshot_read()` (ADC1_CH3) | 0–2900 mV range |
| 5 | TMC2209 DIR | `gpio_set_direction()` + `gpio_set_level()` | HIGH=CW; moved from GPIO26 (PSRAM CS1) |
| 6 | DS18B20 | OneWire bitbang | 4.7k pull-up; moved from GPIO33 (PSRAM D4) |
| 7 | Limit FULL | GPIO ISR pos-edge | Syringe bottom; moved from GPIO34 (PSRAM D5) |
| 13 | TMC2209 EN | `gpio_set_direction()` + `gpio_set_level()` | Active LOW; safe GPIO; moved from GPIO27 (PSRAM HD) |
| 14 | Valve | `gpio_set_direction()` + `gpio_set_level()` | LOW=input, HIGH=output |
| 15 | Limit HOME | GPIO ISR pos-edge | Syringe top; moved from GPIO35 (PSRAM D6) |
| 16 | TMC2209 UART RX | `uart_set_pin(UART_NUM_2)` | PDN_UART half-duplex |
| 17 | TMC2209 UART TX | `uart_set_pin(UART_NUM_2)` | PDN_UART half-duplex |
| 21 | TMC2209 STEP | RMT TX (channel 0) | Pulse train |
| 48 | Status LED | RMT TX (WS2812) | RGB via RMT — NOT `gpio_set_level` |

**Critical:** GPIOs 26–37 are on the PSRAM/Flash bus. Calling `gpio_set_direction()` or `gpio_config()` on these pins causes an immediate system hang. Only `gpio_set_level()` is safe. Full reference: [`docs/refs/gpio_pins_spec.md`](docs/refs/gpio_pins_spec.md).

---

## Architecture

```
domain/ → application/ → infrastructure/ → interface/
```

- **`domain/`** — pure logic, no ESP-IDF imports: state machines, calibration math, motion planning
- **`application/`** — orchestration: command dispatch, state machines, scheduling
- **`infrastructure/`** — hardware: stepper (RMT), ADC, OneWire, valve, WiFi, BLE, HTTP, NVS, diagnostics
- **`interface/`** — external boundaries: USB-Serial, REST handlers, WebUI, BLE GATT

### Concurrency Model

- Dual-core FreeRTOS (`CONFIG_FREERTOS_UNICORE=n`)
- Main loop: non-blocking 10 ms tick — polls atomics, never blocks
- Blocking operations (RMT pulse trains, 1-Wire bitbang, BLE notify) in dedicated threads
- Cross-task communication: FreeRTOS queues + `std::atomic` — no shared functions, no semaphores
- Init order (inside `net_owner`): WiFi → HTTP → BLE (DRAM Triangle, CONSTITUTION Art. IV)

---

## Development with OpenCode

This project includes custom [OpenCode](https://opencode.ai) AI sub-agents in `.opencode/agents/` that understand the project's architecture, rules, and hardware constraints:

| Agent | Purpose |
|-------|---------|
| **planner** | Analyzes tasks, produces structured plans with acceptance criteria |
| **verifier** | Validates plan feasibility against real code and hardware invariants |
| **implementer** | Implements code from verified plans, runs all checks |
| **validator** | Builds firmware, flashes real ESP32-S3, runs smoke tests |
| **reviewer** | Code review (architecture, style, safety, conventions) |
| **debugger** | Embedded crash analysis using Occam's Razor protocol (S1–S5) |
| **reporter** | Generates completion reports and conventional-commit messages |

All agents enforce the non-negotiable rules in [`CONSTITUTION.md`](docs/refs/CONSTITUTION.md) and [`AGENTS.md`](AGENTS.md) — derived from real hardware post-mortems.

---

## References

| Document | Purpose |
|----------|---------|
| [`CONSTITUTION.md`](docs/refs/CONSTITUTION.md) | 8 architectural invariants (non-blocking main loop, task sovereignty, dual-core, init order, safety, memory) |
| [`AGENTS.md`](AGENTS.md) | Operational rules for AI agents (verification, escalation, git policy) |
| [`gpio_pins_spec.md`](docs/refs/gpio_pins_spec.md) | GPIO pinout, PSRAM bus safety, strapping pins |
| [`memory_spec.md`](docs/refs/memory_spec.md) | PSRAM/DRAM strategy, PMR allocator patterns, decision matrix |
| [`project.md`](docs/refs/project.md) | Full specification — hardware, threads, network, protocol, NVS, state machines |
| [`coding_style.md`](docs/refs/coding_style.md) | C++23 conventions, error hierarchy, RAII, concurrency rules |
| [`diagnostic_spec.md`](docs/refs/diagnostic_spec.md) | Crash capture, BlackBox, stack monitoring, core dump pipeline |
| [`crash_triage.md`](docs/protocols/crash_triage.md) | Crash output format, known signatures, exception causes, triage commands |
| [`lessons_learned/`](docs/lessons_learned/) | 25+ documented crash post-mortems with root cause and fix |
