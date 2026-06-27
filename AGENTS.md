# Build & Check Commands

- `PATH="/c/Users/vlbes/.pyenv/pyenv-win/versions/3.11.9:$PATH" cargo +esp build --target xtensa-esp32-espidf` — build firmware (pyenv fix for Windows)
- `cargo test --lib stepper::ramp::tests` — host-based ramp unit tests
- `espflash flash --port COM5 "target/xtensa-esp32-espidf/debug/ecotiter"` — flash only
- `timeout 30 python scripts/serial_monitor.py COM5` — monitor with 30s timeout
- WDT must be disabled during debugging: `unsafe { esp_idf_sys::esp_task_wdt_deinit(); }`

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

# GPIO Pinout

| Component | Pin | Driver |
|---|---|---|
| TMC2209 STEP | GPIO25 | TxChannelDriver (RMT) |
| TMC2209 DIR | GPIO26 | PinDriver::output |
| TMC2209 EN | GPIO27 | PinDriver::output (active LOW) |
| TMC2209 UART TX/RX | GPIO17/16 | (deferred) |
| Valve open/close | GPIO12/13 | PinDriver::output |
| Limit FULL | GPIO32 | PinDriver::input + ISR |
| Limit EMPTY | GPIO35 | PinDriver::input + ISR |
| DS18B20 | GPIO4 | OneWire bitbang |
| ADC (pH) | GPIO34 | ADC1 |
| LED | GPIO2 | PinDriver::output |
| USB-Serial RX | **GPIO3 — НЕ ТРОГАТЬ** | |

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

# COM Port Safety

- **ABSOLUTELY FORBIDDEN to launch background processes that hold COM ports.**
  Any blocking/serial tool (pio device monitor, serial terminal, etc.) MUST be run with an explicit timeout via the bash tool's `timeout` parameter or via `timeout <seconds>` prefix. Never leave a process occupying COM after exit.

# Python Script Rules

- NEVER use `python -c "..."` inline scripts — bash on Windows mangles quotes/backslashes.
- ALWAYS write Python code to a temp file first, then run it.
  Use `C:\Users\vlbes\AppData\Local\Temp\opencode` for temp scripts.
- Example:
  ```bash
  # Write temp script
  cat > "$TMPDIR/test_serial.py" << 'PYEOF'
  ... python code ...
  PYEOF
  python "$TMPDIR/test_serial.py"
  ```
