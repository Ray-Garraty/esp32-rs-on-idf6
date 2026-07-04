---
type: Testing Guide
title: Testing Guide
description: Three-tier testing strategy — host unit, on-device integration, and pytest HIL
tags: [testing, unit-test, integration, hil]
timestamp: 2026-06-29
---

# Testing Guide

## Overview

Three-tier testing strategy derived from the ASMPL autosampler project and adapted for the Rust + ESP-IDF v6 (`std` mode) stack with `espflash` as the runner.

| Tier | Scope | Command | Coverage |
|---|---|---|---|---|---|
| 1 | Host-based unit + property-based tests | `cargo test --lib` | ~70% — pure logic + mocks |
| 2 | On-device integration tests | Custom test binary + `espflash flash` | ~20% — hardware-dependent code |
| 3 | pytest HIL (hardware-in-the-loop) | `pytest --target esp32` | ~10% — end-to-end validation |

### Key Principles

1. **Pure logic** (ramp calculations, calibration, command parsing, state machines) lives in `esp-idf`-free modules and is tested on host
2. **Hardware access** is abstracted behind traits for mocking in unit tests
3. `#[cfg(target_arch = "xtensa")]` prevents `esp-idf` crates from being compiled for host tests
4. **Integration tests** require real hardware (ESP32-WROOM-32 on COM5) and are run separately from fast unit tests
5. Python scripts must never use `python -c ""` — always write to a temp file first (per AGENTS.md rules)

### Test Pyramid

```
           /  E2E (pytest HIL)  \          ~10%
          /   - Full dose via API   \
         /   - Captive portal flow   \
        /=============================\
       /  Integration (on-device)    \     ~20%
      /   - RMT stepper at 500 Hz    \
     /   - ADC calibration loop      \
    /=================================\
   /  Unit + Property-Based (host)   \     ~70%
  /   - compute_ramp()               \
 /    - State machine transitions     \
/    - Command parsing + validation  \
/    - Ramp invariants (proptest)    \
```

---

## Tier 1: Host-Based Unit Tests (x86_64)

Test business logic without hardware dependencies. These run on the development machine with `cargo test --lib`.

### Project Layout

```
src/
├── lib.rs               # Module declarations (cfg-gated for xtensa)
├── types.rs             # Newtypes — host-testable
├── errors.rs            # StepperError — host-testable
├── config.rs            # Constants — host-testable
├── stepper/
│   ├── mod.rs
│   ├── ramp.rs          # Pure logic — 10 host tests exist
│   └── rmt_stepper.rs   # xtensa only — not host-testable
├── wifi.rs              # DNS builder — has host tests
├── adc.rs               # Atomics only — host-testable calibration
├── temperature.rs       # Atomics + OneWire bitbang — logic partially testable
└── ...
```

> Reference implementation in `prototype/src/` — read-only, do not edit.

Modules with `#[cfg(target_arch = "xtensa")]` in `lib.rs` are excluded from host compilation.

### Example: Pure Logic Module (ramp.rs)

```rust
pub fn compute_ramp(total_steps: u32, config: &RampConfig) -> Vec<u32> {
    if total_steps == 0 {
        return Vec::new();
    }
    // ... trapezoidal acceleration logic (no hardware deps)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn cfg_1000_to_100() -> RampConfig {
        RampConfig::new(10, 10, 1000, 100)
    }

    #[test]
    fn zero_steps_returns_empty() {
        assert!(compute_ramp(0, &cfg_1000_to_100()).is_empty());
    }

    #[test]
    fn cruise_at_min_interval() {
        let r = compute_ramp(50, &cfg_1000_to_100());
        for i in 10..40 {
            assert_eq!(r[i], cfg_1000_to_100().min_interval_us);
        }
    }

    #[test]
    fn accel_first_is_max_interval() {
        let r = compute_ramp(50, &cfg_1000_to_100());
        assert_eq!(r[0], cfg_1000_to_100().max_interval_us);
    }

    #[test]
    fn decel_last_is_max_interval() {
        let r = compute_ramp(50, &cfg_1000_to_100());
        assert_eq!(r[49], cfg_1000_to_100().max_interval_us);
    }
}
```

