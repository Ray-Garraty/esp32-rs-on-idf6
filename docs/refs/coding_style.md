---
type: Architecture Reference
title: Coding Style Guide
description: Coding conventions, error hierarchy, state machine patterns, memory budget, and concurrency rules for esp32-rs-on-idf6 firmware
tags: [coding-style, conventions, architecture]
timestamp: 2026-06-29
---

# Coding Style Guide

Based on proven conventions from the ASMPL autosampler project, adapted for the esp32-rs-on-idf6 ESP32 + Rust + ESP-IDF v6 stack, and extended with [Qwen-recommended](https://github.com/qwen-lm) architectural principles.

## 1. Layered Architecture

Four layers with strict one-way dependency: `domain` -> `application` -> `infrastructure` -> `interface`.

**Golden rule:** `domain/` must NEVER import `esp-idf-*` crates. Only `infrastructure/` talks to hardware. `application/` coordinates using domain types and infrastructure traits.

See [project.md](./project.md) for the full crate structure.

## 2. Error Hierarchy

Three-level typed errors with automatic conversion:

```rust
// errors.rs
#[derive(Debug)]
pub enum AppError {
    Hardware(HardwareError),
    Protocol(ProtocolError),
    State(StateError),
    Resource(ResourceError),
}

#[derive(Debug)]
pub enum HardwareError {
    StepperMotor(StepperError),
    Sensor(SensorError),
    Network(NetworkError),
}

#[derive(Debug)]
pub enum StepperError {
    InitFailed { reason: &'static str },
    Rmt { code: i32 },
    LimitSwitchTriggered { switch: LimitSwitchId },
    Timeout { operation: &'static str },
}

// From impls for automatic ? conversion
impl From<StepperError> for AppError {
    fn from(e: StepperError) -> Self {
        AppError::Hardware(HardwareError::StepperMotor(e))
    }
}
impl From<esp_idf_sys::EspError> for StepperError {
    fn from(e: esp_idf_sys::EspError) -> Self {
        StepperError::Rmt { code: e.code() }
    }
}
```

### Recovery Strategy

```rust
pub trait Recoverable {
    fn can_recover(&self) -> bool;
    fn recovery_action(&self) -> Option<RecoveryAction>;
}

impl Recoverable for AppError {
    fn can_recover(&self) -> bool {
        matches!(self, AppError::Hardware(HardwareError::Network(_)))
    }
    fn recovery_action(&self) -> Option<RecoveryAction> {
        match self {
            AppError::Hardware(HardwareError::Network(_)) => {
                Some(RecoveryAction::Retry)
            }
            _ => None,
        }
    }
}
```

## 3. Enum over Trait (choose deliberately)

### When to use enums

```rust
// GOOD: Enum for state machines, commands, errors
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Direction { Cw, Ccw }

#[derive(Debug)]
pub enum BuretteState { Idle, Homing, Filling { .. }, Dosing { .. }, Error { .. } }
```

### When to use traits

```rust
// GOOD: Trait for hardware abstraction (mockable in tests)
pub trait StepperMotor {
    fn move_steps(&mut self, steps: Steps, speed: Hz) -> Result<(), StepperError>;
    fn stop(&mut self) -> Result<(), StepperError>;
    fn position(&self) -> Steps;
}

// BAD: Trait for what should be an enum
pub trait BuretteState { fn process(&self) -> Result<(), Error>; }
struct IdleState; struct DosingState;  // Use enum instead!
```

See [project.md](./project.md) for the concrete `RmtStepper` implementation.

## 4. State Machine: Explicit Enum + Exhaustive Match

```rust
// Type-safe state with contextual payloads
pub enum BuretteState {
    Idle,
    Homing { started_at: Instant },
    Filling { target_ml: Ml, progress: Ml },
    Dosing { remaining_ml: Ml, speed: MlMin },
    Rinsing { phase: RinsePhase, cycles_left: u32 },
    Stopping,
    Error { code: ErrorCode, message: heapless::String<64> },
}

pub enum BuretteCommand {
    Fill(Ml),
    Dose { volume: Ml, speed: MlMin },
    Stop,
    Reset,
}
```

Validation pipeline order:
1. **Safety** -- limit switches, valve position
2. **Concurrency** -- return `Busy` if already moving
3. **State Logic** -- reject invalid transitions via exhaustive match
4. **Parameter Validation** -- bounds, ranges
5. **Execution** -- perform the action

```rust
impl BuretteController {
    pub fn handle_command(&mut self, cmd: BuretteCommand) -> Result<Response, AppError> {
        match (&self.state, &cmd) {
            (BuretteState::Idle, BuretteCommand::Fill(_)) => { /* OK */ },
            (BuretteState::Idle, BuretteCommand::Dose { .. }) => { /* OK */ },
            (BuretteState::Filling { .. }, BuretteCommand::Fill(_)) => {
                return Err(AppError::State(StateError::AlreadyRunning));
            },
            (_, BuretteCommand::Stop) => { /* Always allowed */ },
            _ => return Err(AppError::State(StateError::InvalidTransition)),
        }
        self.transition(cmd)
    }
}
```

## 5. Memory: Budget Enforcement

Fixed-size buffers for all hot paths. No heap allocations in main loop or motor thread.

```rust
// src/domain/memory.rs -- central budget definition
pub mod memory {
    use heapless::{String, Vec, Deque};
    use crate::logging::LogEntry;

    pub const MAX_COMMAND_SIZE: usize = 256;
    pub const MAX_RESPONSE_SIZE: usize = 512;
    pub const LOG_BUFFER_SIZE: usize = 100;
    pub const ADC_BUF_SIZE: usize = 64;
    pub const DNS_BUF_SIZE: usize = 512;

    pub type CommandBuffer = Vec<u8, MAX_COMMAND_SIZE>;
    pub type ResponseBuffer = Vec<u8, MAX_RESPONSE_SIZE>;
    pub type LogBuffer = Deque<LogEntry, LOG_BUFFER_SIZE>;
}
```

```rust
// GOOD: Fixed-size buffers for hot paths
let mut adc_buf = [0u16; memory::ADC_BUF_SIZE];
let mut dns_buf = [0u8; memory::DNS_BUF_SIZE];
```

```rust
// ALLOWED: Vec at init or config-change time only
let intervals = compute_ramp(total_steps, &config); // Vec -- computed once per config
```

```rust
// BAD: Vec/String allocation in main loop or motor thread
loop {
    let data: Vec<u32> = some_dynamic_data();  // NO -- use fixed buffer
    let json = serde_json::to_string(&status).unwrap(); // NO -- pre-allocate
}
```

## 6. Concurrency: Atomics + Channels, Never Block Main Loop

### Golden Rule

The main loop (FreeRTOS `main` task) must **never** execute a blocking operation.

| Context | Allowed | Forbidden |
|---|---|---|
| Main loop | `Atomic*` read/write, `try_lock()`, poll functions, `sleep(10ms)` | `send_and_wait()`, `lock()`, `recv()`, `sleep()` > 10ms |
| Motor thread | `send_and_wait()`, blocking RMT, `Ets::delay_us` | Mutex contention |
| HTTP server task | Blocking writes to socket fd | Long computations |
| BLE notify thread | `mpsc::recv()`, `server.notify()` | Main loop blocking |

### Channel-Based Communication

```rust
// Motor thread reads from channel, writes atomics
std::thread::Builder::new()
    .stack_size(4096)
    .name("motor".into())
    .spawn(move || {
        for cmd in cmd_rx {
            match cmd {
                Command::Move { steps, speed } => {
                    TARGET.store(steps.0, Ordering::Release);
                    SPEED.store(speed.0, Ordering::Release);
                }
                Command::Stop => { ENABLED.store(false, Ordering::Release); }
            }
        }
    });
```

### Atomic Ordering

```rust
pub struct MotorState {
    target: AtomicI32,
    current: AtomicI32,
    enabled: AtomicBool,
}

impl MotorState {
    pub fn update_current(&self, pos: i32) {      // motor thread (writer)
        self.current.store(pos, Ordering::Release);
    }
    pub fn current(&self) -> i32 {                  // main loop (reader)
        self.current.load(Ordering::Acquire)
    }
    pub fn try_enable(&self) -> bool {               // compare-and-swap
        self.enabled.compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire).is_ok()
    }
}
```

### Mutex Safety

- `try_lock()` only in main loop (never `lock()`)
- `Arc<Mutex<T>>` only for shared resources between tasks
- Never hold a mutex across a blocking call

```rust
// GOOD:
if let Ok(mut wifi) = wifi_mgr.try_lock() { wifi.process(); }

// BAD:
let wifi = wifi_mgr.lock().unwrap(); // may block HTTP task
```

## 7. Types and Constants

```rust
// GOOD: Newtype wrappers for domain units
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Steps(pub i32);
pub struct Hz(pub u32);
pub struct Ml(pub f32);
pub struct MlMin(pub f32);

// GOOD: Named constants, no magic numbers
pub const AP_SSID: &str = "EcoTiter-AP";
pub const MOTOR_THREAD_STACK: usize = 4096;
pub const HTTP_SERVER_STACK: usize = 12288;
pub const RMT_RESOLUTION: u32 = 1_000_000;
```

```rust
// BAD: Magic numbers
stepper.move_steps(64, 2000);  // what is 64? what is 2000?
```

## 8. Structured Logging

```rust
// domain/logging.rs
#[derive(Debug, Clone)]
pub struct LogEntry {
    pub timestamp: u64,
    pub level: Level,
    pub module: &'static str,
    pub message: heapless::String<128>,
}

// Application logging usage
info!("Stepper move completed"; steps = steps.0, speed = speed.0);
warn!("ADC read error"; error = %e, channel = 34);
error!("Limit switch triggered"; switch = ?switch_id, action = "emergency_stop");

// Logger implementation in infrastructure/
// Backed by log crate + EspLogger (std mode) + ring buffer for HTTP access
```

## 9. ESP32 Special Rules

### 9.1 WDT

Must be disabled at boot -- RMT `send_and_wait()` blocks > 250 ms.

```rust
unsafe {
    // Safe: called once at boot, before any task uses WDT
    esp_idf_sys::esp_task_wdt_deinit();
}
```

### 9.2 RMT Stepper

- `TxChannelDriver::new(step_pin, &config)` -- accepts `impl OutputPin + 'd`
- `send_and_wait()` **only in motor thread**, never in main loop
- `RmtChannel` trait must be in scope for `disable()`

### 9.3 GPIO Pins

- `GpioXX` have private fields -- use `peripherals.pins.gpioXX.degrade_output()`
- TMC2209 EN is active LOW -- call `set_low()` in constructor

### 9.4 Unsafe

Every `unsafe` block must have a comment explaining why it is safe.

### 9.5 Thread Stack Sizes

| Thread | Stack | Notes |
|---|---|---|
| Main loop | 16 KB | `CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384` |
| Motor (RMT stepper) | 4 KB | |
| Temperature (DS18B20) | 16 KB | Bitbang call chain |
| BLE notify | 8 KB | |
| HTTP server | 12 KB | `stack_size: 12288` |
| std::thread default | 8 KB | `CONFIG_PTHREAD_TASK_STACK_SIZE_DEFAULT=8192` |

### 9.6 `PinDriver` Generics

`PinDriver<'d, MODE>` has **1** generic argument (MODE), not 2:

```rust
pub struct RmtStepper<'d> {
    dir: PinDriver<'d, Output>,
    en: PinDriver<'d, Output>,
}
```

## 10. Testing

See [testing.md](../guides/testing.md) for the 3-tier strategy, and [project.md](./project.md) for key test scenarios.

## 11. Documentation

### When to write doc comments
- Incorrect usage can **damage hardware** or cause WDT reset
- **Semantics differ from signature** (side effects, ordering, 1-based indices)
- Function has **preconditions** the caller must satisfy

```rust
/// Feed the Task Watchdog Timer to prevent ESP32 reset.
/// Must be called at least every ~5 s (default WDT timeout).
pub fn feed_watchdog() { /* ... */ }
```

### When NOT to write doc comments
- Trivial getters (`fn is_enabled(&self) -> bool`)
- Self-explanatory enum variants (`Direction::Cw`, `Error::Busy`)

## 12. Anti-Patterns (Forbidden Patterns)

```rust
// BAD: Global mutable state
static mut GLOBAL_STATE: Option<System> = None;

// GOOD: Pass via channels or Arc<Mutex>
pub struct System { /* ... */ }
```

```rust
// BAD: Blocking in main loop
loop { http_client.get("/api/data").wait(); /* BLOCKS! */ }

// GOOD: Non-blocking polling
loop {
    if let Some(response) = http_client.try_poll() { process(response); }
    sleep(10ms);
}
```

```rust
// BAD: unwrap() in library code
pub fn read_sensor() -> f32 { adc.read().unwrap() }

// GOOD: Proper error propagation
pub fn read_sensor() -> Result<f32, SensorError> { Ok(adc.read()?) }
```

```rust
// BAD: Magic numbers
stepper.move_steps(64, 2000);

// GOOD: Named constants
stepper.move_steps(config::HOMING_STEPS, config::HOMING_SPEED_HZ);
```

## 13. Pre-Merge Checklist

### Architecture
- [ ] Code follows layered architecture (`domain/` -> no `esp-idf` imports)
- [ ] State machine uses exhaustive matching on enum pairs
- [ ] No circular dependencies between modules

### Memory
- [ ] No `Vec`/`String` in main loop or motor thread
- [ ] `heapless` containers for all hot paths
- [ ] All buffers have fixed sizes (defined in `memory` module)

### Concurrency
- [ ] Main loop has NO blocking operations
- [ ] Correct `Ordering` semantics (`Release`/`Acquire`/`AcqRel`)
- [ ] `try_lock()` used in main loop, never `lock()`

### Error Handling
- [ ] No `unwrap()`/`expect()` in library code
- [ ] Errors use typed hierarchy (`AppError -> HardwareError -> StepperError`)
- [ ] Every `unsafe` block has a safety comment

### Testing
- [ ] `cargo test --lib` passes
- [ ] `cargo +esp clippy -- -D warnings` passes
- [ ] `cargo +esp build --target xtensa-esp32-espidf` passes

## 14. Dependency Version Duplicates

Embedded ecosystem is in transition -- multiple major versions of core crates coexist:

| Crate | Versions | Rationale |
|---|---|---|
| `embedded-hal` | 0.2 + 1.0 | `esp-idf-hal` exposes both legacy and new trait APIs |
| `embedded-io` | 0.6 + 0.7 | `embassy` (0.6) and `embedded-svc` (0.7) on different tracks |
| `heapless` | 0.8 + 0.9 | `embassy-sync` pins 0.8; all ESP-IDF crates use 0.9 |
| `bitflags` | 1.3 + 2.13 | Host build tooling vs firmware deps |

**Policy:**
- Duplicates are **accepted** -- ~5-10 KB binary overhead is acceptable on ESP32 (520 KB SRAM)
- **DO NOT** `[patch]` versions without verifying API compatibility
- Monitor binary size (`cargo bloat`), not version count
- Warning suppressed via `#![allow(clippy::multiple_crate_versions)]`

## Summary

| Principle | Rule |
|---|---|
| **Layers** | `domain` -> `application` -> `infrastructure` -> `interface`. One-way deps only. |
| **Error Handling** | 3-level hierarchy: `AppError -> HardwareError -> StepperError`. No `unwrap` in lib. |
| **Memory** | Fixed-size `heapless` buffers for hot paths. `Vec` only at init/config-change. |
| **State Machine** | Explicit enum + exhaustive match. Pipeline: Safety -> Busy -> State -> Params -> Execute. |
| **Concurrency** | `Atomic*` for ISR->task. `mpsc` for task->task. **Never block main loop.** `try_lock()`. |
| **Types** | Newtype for domain units (`Steps`, `Hz`, `Ml`). Named constants -- no magic numbers. |
| **Testing** | Host units + on-device integration + pytest HIL + **property-based** (proptest). |
| **Anti-patterns** | No global state, no blocking in main loop, no `unwrap` in lib, no magic numbers. |
