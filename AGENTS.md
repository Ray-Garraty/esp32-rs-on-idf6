# Build & Check Commands

- `. /home/vlabe/export-esp.sh && cargo +esp build --target xtensa-esp32-espidf` — build firmware (source export-esp.sh first)
- `cargo test --lib stepper::ramp::tests` — host-based ramp unit tests
- `espflash flash --port /dev/ttyUSB0 "target/xtensa-esp32-espidf/debug/ecotiter"` — flash only (adjust port as needed)
- `timeout 30 python3 scripts/serial_monitor.py` — monitor with 30s timeout (auto-detects port)
- WDT must be disabled during debugging: `unsafe { esp_idf_sys::esp_task_wdt_deinit(); }`

# GOLDEN RULE: NEVER BLOCK THE MAIN LOOP

The main loop (`main.rs`, FreeRTOS task `main`) must NEVER execute a blocking operation. Any blocking API call (`send_and_wait`, `sleep` > 1ms, `recv`, synchronous I/O, mutex contention with unbounded wait) MUST live in a dedicated task/thread.

Liftime-blocking operations (RMT transmit, file I/O, HTTP body read, DNS query) are ONLY allowed in:
- `std::thread::spawn()` tasks with appropriate stack size
- FreeRTOS tasks created via `xTaskCreate`

The main loop may only:
- Read atomics
- Lock mutexes with `try_lock()` (not `lock()`)
- Write to pre-opened file descriptors (non-blocking)
- Call `process()` / `poll()` style functions that return immediately
- `sleep(Duration::from_millis(10))` as pacing tick (exception — 10ms tick is the heartbeat)

Violation of this rule — any blocking call added to main loop — invalidates all changes and requires immediate revert.

# ESP32 Crash Investigation

Any ESP32 crash (Guru Meditation, StoreProhibited, LoadProhibited, stack overflow, abort) requires **immediate investigation and fix** using tools described in [ESP-IDF Core Dump Guide](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32/api-guides/core_dump.html).

Mandatory steps:
1. Record EXCCAUSE, EXCVADDR, registers A0–A15, PC.
2. Decode backtrace via `xtensa-esp32-elf-addr2line` or `espcoredump.py`.
3. If backtrace is corrupted — analyze registers: typical patterns are use-after-free (EXCVADDR = 0xFFFFFFA0, i.e. NULL + offset), stack overflow, buffer overflow.
4. If crash is related to HTTP server — always check `stack_size` in `EspHttpServer::Configuration` first.
5. Guru Meditation is never "one-time" or "pre-existing" — root cause must be found and fixed.

# RMT Stepper API (esp-idf-hal v0.46, IDF v6)

- `TxChannelDriver::new(pin: impl OutputPin, config: &TxChannelConfig) -> Result<Self, EspError>` — create TX channel
- `CopyEncoder::new() -> Result<CopyEncoder, EspError>` — encoder for &[Symbol]
- `channel.send_and_wait(encoder, signal, &TransmitConfig{..})` — blocking transmit
- `RmtChannel` trait must be in scope for `disable()`
- `PinDriver<'d, Output>` has **1** generic arg (MODE), not 2
- Use `pin.degrade_output()` to convert concrete GPIO to `AnyOutputPin`
- `EspError` from `esp_idf_sys`, not `esp_idf_hal`
- `PinDriver::set_low()` / `set_high()` — control EN/DIR
- EN pin **active LOW**: call `set_low()` in constructor

# Common Issues

- `rst:0x8 (TG1WDT_SYS_RESET)` — WDT timeout from blocking RMT. Add `esp_task_wdt_deinit()`.
- GPIO pin constructors have private fields — use `peripherals.pins.gpioXX.degrade_output()`
- `esp32-nimble` needs local patch for IDF v6: add `all(esp_idf_version_major = "6")` to two `cfg_if!` blocks in `ble_characteristic.rs`

# Project Conventions

- Never break existing functionality or business logic.
- Never invent methods/hooks/APIs — verify against official docs or source.
- Never fix symptoms; always find root cause.
- Answer user questions immediately, clearly, concisely.
- Show only changed fragments with file paths.
- Never assert physical-world events — ask the user what they observe.
- Core principles: DRY, KISS, YAGNI.
- Prefer cohesive units over fragmented micro-modules.
- Extract non-obvious constants to config module or top of file.
- All pipelines must pass with 0 errors, 0 warnings — no "pre-existing" excuses.
- Use LF (\n) line endings.
- Two-strike rule: 2 attempts per task, then stop and consult user.

# Serial Port Safety

- **ABSOLUTELY FORBIDDEN to launch background processes that hold serial ports.**
  Any blocking/serial tool (pio device monitor, serial terminal, etc.) MUST be run with an explicit timeout via the bash tool's `timeout` parameter or via `timeout <seconds>` prefix. Never leave a process occupying a serial port (ttyUSB/ttyACM) after exit.

# Python Script Rules

- NEVER use `python -c "..."` inline scripts — bash on Windows mangles quotes/backslashes.
- ALWAYS write Python code to a temp file first, then run it.
  Use `/tmp/opencode` for temp scripts.
- Example:
  ```bash
  # Write temp script
  cat > "$TMPDIR/test_serial.py" << 'PYEOF'
  ... python code ...
  PYEOF
  python "$TMPDIR/test_serial.py"
  ```