### Example: Trait-Based Abstraction for Mocking

```rust
// src/stepper/mod.rs
pub trait StepperDriver {
    fn move_steps(&mut self, intervals: &[u32]) -> Result<(), StepperError>;
    fn stop(&mut self) -> Result<(), StepperError>;
    fn set_direction(&mut self, dir: Direction) -> Result<(), StepperError>;
}

#[cfg(test)]
pub mod mock {
    use super::*;

    pub struct MockStepper {
        pub last_intervals: Vec<u32>,
        pub stopped: bool,
    }

    impl StepperDriver for MockStepper {
        fn move_steps(&mut self, intervals: &[u32]) -> Result<(), StepperError> {
            self.last_intervals = intervals.to_vec();
            Ok(())
        }
        fn stop(&mut self) -> Result<(), StepperError> {
            self.stopped = true;
            Ok(())
        }
        fn set_direction(&mut self, _dir: Direction) -> Result<(), StepperError> {
            Ok(())
        }
    }
}

#[cfg(test)]
mod tests {
    use super::mock::MockStepper;

    #[test]
    fn mock_stepper_records_moves() {
        let mut stepper = MockStepper {
            last_intervals: Vec::new(),
            stopped: false,
        };
        stepper.move_steps(&[2000; 16]).unwrap();
        assert_eq!(stepper.last_intervals.len(), 16);
    }
}
```

### Example: DNS Response Tests (wifi.rs)

```rust
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_dns_response_has_expected_structure() {
        let query = build_test_query();
        let resp = build_dns_response(&query, [192, 168, 4, 1]);
        assert!(!resp.is_empty());
        assert_eq!(resp[2] & 0x80, 0x80, "QR bit should be set");
        assert_eq!(resp[7], 1, "answer count should be 1");
    }
}
```

### Running Tier 1

```bash
cargo test --lib                              # all host tests
cargo test --lib stepper::ramp::tests         # ramp-specific
cargo test --lib stepper::ramp::tests::zero_steps_returns_empty  # single test
```

### Property-Based Testing (proptest)

Add to `Cargo.toml`:

```toml
[dev-dependencies]
proptest = "1"
```

Property-based tests verify invariants across a wide range of random inputs. Add them alongside regular unit tests in the same `#[cfg(test)] mod tests` block.

```rust
#[cfg(test)]
mod property_tests {
    use proptest::prelude::*;

    proptest! {
        #[test]
        fn ramp_always_positive_intervals(steps in 1u32..10000) {
            let config = RampConfig::new(10, 10, 1000, 100);
            let ramp = compute_ramp(steps, &config);
            for &interval in &ramp {
                prop_assert!(interval > 0, "interval must be positive");
                prop_assert!(interval >= config.min_interval_us);
                prop_assert!(interval <= config.max_interval_us);
            }
        }

        #[test]
        fn ramp_total_length_matches_input(steps in 0u32..10000) {
            let config = RampConfig::new(10, 10, 1000, 100);
            let ramp = compute_ramp(steps, &config);
            prop_assert_eq!(ramp.len() as u32, steps);
        }

        #[test]
        fn ramp_monotonic_accel_decel(steps in 2u32..500) {
            let config = RampConfig::new(steps / 4, steps / 4, 1000, 100);
            let ramp = compute_ramp(steps, &config);
            let mid = ramp.len() / 2;
            // First half: intervals non-increasing (accelerating)
            for i in 1..mid {
                prop_assert!(ramp[i] <= ramp[i-1],
                    "accel: step {} interval {} > {}", i, ramp[i], ramp[i-1]);
            }
            // Second half: intervals non-decreasing (decelerating)
            for i in mid+1..ramp.len() {
                prop_assert!(ramp[i] >= ramp[i-1],
                    "decel: step {} interval {} < {}", i, ramp[i], ramp[i-1]);
            }
        }

        #[test]
        fn state_machine_no_panic_on_any_command(
            cmd in prop_oneof![
                Just(BuretteCommand::Fill(Ml(50.0))),
                Just(BuretteCommand::Dose { volume: Ml(5.0), speed: MlMin(10.0) }),
                Just(BuretteCommand::Stop),
                Just(BuretteCommand::Reset),
            ]
        ) {
            let mut ctrl = BuretteController::new(/* ... */);
            let result = ctrl.handle_command(cmd);
            // Must return either Ok or Err — never panic
            prop_assert!(result.is_ok() || result.is_err());
        }

        #[test]
        fn dns_response_never_exceeds_512_bytes(
            labels in prop::collection::vec("[a-z]{1,63}", 1..5)
        ) {
            let mut query = build_dns_query(&labels);
            let resp = build_dns_response(&query, [192, 168, 4, 1]);
            prop_assert!(resp.len() <= 512, "DNS response too large: {}", resp.len());
        }
    }
}
```

