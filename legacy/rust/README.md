# esp32-rs-on-idf6

**ESP32-S3 + Rust + ESP-IDF v6 — experimental firmware reference**

An experimental firmware stack exploring ESP32 + Rust + ESP-IDF v6 with WiFi/BLE coexistence, RMT stepper control, limit switch ISRs, DS18B20 bitbang, ADC, multi-threaded architecture, and a comprehensive diagnostic subsystem.

## What's Inside

- **Working drivers**: RMT stepper (TMC2209), ADC (pH electrode), DS18B20 (1-Wire bitbang), limit switches (GPIO ISR), solenoid valve, status LED
- **Three communication transports**: USB-Serial (JSON), BLE GATT (NimBLE NUS), WiFi (REST API + WebSocket + captive portal)
- **Multi-threaded architecture**: 7 dedicated threads with documented stack budgets
- **32 wire-protocol commands** with typed dispatch
- **Comprehensive diagnostics**: black-box event ring, stack watermark monitor, heap snapshotter, tick watchdog, FFI guard, state tracer
- **Extensive host-testable domain logic**: burette state machine, calibration math, motion planning (>100 unit tests)
- **14 documented crash post-mortems**: see [docs/lessons_learned.yaml](./docs/lessons_learned.yaml) — real bugs, root causes, fixes

## Current Status

**Experimental — NOT production-ready.** This is an extended feasibility study. The code compiles and runs on real hardware, but known issues exist (DRAM fragmentation, init-order sensitivity, BLE null dereference edge cases). All findings are documented in the crash log and lessons-learned database.

## Quick Start

```bash
source scripts/build.sh        # set up toolchain environment
scripts/build.sh               # build for xtensa target
scripts/build.sh test          # run host unit tests
scripts/build.sh clippy        # clippy (xtensa) — 0 warnings
scripts/build.sh flash /dev/ttyUSB0  # flash firmware
timeout 30 python3 scripts/serial_monitor.py
```

**Prerequisites:** `espup install`, Python 3.11+.

## Development with OpenCode

This project includes a set of custom [OpenCode](https://opencode.ai) AI sub-agents in `.opencode/agents/` that understand the project's architecture, rules, and hardware constraints:

| Agent | Purpose |
|---|---|
| **planner** | Analyzes tasks and produces structured plans with acceptance criteria |
| **verifier** | Validates plan feasibility against real code and hardware invariants |
| **implementer** | Implements code from verified plans, runs all checks |
| **validator** | Builds firmware, flashes real ESP32-S3, runs smoke tests |
| **reviewer** | Code review (architecture, style, safety, conventions) |
| **debugger** | Embedded crash analysis using Occam's Razor protocol (S1–S5) |
| **reporter** | Generates completion reports and conventional-commit messages |
| **foreman** | Drives full feature/bugfix workflows using the above agents |

All agents enforce the non-negotiable rules in [AGENTS.md](./AGENTS.md) — golden rules (GR-1 through GR-7), thread budget, init order, and hardware invariants derived from real post-mortems.

## Pin Assignment

| Signal | GPIO | Driver |
|---|---|---|
| TMC2209 STEP | 21 | RMT (TxChannelDriver) |
| TMC2209 DIR | 26 | PinDriver::output |
| TMC2209 EN | 27 | PinDriver::output (active LOW) |
| Valve | 14 | PinDriver::output (LOW=input, HIGH=output) |
| Limit FULL | 32 | PinDriver::input + ISR |
| Limit HOME | 35 | PinDriver::input + ISR |
| pH electrode | 4 | ADC1_CH3 (12-bit, 0–2900 mV) |
| DS18B20 | 33 | OneWire bitbang |
| Status LED | 2 | PinDriver::output |
| USB-Serial RX | **3** | **DO NOT TOUCH** |

The **critical rule**: GPIO3 is U0RXD — never configure it. `Serial` itself owns this pin.

## Communication

| Interface | Role | Protocol |
|---|---|---|
| USB-Serial | Command/response | JSON lines, two-phase (ACK → Result) |
| BLE (NimBLE NUS) | Secondary | Same JSON protocol |
| WiFi (AP/STA) | REST API + WebSocket + embedded WebUI | HTTP/JSON |

## Architecture

Four-layer design with strict dependency direction:

- **`domain/`** — pure logic, no ESP-IDF imports: burette states, calibration math, motion planning
- **`application/`** — orchestration: command dispatch, state machines, scheduling
- **`infrastructure/`** — hardware: stepper (RMT), ADC, OneWire, valve, WiFi, BLE, HTTP server, NVS
- **`interface/`** — external boundaries: USB-Serial, REST handlers, WebUI

Concurrency: `std::thread` + atomics + `mpsc` channels. The main loop never blocks — it polls atomics at 10 ms ticks. Blocking operations (RMT pulse trains, OneWire bitbang, BLE notify) run in dedicated threads.

---

For the full technical specification — protocol reference, state machines, NVS layout, error hierarchy, thread model, build configuration, and dependencies — see [project.md](./docs/refs/project.md).
