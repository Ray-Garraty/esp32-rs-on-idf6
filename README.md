# EcoTiter Firmware

Firmware for a laboratory automatic titrator. Runs on ESP32, written in Rust (ESP-IDF v6, `esp-idf-hal` std mode). Controls a stepper-driven glass burette, solenoid valve, pH/ORP electrode, and temperature sensor. Primary host: Tauri desktop app.

## The Instrument

### Burette Module

A glass cylinder with a plunger driven by a linear stepper motor (TMC2209). The plunger moves between two **limit switches**:

- **FULL** (bottom) — burette is full of titrant
- **EMPTY** (top) — burette is empty

Position is tracked in steps from the FULL switch. The volume currently inside the cylinder is the **leftover volume** (ml). A homing sequence on power-up finds both endstops to establish position.

### Valve

A 2-way solenoid valve directs flow:

| Position | Flow |
|---|---|
| `input` | From **titrant bottle** into the burette (filling) |
| `output` | From **burette** into the titration vessel (dispensing) |

### Sensors

| Sensor | Measures | Used for |
|---|---|---|
| pH/ORP electrode | Electrical potential (mV) via ADC | Tracking the analytical signal during titration |
| DS18B20 | Temperature (°C) | Temperature compensation of pH readings |

### How Titration Works

1. **Fill** the burette from the titrant bottle (valve → `input`, plunger moves down to FULL)
2. For each dose step:
   - **Dose** a precise volume into the vessel (valve → `output`, plunger moves up)
   - **Wait** for the pH/mV reading to stabilise (drift < threshold, or timeout)
   - **Record** the point (volume, pH/mV, derivative)
3. Compute dpH/dV to detect the **equivalence point**
4. Deliver post-equivalence volume if configured, then stop

Results: a titration curve (CSV) with detected equivalence points.

## Quick Start

```bash
PATH="/c/Users/vlbes/.pyenv/pyenv-win/versions/3.11.9:$PATH" \
  cargo +esp build --target xtensa-esp32-espidf

espflash flash --port COM5 "target/xtensa-esp32-espidf/debug/ecotiter"

timeout 30 python scripts/serial_monitor.py COM5

cargo test --lib stepper::ramp::tests

cargo +esp clippy -- -D warnings
```

**Prerequisites:** `espup install`, `cargo install espflash ldproxy`, Python 3.11+.

## Pin Assignment

| Signal | GPIO | Driver |
|---|---|---|
| TMC2209 STEP | 25 | RMT (TxChannelDriver) |
| TMC2209 DIR | 26 | PinDriver::output |
| TMC2209 EN | 27 | PinDriver::output (active LOW) |
| Valve | 14 | PinDriver::output (LOW=input, HIGH=output) |
| Limit FULL | 32 | PinDriver::input + ISR |
| Limit EMPTY | 35 | PinDriver::input + ISR |
| pH electrode | 34 | ADC1_CH6 (12-bit) |
| DS18B20 | 33 | OneWire bitbang |
| Status LED | 2 | PinDriver::output |
| USB-Serial RX | **3** | **DO NOT TOUCH** |

The **critical rule**: GPIO3 is U0RXD — never configure it. `Serial` itself owns this pin.

## Communication

| Interface | Role | Protocol |
|---|---|---|
| USB-Serial | Primary command/response | JSON lines, two-phase (ACK → Result) |
| BLE (NimBLE NUS) | Secondary | Same JSON protocol |
| WiFi (AP/STA) | REST API + SSE + embedded WebUI | HTTP/JSON |

USB has absolute priority — when USB is active, BLE is disconnected.

## Architecture

Four-layer design with strict dependency direction:

- **`domain/`** — pure logic, no ESP-IDF imports: burette states, calibration math, motion planning
- **`application/`** — orchestration: command dispatch, state machines, scheduling
- **`infrastructure/`** — hardware: stepper (RMT), ADC, OneWire, valve, WiFi, BLE, HTTP server, NVS
- **`interface/`** — external boundaries: USB-Serial, REST handlers, WebUI

Concurrency: `std::thread` + atomics + `mpsc` channels. The main loop never blocks — it polls atomics at 10 ms ticks. Blocking operations (RMT pulse trains, OneWire bitbang, BLE notify) run in dedicated threads.

---

For the full technical specification — protocol reference, state machines, NVS layout, error hierarchy, thread model, build configuration, and dependencies — see [project.md](./docs/refs/project.md).