Run property tests:

```bash
cargo test --lib property_tests          # all prop tests
PROPTEST_CASES=10000 cargo test          # 10k test cases instead of default 256
```

---

## Tier 2: On-Device Integration Tests

These tests compile for `xtensa-esp32-espidf` and run on real hardware via `espflash`. They validate that hardware peripherals (RMT, GPIO, ADC, OneWire) work correctly.

NOTE: The `embedded-test` crate (probe-rs based) is **not** compatible with our stack. It requires `#![no_std]` + `#![no_main]` + JTAG/SWD probe (semihosting). Our project uses `esp-idf-hal` in `std` mode with flashing via `espflash` over UART. On-device tests use a **custom test binary** pattern instead.

### Approach: Custom Test Binary

Create a separate test binary (e.g., `tests/hw_test.rs` or a module behind a Cargo feature flag) that:

1. Initializes hardware once
2. Runs a sequence of test functions
3. Prints `PASS: <name>` or `FAIL: <name>` for each
4. Exits (or loops waiting for reset)

```rust
// tests/stepper_hw_test.rs
// Run: cargo +esp build --bin stepper_hw_test && espflash flash target/xtensa-esp32-espidf/debug/stepper_hw_test

use ecotiter_fw::stepper::rmt_stepper::RmtStepper;
use ecotiter_fw::types::Direction;

fn main() {
    esp_idf_sys::link_patches();
    unsafe { esp_idf_sys::esp_task_wdt_deinit(); }

    let peripherals = esp_idf_hal::peripherals::Peripherals::take().unwrap();

    // Test 1: Stepper init
    let mut stepper = RmtStepper::new(
        peripherals.pins.gpio25.degrade_output(),
        peripherals.pins.gpio26.degrade_output(),
        peripherals.pins.gpio27.degrade_output(),
    ).expect("RmtStepper::new()");
    println!("PASS: Stepper init");

    // Test 2: Stepper direction CW
    stepper.set_direction(Direction::Cw).unwrap();
    stepper.enable().unwrap();
    let chunk = vec![2000; 16];
    stepper.move_steps(&chunk).unwrap();
    println!("PASS: Stepper moves 16 steps at 500 Hz CW");

    // Test 3: Stepper direction CCW
    stepper.set_direction(Direction::Ccw).unwrap();
    stepper.move_steps(&chunk).unwrap();
    println!("PASS: Stepper moves 16 steps at 500 Hz CCW");

    stepper.disable().unwrap();
    println!("ALL TESTS PASSED");
}
```

Register the binary in `Cargo.toml`:

```toml
[[bin]]
name = "stepper_hw_test"
path = "tests/stepper_hw_test.rs"
test = false
```

### Running Tier 2

```bash
# Build test binary
PATH="/c/Users/vlbes/.pyenv/pyenv-win/versions/3.11.9:$PATH" \
  cargo +esp build --bin stepper_hw_test --target xtensa-esp32-espidf

# Flash
espflash flash --port COM5 "target/xtensa-esp32-espidf/debug/stepper_hw_test"

# Monitor with timeout
timeout 60 python scripts/serial_monitor.py COM5
```

### One-Shot Integration via main.rs Feature Flag

For simpler cases, gate test code behind a Cargo feature:

```toml
[features]
hw-test = []
```

```rust
// in main.rs
#![cfg_attr(feature = "hw-test", allow(dead_code))]

#[cfg(feature = "hw-test")]
fn run_hw_tests() {
    // ... test sequence
}

fn main() {
    #[cfg(feature = "hw-test")]
    return run_hw_tests();

    // normal firmware boot ...
}
```

```bash
cargo +esp build --features hw-test --target xtensa-esp32-espidf
```

---

## Tier 3: pytest HIL (Hardware-in-the-Loop)

Automated testing on real hardware with host-side control via `pytest-embedded`. The host PC connects to the ESP32 via USB-Serial (COM5), flashes firmware, sends commands, and validates responses.

### Installation

```bash
pip install pytest-embedded pytest-embedded-serial-esp
```

### Project Structure

```
pytests/
├── conftest.py          # DUT fixture, port configuration
├── test_burette.py      # Burette command/response tests
├── test_wifi.py         # Captive portal, AP tests
├── test_sensors.py      # ADC, temperature readback
└── pytest.ini           # Optional: pytest config
```

### conftest.py

```python
import pytest

@pytest.fixture(scope='module')
def port():
    return 'COM5'

@pytest.fixture(scope='module')
def app_path():
    """Path to the built firmware binary."""
    import os
    return os.path.join(os.path.dirname(__file__), '..')
```

### Test Example: Burette Commands

```python
# tests/test_burette.py
import pytest

@pytest.mark.parametrize('target', ['esp32'], indirect=True)
@pytest.mark.generic
def test_ping_response(dut):
    """Verify that /api/ping returns expected JSON."""
    import json
    import urllib.request

    # Wait for boot
    dut.expect('=== esp32-rs-on-idf6 ===', timeout=30)

    # Get the AP IP (firmware starts in AP mode)
    ip = '192.168.4.1'

    response = urllib.request.urlopen(f'http://{ip}/api/ping').read()
    data = json.loads(response)
    assert data['status'] == 'ok'

@pytest.mark.flaky(reruns=2, reruns_delay=5)
@pytest.mark.parametrize('target', ['esp32'], indirect=True)
@pytest.mark.generic
def test_wifi_captive_portal(dut):
    """Test captive portal redirect."""
    dut.expect('AP ready at', timeout=30)
    dut.expect_exact('HTTP server started on port 80')

    # Send a request that should redirect to /wifi
    import urllib.request
    from urllib.error import HTTPError

    try:
        urllib.request.urlopen('http://192.168.4.1/generate_204')
    except HTTPError as e:
        assert e.code == 302  # redirect
        assert e.headers['Location'] == '/wifi'
```

### Running Tier 3

```bash
# Build firmware first
PATH="/c/Users/vlbes/.pyenv/pyenv-win/versions/3.11.9:$PATH" \
  cargo +esp build --target xtensa-esp32-espidf

# Run all HIL tests
pytest --target esp32 --port COM5

# Run specific test
pytest --target esp32 --port COM5 -k test_ping_response

# Run with verbose output
pytest --target esp32 --port COM5 -v

# Debug: capture all serial output
pytest --target esp32 --port COM5 --log-cli-level=DEBUG
```

### ESP-IDF pytest-embedded Reference

| Feature | Usage |
|---|---|
| `dut.expect(pattern)` | Wait for regex match on serial |
| `dut.expect_exact(string)` | Wait for exact string match |
| `dut.write(data)` | Write data to serial (to ESP32 stdin) |
| `dut.reset()` | Reset the device (DTR pulse) |
| `@pytest.mark.flaky(reruns=N, reruns_delay=S)` | Retry flaky tests |
| `@pytest.mark.xfail(condition, reason=...)` | Mark known failures |
| `--target esp32` | Select target chip |
| `-k "not slow"` | Filter tests by keyword expression |

---

## Test Organization

### Naming Conventions

```rust
// Test function names: {behaviour}_{condition}
#[test]
fn zero_steps_returns_empty() { /* ... */ }
#[test]
fn cruise_at_min_interval() { /* ... */ }
#[test]
fn triangular_no_cruise() { /* ... */ }
```

```python
# Python test names: test_{feature}_{scenario}
def test_ping_response(): ...
def test_wifi_captive_portal(): ...
def test_burette_fill_then_empty(): ...
```

### Directory Structure

```
D:\ecttr/
├── src/                              # Production source code
│   ├── lib.rs                        # Crate root — cfg-gated modules
│   ├── stepper/
│   │   ├── mod.rs                    # StepperDriver trait + re-exports
│   │   ├── ramp.rs                   # Pure logic + inline unit tests
│   │   └── rmt_stepper.rs            # xtensa only
│   └── wifi.rs                       # Some inline tests
│
├── tests/                            # Tier 2: on-device binaries
│   ├── stepper_hw_test.rs
│   ├── sensor_hw_test.rs
│   └── Cargo.toml                    # (optional) separate manifest
│
├── pytests/                          # Tier 3: pytest HIL
│   ├── conftest.py
│   ├── test_burette.py
│   ├── test_wifi.py
│   └── requirements.txt              # pytest-embedded
│
├── prototype/                        # Read-only reference implementation
│   └── src/                          # Do NOT edit — consult for patterns
│
└── docs/
    ├── guides/testing.md             # This file
    └── refs/coding_style.md
```

### cfg Attributes

```rust
// In lib.rs — gate xtensa-only modules
#[cfg(target_arch = "xtensa")]
pub mod rmt_stepper;

// Inline tests — always host-compiled
#[cfg(test)]
mod tests {
    use super::*;
    // ...
}
```

---

## Pre-Commit Checklist

Run these in order before every commit:

```bash
# 1. Host unit tests (fast, must pass)
cargo test --lib

# 2. Clippy (zero warnings)
cargo +esp clippy -- -D warnings

# 3. Xtensa build (zero errors)
PATH="/c/Users/vlbes/.pyenv/pyenv-win/versions/3.11.9:$PATH" \
  cargo +esp build --target xtensa-esp32-espidf

# 4. (optional) Flash + smoke test
espflash flash --port COM5 "target/xtensa-esp32-espidf/debug/ecotiter" && timeout 30 python scripts/serial_monitor.py COM5
```

### What to Check

| Check | Enforced by | Action if fails |
|---|---|---|
| All `unsafe` blocks have safety comments | Manual review | Add comment or refactor |
| No `unwrap()`/`expect()` in library code | Manual review | Replace with `?` propagation |
| All `#[cfg(target_arch = "xtensa")]` correct | Build test | Fix cfg gate |
| `cargo test --lib` passes | CI / pre-commit | Fix failing test |
| `clippy -- -D warnings` passes | CI / pre-commit | Fix warning |
| No blocking calls in main loop | Manual review | Move to dedicated thread |
| Python files use temp-file pattern | Manual review | Never `python -c ""` |

---

## Appendix: pytest-embedded Quick Reference

### Installation

```bash
pip install pytest-embedded pytest-embedded-serial-esp
```

### Common CLI Options

| Option | Example | Description |
|---|---|---|
| `--target` | `--target esp32` | Target chip |
| `--port` | `--port COM5` | Serial port |
| `-k` | `-k "test_ping"` | Filter tests by name |
| `-v` | `-v` | Verbose output |
| `--log-cli-level` | `--log-cli-level=DEBUG` | Log level |
| `--pipeline-id` | `--pipeline-id 123456` | Download CI-built binaries |

### DUT API

```python
# Wait for regex match (timeout defaults to 30s)
dut.expect(r'\[INFO\] ADC: raw_mv=\d+')

# Wait for exact string
dut.expect_exact('=== esp32-rs-on-idf6 ===')

# Write data to ESP32 serial input
dut.write(b'{"cmd": "dose", "ml": 5.0}\n')

# Reset board via DTR
dut.reset()

# Run all Unity test cases (if using Unity framework)
dut.run_all_single_board_cases(group='stepper')
```

### Markers

```python
# Run only on specific target
@pytest.mark.parametrize('target', ['esp32'], indirect=True)
@pytest.mark.generic

# Flaky test (retry up to 3 times with 5s delay)
@pytest.mark.flaky(reruns=3, reruns_delay=5)

# Known failure on specific target
@pytest.mark.xfail('config.getvalue("target") == "esp32s2"',
                   reason='Not implemented on ESP32-S2')

# Nightly-only test (skipped in CI PR pipelines)
@pytest.mark.nightly_run
```
