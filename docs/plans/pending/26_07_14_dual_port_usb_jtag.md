---
type: Plan
title: Migrate JSON command/response and broadcast from UART0 to USB Serial/JTAG
description: >
  Split the current single-UART0 architecture into two independent ports:
  USB Serial/JTAG (GPIO19/20) for JSON command/response and broadcast data,
  UART0 (GPIO1/3) exclusively for debug logging (ESP_LOG, printf, core dumps,
  panic handler). BLE and WiFi channels remain unchanged.
tags: [usb, serial, jtag, uart, dual-port, architecture]
timestamp: 2026-07-14
status: verified
---

# Dual-Port USB Migration: UART0 → USB Serial/JTAG for Command Traffic

## 1. Overview

### Objective
Move all JSON command/response and broadcast data from UART0 (GPIO1/3, external
USB-UART bridge) to the ESP32-S3 native USB Serial/JTAG port (GPIO19/20),
leaving UART0 exclusively for debug output (ESP_LOG, printf, core dumps,
panic handler). BLE NUS and WiFi WebSocket channels remain unchanged.

### Motivation
- **COM port conflicts:** UART0 is shared between debug logging and JSON protocol
  traffic, making it impossible to monitor debug logs independently of command data.
- **Clean separation:** Debug output (ESP_LOG) stays on the classic COM port
  accessible via any serial monitor. Command/response uses the native USB CDC-ACM
  port that appears when the ESP32-S3's USB Serial/JTAG peripheral is enabled.
- **Full-speed USB:** The USB Serial/JTAG controller (CDC-ACM) communicates at
  full-speed (12 Mbps) compared to UART0's 115200 baud — significantly faster
  broadcast and response throughput.
- **Hardware-robust connection detection:** `usb_serial_jtag_is_connected()`
  uses SOF (Start-of-Frame) packets to detect host presence — more reliable
  than the current heuristic (any UART data = connected).

### Design
```
┌─────────────────────────────────────────────────────────────┐
│  ESP32-S3 Firmware                                           │
│                                                              │
│  UART0 (GPIO1/3)          USB Serial/JTAG (GPIO19/20)        │
│  ┌──────────────────┐    ┌──────────────────────────────┐   │
│  │ ESP_LOG          │    │ JSON commands (RX)           │   │
│  │ printf           │    │ JSON responses (TX)          │   │
│  │ core dumps       │    │ Broadcast events (TX, 300ms) │   │
│  │ panic handler    │    │ usb_serial_jtag_is_connected │   │
│  └──────────────────┘    └──────────────────────────────┘   │
│                                                              │
│  ┌─────────────────────────────────────────────────┐         │
│  │ SerialReader class (fd → /dev/usbserjtag)       │         │
│  │ select() + read() for non-blocking RX            │         │
│  │ write() for TX (response + broadcast)            │         │
│  └─────────────────────────────────────────────────┘         │
└─────────────────────────────────────────────────────────────┘
```

## 2. Scope — All Files and Changes

### 2.1 Files to Modify

#### A. `components/interface/include/interface/serial.hpp`
| Lines | Change | Reason |
|-------|--------|--------|
| All | Add `[[nodiscard]] bool isConnected() const noexcept;` | Wrap `usb_serial_jtag_is_connected()` for SOF-based USB detection |
| ~31-34 | Destructor body changes (platform-specific) | Calls `usb_serial_jtag_driver_uninstall()` instead of `uart_driver_delete()` |
| ~136 | Change `int fd_{-1}` semantics | fd now points to `/dev/usbserjtag` instead of `/dev/uart/0` |
| ~69 | Keep `INPUT_BUF_SIZE = 256` unchanged | Buffer size is adequate for USB as well |

No changes to `process()` or `write()` signatures — they already work
over generic fd using `select()` + `read()` + `::write()`.

#### B. `components/interface/src/serial.cpp`
| Lines | Change | Reason |
|-------|--------|--------|
| 1-14 | Replace includes | Remove `driver/uart.h`, `driver/uart_vfs.h`. Add `driver/usb_serial_jtag.h`, `driver/usb_serial_jtag_vfs.h`. Keep `esp_vfs_dev.h` if needed, though `usb_serial_jtag_vfs_register()` does its own VFS registration |
| 19-24 | Destructor: replace `uart_driver_delete(UART_NUM_0)` with `usb_serial_jtag_driver_uninstall()` | Cleanup USB driver instead of UART driver |
| 26-78 | `init()`: Replace entire UART init sequence with USB Serial/JTAG init | **New sequence:** (1) `usb_serial_jtag_driver_install()` with config, (2) `usb_serial_jtag_vfs_register()`, (3) `usb_serial_jtag_vfs_use_nonblocking()` for select()-compatible reads, (4) `fd_ = open("/dev/usbserjtag", O_RDWR | O_NONBLOCK)` |
| 80-102 | `process()`: Keep unchanged | Uses fd_ with select()+read() — same pattern works for any VFS fd |
| 104-112 | `write()`: Keep unchanged | Uses fd_ with ::write() — works for any VFS fd |
| New | Add `isConnected()` method | Returns `usb_serial_jtag_is_connected()` |
| New | Add `#include "driver/usb_serial_jtag.h"` | For USB Serial/JTAG driver API |

#### C. `components/interface/CMakeLists.txt`
| Line | Change | Reason |
|------|--------|--------|
| 12 | Replace `esp_driver_uart` with `esp_driver_usb_serial_jtag` | Interface no longer needs UART driver; needs USB Serial/JTAG driver instead |
| - | Keep `REQUIRES domain, application, json, esp_http_server` unchanged | These dependencies remain |

**Note:** `esp_driver_uart` stays in `infrastructure/CMakeLists.txt` (line 26)
for TMC2209 UART communication (GPIO16/17) — that component is unchanged.

#### D. `main/main.cpp`
| Lines | Change | Reason |
|-------|--------|--------|
| 90 | Remove `gUsbHandshakeReceived` include if only used here | Will be replaced by `serial.isConnected()` |
| 526 | `.usbSerialConnected = ecotiter::domain::gUsbHandshakeReceived.load(...)` → `.usbSerialConnected = serial.isConnected()` | Use real SOF-based detection |
| 581 | `if (domain::gUsbHandshakeReceived.load(...))` → `if (serial.isConnected())` | LED transport SM uses real connection state |
| 623 | `if (domain::gUsbHandshakeReceived.load(...))` → `if (serial.isConnected())` | TransportState setter uses real connection state |
| 674 | Remove `ecotiter::domain::gUsbHandshakeReceived.store(true, ...)` | No longer set flag on any data arrival; connection state comes from USB SOF detection |
| New | Track `prevUsbConnected`. false→true: `bleManager.stopAdvertising()` + `bleManager.disconnect()`. true→false: `bleManager.startAdvertising()`. | Article V: USB absolute priority — BLE shuts down on USB connect, restarts on USB disconnect. |

#### E. `components/domain/include/domain/types.hpp`
| Line | Change | Reason |
|------|--------|--------|
| 90 | Deprecate/remove `gUsbHandshakeReceived` | Replaced by `serial.isConnected()` in main.cpp; no other component reads this atomic |

**Note:** Keep `gUsbHandshakeReceived` for this migration phase but mark as
deprecated with a comment. Remove in follow-up cleanup after everything verifies.

#### F. `components/interface/include/interface/broadcast.hpp`
| Line | Change | Reason |
|------|--------|--------|
| 21 | `bool usbSerialConnected;` — keep field name | Backward-compatible JSON schema. The data source changes from `gUsbHandshakeReceived` to `serial.isConnected()` in main.cpp |

#### G. `scripts/find_port.py`
| Line | Change | Reason |
|------|--------|--------|
| 4-10 | Add port preference logic for UART-over-USB bridges vs native USB | When both ports are connected, `flash` and `monitor` MUST target UART0 (CP2102/CH340/FTDI), NOT USB Serial/JTAG (0x303A). Add VID priority: 0x10C4(CP2102), 0x1A86(CH340), 0x0403(FTDI) > 0x303A(ESP32 native) |
| New | Add `find_uart_port()` function that returns UART bridge port only | Used by `idf.sh flash` and `idf.sh monitor` to target the debug UART |
| New | Add `find_usb_serial_port()` function that returns native USB port only | Used by future USB test scripts |

#### H. `scripts/idf.sh`
| Lines | Change | Reason |
|-------|--------|--------|
| 46-60 | `resolve_port()`: Prioritize UART bridge ports over native USB | Flash + monitor must use UART0 (COM port), not USB Serial/JTAG |
| ~254 | `PORT=$(python3 "$SCRIPT_DIR/find_port.py")` in `smoke` subcommand | After fix, this returns UART bridge port for flash, and monitor also uses it |

#### I. `scripts/monitor.py`
No changes needed. It connects to whatever serial port is passed (or auto-detected)
via `find_port.py`. Since `find_port.py` will prioritize UART bridges, monitor
will see UART0 (debug logs) correctly.

#### J. `docs/refs/gpio_pins_spec.md`
| Lines | Change | Reason |
|-------|--------|--------|
| 96 | Change `"Not used (WiFi/HTTP/BLE only, no USB host/peripheral)"` to `"USB Serial/JTAG data port — JSON command/response + broadcast"` | Document that GPIO19/20 are now in use for data traffic |

#### K. `docs/refs/project.md`
| Lines | Change | Reason |
|-------|--------|--------|
| 60 | Change `"USB-Serial \| GPIO1/3 (U0TXD/RXD, DO NOT TOUCH) \| JSON command/response, debug logging"` to `"UART0 (debug) \| GPIO1/3 \| Debug logging, core dumps, panic handler"` | Reflect new split |
| 61 | Add row: `"USB Serial/JTAG \| GPIO19/20 (native USB) \| JSON command/response, broadcast events"` | Document new port |

### 2.2 Files to Create
None. All changes are modifications to existing files.

#### L. `components/infrastructure/network/include/infrastructure/network/ble.hpp`
| Method | Purpose |
|--------|---------|
| `void stopAdvertising() noexcept` | `ble_gap_adv_stop(0)` — stop advertising |
| `void startAdvertising() noexcept` | `ble_gap_adv_start()` — start advertising |
| `void disconnect() noexcept` | `ble_gap_terminate(connHandle_)` — disconnect current BLE connection |

#### M. `components/infrastructure/network/src/ble.cpp`
Three new methods, each wrapped in `diag::FfiGuard guard(N)`:
```cpp
void BleManager::stopAdvertising() noexcept {
    if (!initialized_) return;
    diag::FfiGuard guard(66);
    ble_gap_adv_stop(0);
}

void BleManager::startAdvertising() noexcept {
    if (!initialized_) return;
    if (connected_) return;  // already connected — no advertising needed
    diag::FfiGuard guard(67);
    ble_gap_adv_start(ownAddrType_, nullptr, BLE_HS_FOREVER,
                      &ADV_PARAMS, gapEventCallback, nullptr);
}

void BleManager::disconnect() noexcept {
    if (!initialized_ || !connected_) return;
    diag::FfiGuard guard(68);
    ble_gap_terminate(connHandle_, BLE_ERR_REM_USER_CONN_TERM);
}
```

### 2.3 Files Unchanged
- `components/infrastructure/CMakeLists.txt` — still needs `esp_driver_uart` for TMC2209
- `tests/src/test_serial.cpp` — uses `process(string_view)` overload, no hardware dependency
- `components/interface/src/broadcast.cpp` — broadcast format unchanged
- `components/interface/src/rest_api.cpp` — HTTP API unchanged
- `components/application/` — command dispatch unchanged
- `components/infrastructure/network/http_server.cpp` — WiFi unchanged
- `components/infrastructure/network/wifi.cpp` — WiFi unchanged
- `components/infrastructure/network/ble_notify_thread.cpp` — notify thread unchanged
- `scripts/ble_test.py` — BLE testing unchanged
- `partitions.csv` — no partition changes needed
- `sdkconfig.defaults` — `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` stays; core dumps stay on UART0

## 3. Implementation Steps (Ordered)

### Step 1: Update Interface CMakeLists (no-op build test)
**Files:** `components/interface/CMakeLists.txt`
**Change:** Replace `esp_driver_uart` with `esp_driver_usb_serial_jtag` in REQUIRES.
**Effort:** 5 min
**AC:** `scripts/idf.sh build` succeeds

### Step 2: Update SerialReader header
**Files:** `components/interface/include/interface/serial.hpp`
**Changes:**
- Add `[[nodiscard]] bool isConnected() const noexcept;` declaration (public)
- No other structural changes
**Effort:** 10 min
**AC:** Builds successfully (host test compiles)

### Step 3: Rewrite SerialReader implementation
**Files:** `components/interface/src/serial.cpp`
**Changes:**
- Replace all `driver/uart.h` / `driver/uart_vfs.h` includes with `driver/usb_serial_jtag.h` / `driver/usb_serial_jtag_vfs.h`
- Rewrite `init()`:
  1. `usb_serial_jtag_driver_install(&config)` with `tx_buffer_size=256, rx_buffer_size=256`
  2. `usb_serial_jtag_vfs_register()` — creates `/dev/usbserjtag`
  3. `usb_serial_jtag_vfs_use_nonblocking()` — enables select()-compatible reads
  4. `fd_ = open("/dev/usbserjtag", O_RDWR | O_NONBLOCK)`
- Rewrite destructor:
  1. `::close(fd_)`
  2. `usb_serial_jtag_driver_uninstall()`
- Add `isConnected()` body: `return usb_serial_jtag_is_connected();`
- Modify `write()` to suppress `ESP_LOGE` when USB is not connected — in non-blocking VFS mode, `usb_serial_jtag_write()` returns -1 unconditionally when `usb_serial_jtag_is_connected()` is false. Without this fix, broadcast writes (every 300ms) would flood LogBuffer when only COM is connected.
- Keep `process()` unchanged (uses generic fd ops)

**Implementation details:**
```cpp
Result<void> SerialReader::init() noexcept {
    usb_serial_jtag_driver_config_t cfg = {
        .tx_buffer_size = 256,
        .rx_buffer_size = 256,
        .intr_priority = 0,
    };
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_serial_jtag_driver_install failed: %s", esp_err_to_name(err));
        return std::unexpected(SerialError::InitFailed);
    }

    err = usb_serial_jtag_vfs_register();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_serial_jtag_vfs_register failed: %s", esp_err_to_name(err));
        usb_serial_jtag_driver_uninstall();
        return std::unexpected(SerialError::InitFailed);
    }

    usb_serial_jtag_vfs_use_nonblocking();

    fd_ = open("/dev/usbserjtag", O_RDWR | O_NONBLOCK);
    if (fd_ < 0) {
        ESP_LOGE(TAG, "open /dev/usbserjtag failed");
        usb_serial_jtag_driver_uninstall();
        return std::unexpected(SerialError::InitFailed);
    }

    ESP_LOGI(TAG, "SerialReader initialized on USB Serial/JTAG, fd=%d", fd_);
    return {};
}

SerialReader::~SerialReader() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    usb_serial_jtag_driver_uninstall();
}

bool SerialReader::isConnected() const noexcept {
    return usb_serial_jtag_is_connected();
}

void SerialReader::write(std::string_view s) noexcept {
    if (fd_ < 0 || s.empty() || !usb_serial_jtag_is_connected()) {
        return;
    }
    ssize_t written = ::write(fd_, s.data(), s.size());
    if (written < 0) {
        ESP_LOGE(TAG, "write failed: errno=%d", errno);
    }
}
```

Note the `!usb_serial_jtag_is_connected()` guard: in non-blocking VFS mode,
`usb_serial_jtag_tx_char_no_driver()` returns -1 unconditionally when the
host is not connected (no SOF). Without this guard, every broadcast cycle
(300ms) would flood LogBuffer with stale-errno `ESP_LOGE`.

**Effort:** 45 min
**AC:** `scripts/idf.sh build` succeeds; unit tests pass (host-side tests use
the `process(string_view)` overload which is unchanged)

### Step 4: Update main.cpp connection logic
**Files:** `main/main.cpp`
**Changes:**
- Line 526: `.usbSerialConnected = serial.isConnected()`
- Line 581: `if (serial.isConnected())` (LED SM)
- Line 623: `if (serial.isConnected())` (TransportState)
- Line 674: Remove `gUsbHandshakeReceived.store(true, ...)` (no longer set on data arrival)

**Important:** The LED logic for "USB active → LED off" changes:
```
Old: gUsbHandshakeReceived set on ANY UART data → LED off
New: serial.isConnected() (SOF detection) → LED off
```
This means the LED turns off ONLY when a USB host is actually connected to
the USB Serial/JTAG port and is sending SOF packets, which is more correct.

**Effort:** 15 min
**AC:** `scripts/idf.sh build` succeeds

### Step 5: Deprecate gUsbHandshakeReceived in domain types
**Files:** `components/domain/include/domain/types.hpp`
**Change:** Add deprecation comment to `gUsbHandshakeReceived`. Keep it
for now (referenced by nothing after Step 4 but safer to leave during
transition). Mark with `// DEPRECATED: use serial.isConnected() instead`.
**Effort:** 5 min
**AC:** Build succeeds

### Step 6: Update find_port.py for dual-port detection
**Files:** `scripts/find_port.py`
**Changes:**
- Add `find_uart_port()` — returns first port with VID in {0x10C4, 0x1A86, 0x0403}
  (excludes 0x303A)
- Add `find_usb_serial_port()` — returns first port with VID 0x303A
- Keep `find_esp32_port()` as fallback (returns any ESP32 VID)
- Change default behavior: when called directly (`__main__`), prefer UART bridge
  over native USB

**Effort:** 20 min
**AC:** `python3 scripts/find_port.py` returns UART bridge port when both connected

### Step 7: Update idf.sh for correct port targeting
**Files:** `scripts/idf.sh`
**Changes:**
- `resolve_port()`: Use `find_uart_port()` instead of `find_esp32_port()` for
  flash and monitor operations
- Keep fallback chain: specific port → UART bridge → first ESP32 → /dev/ttyUSB0

**Effort:** 10 min
**AC:** `scripts/idf.sh flash` and `scripts/idf.sh smoke` correctly target UART0

### Step 8: Update documentation
**Files:**
- `docs/refs/gpio_pins_spec.md` (line 96)
- `docs/refs/project.md` (lines 60-61, 224-230, 379)

**Changes:** Reflect new port split and coexistence semantics as described
in §2.1 items J and K.

**Effort:** 15 min
**AC:** Documentation accurately describes new architecture

### Step 9: Smoke test on real hardware
**Command:** `scripts/idf.sh smoke`
**What to expect:**
1. Build succeeds
2. Flash succeeds (via UART0 bootloader)
3. 30s serial monitor shows:
   - `BOOT OK: ecotiter v...` on UART0
   - `SerialReader initialized on USB Serial/JTAG, fd=...` on UART0
   - All ESP_LOG debug output on UART0
   - No Guru Meditation, no WDT panic
4. USB Serial/JTAG port appears as `/dev/ttyACM0` (or similar) on host

**Effort:** 15 min
**AC:** All conditions above met

## 4. Risks & Mitigations

### R1: Boot hang on USB Serial/JTAG driver install
| Risk | Level | Description | Mitigation |
|------|-------|-------------|------------|
| Driver install fails if USB peripheral is not clocked/reset properly | **Medium** | `usb_serial_jtag_driver_install()` might fail or hang if USB peripheral was left in bad state by bootloader | Add timeout/retry in init. Fallback: `SerialReader::init()` returns error, main loop continues without USB (same as current UART init failure). Log the error to UART0. |

### R2: VFS conflict with console
| Risk | Level | Description | Mitigation |
|------|-------|-------------|------------|
| `usb_serial_jtag_vfs_register()` conflicts with console if both try to own the same VFS path | **Low** | Console is on UART0 (`CONFIG_ESP_CONSOLE_UART_DEFAULT=y`), so it does NOT touch USB Serial/JTAG VFS. The USB VFS only registers `/dev/usbserjtag`. No conflict. | Verify: console stays on UART0. Test with smoke. |

### R3: `select()` behavior on `/dev/usbserjtag`
| Risk | Level | Description | Mitigation |
|------|-------|-------------|------------|
| Non-blocking mode (`usb_serial_jtag_vfs_use_nonblocking()`) may not support `select()` | **Medium** | Need to verify that `select()` on `/dev/usbserjtag` fd returns correctly when data is available. Raw driver mode (`usb_serial_jtag_vfs_use_driver()`) has interrupt-driven reads but doesn't support select(). | Test with smoke: send a JSON command via USB serial terminal, verify it's processed. If `select()` doesn't work, switch to read-with-zero-timeout pattern. |

### R4: USB not connected at boot (no SOF packets)
| Risk | Level | Description | Mitigation |
|------|-------|-------------|------------|
| `usb_serial_jtag_is_connected()` returns false when USB cable is unplugged | **Low — desired behavior** | SerialReader still has valid fd. Reads return nothing. Writes succeed (data goes to TX buffer, drains when USB connects). The transport SM correctly reports "not connected". | This is correct behavior. `serial.isConnected()` accurately reflects physical USB state. The system operates on BLE instead. When USB is reconnected, SOF resumes and `isConnected()` returns true. |

### R5: Both USB Serial/JTAG and UART0 ports visible as COM ports
| Risk | Level | Description | Mitigation |
|------|-------|-------------|------------|
| User may accidentally send commands to UART0 (they disappear) or try to see debug on USB Serial/JTAG | **Medium** | Two serial ports appear on host. Commands sent to UART0 are silently dropped (no JSON processor there). Debug logs don't appear on USB. | Document the split clearly in README. The UART0 monitor shows "USB command traffic moved to USB Serial/JTAG port" on boot. |

### R6: Regression in BLE coexistence
| Risk | Level | Description | Mitigation |
|------|-------|-------------|------------|
| USB Serial/JTAG driver affects BLE/WiFi RF performance | **Low** | USB full-speed (12 MHz) does not share the 2.4 GHz radio. No RF interference expected. | Verify BLE connect/disconnect cycle in smoke test after migration. |

### R7: USB Serial/JTAG TX buffer full (backpressure)
| Risk | Level | Description | Mitigation |
|------|-------|-------------|------------|
| If host USB serial monitor is not reading, `usb_serial_jtag_write_bytes()` may block (in driver mode) or silently drop (in non-blocking mode) | **Low** | In non-blocking VFS mode, `::write()` returns the number of bytes written, which may be less than requested. | `SerialReader::write()` currently ignores partial writes and only logs errno. Acceptable for embedded use. Could add retry logic in future. |

### R8: PSRAM/DRAM pressure from USB buffer
| Risk | Level | Description | Mitigation |
|------|-------|-------------|------------|
| USB Serial/JTAG driver adds ~512 B (256 TX + 256 RX) of internal DRAM buffers | **Low** | This is trivial compared to the ~320 KB available DRAM after init. | No mitigation needed. Monitor heap stats in smoke test. |

### R9: 50ms busy-wait in non-blocking TX when FIFO full
| Risk | Level | Description | Mitigation |
|------|-------|-------------|------------|
| `usb_serial_jtag_tx_char_no_driver()` busy-waits up to 50ms (`TX_FLUSH_TIMEOUT_US = 50000`) if the HW TX FIFO is full (host USB serial client is not reading) | **Low** | The main loop runs at 10ms pacing (`vTaskDelayUntil` at 10ms). A 50ms busy-wait inside broadcast write (300ms interval) can cause the main loop to miss multiple ticks. In practice this only occurs when the host serial monitor has stopped reading, which is a transient condition. | **Accept** — broadcast (300ms) + occasional responses rarely coincide with a full TX FIFO. If main loop jitter becomes observable on a logic analyzer or via TickWatchdog, two escalation options exist: (a) switch to `usb_serial_jtag_vfs_use_driver()` for interrupt-driven TX (but reads become blocking), or (b) push broadcast writes to a dedicated low-priority worker thread via queue (architecturally clean, higher effort). |

## 5. Edge Cases

### EC-1: USB Not Plugged In
- `usb_serial_jtag_is_connected()` returns false
- `fd_` is valid (driver installed, VFS registered)
- `select()` on fd returns immediately (no data)
- `write()` returns -1 immediately (no TX buffering in non-blocking VFS mode
  — data written directly to HW FIFO, not through ring buffer)
- Transport SM falls to BLE or advertising
- **Expected behavior:** System works via BLE/WiFi only. Commands sent via USB
  Serial/JTAG go nowhere. Broadcast frames are silently dropped. Debug logs
  appear on UART0 (COM port).
- **Recovery:** Plug in USB → SOF starts → `isConnected()` returns true →
  writes succeed again. No data replay on reconnect (dropped frames are lost).

### EC-2: Hot-Plug (USB Connected Mid-Operation)
- `usb_serial_jtag_is_connected()` transitions false → true
- No driver reinit needed (driver stays installed)
- LED transitions from blue/green → OFF
- Transport SM transitions from BLE → USB active
- **Expected behavior:** Seamless transition. No data loss (RX was buffered by
  USB hardware). BLE stays connected.

### EC-3: COM Port Only (No USB Cable, Only UART0)
- Legacy debugging scenario: user only connects the USB-UART bridge to UART0
- USB Serial/JTAG driver installs fine (no cable needed)
- `isConnected()` returns false (no SOF)
- Debug logs appear on UART0 as before
- No JSON commands can be sent (no USB Serial/JTAG)
- BLE remains the operational channel if needed
- **Expected behavior:** Works like old system but no JSON over UART0.

### EC-4: Both Ports Connected (Common Development Setup)
- UART0 → debug logs
- USB Serial/JTAG → JSON commands + responses + broadcast
- User runs two terminal windows: one on COM (monitor logs), one on USB (send commands)
- `find_port.py` returns UART0 for flash/monitor
- **Expected behavior:** Cleanest experience — split works as designed.

### EC-5: Driver Init Failure at Boot
- If `usb_serial_jtag_driver_install()` fails (unlikely on real hardware):
  - `SerialReader::init()` returns `SerialError::InitFailed`
  - `fd_` stays -1
  - `serial.write()` becomes a no-op (no response, no broadcast over USB)
  - `serial.process()` returns nullopt (no command input)
  - `serial.isConnected()` returns false (driver not installed)
  - System still works via BLE/WiFi
  - Debug logs on UART0 show "Serial init failed"
  - **Expected behavior:** Graceful degradation. BLE/WiFi take over.

### EC-6: USB Disconnect Mid-Broadcast
- `serial.write()` is called every 300ms for broadcast
- If USB disconnects mid-write:
  - `::write()` returns < requested bytes (in non-blocking mode) or errors
  - `SerialReader::write()` currently logs but returns void
  - Next broadcast cycle tries again
  - When USB reconnects, TX resumes
  - **Expected behavior:** No crash. Partial broadcast frame silently dropped.

### EC-7: Flashing (Bootloader) Still Needs UART0
- Bootloader ROM uses GPIO1/3 (U0TXD/RXD) for download
- USB Serial/JTAG download is possible (USB download boot mode via GPIO3 strapping)
  but NOT supported by current workflow
- `idf.py flash` must still target UART0
- **Expected behavior:** No change to flashing workflow. `idf.sh flash` continues
  to use UART0.

### EC-8: Core Dumps Still Go to UART0
- `CONFIG_ESP_COREDUMP_ENABLE_TO_UART=y` directs core dumps to UART0
- `scripts/monitor.py` captures and saves core dumps from UART0
- **Expected behavior:** Core dump workflow unchanged.

## 6. Dependencies

| Dep | From | To | Type |
|-----|------|----|------|
| Step 1 | interface/CMakeLists.txt | Build system | Must come first (build breaks otherwise) |
| Step 2 | serial.hpp | All other steps | Header must be correct for impl |
| Step 3 | serial.cpp | main.cpp (Step 4) | Impl must exist before main.cpp changes |
| Step 4 | main.cpp | Smoke test (Step 9) | Core logic change |
| Step 5 | types.hpp | - | Optional cleanup after Step 4 |
| Step 6 | find_port.py | idf.sh (Step 7) | Script fix before smoke |
| Step 7 | idf.sh | Smoke test (Step 9) | Must target correct port |
| Step 8 | docs/ | - | Final documentation update |
| Step 9 | Hardware | All | Real ESP32-S3 needed |

**Implementation order:** 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9
(With verification at each step: build + host tests)

## 7. Acceptance Criteria

| ID | Description | Verification Method |
|----|-------------|-------------------|
| AC-001 | `scripts/idf.sh build` succeeds with 0 errors, 0 warnings — all `esp_driver_uart` references updated, USB Serial/JTAG driver linked correctly | `automated` (build) |
| AC-002 | All 159+ host unit tests pass: `scripts/idf.sh test` — particularly test_serial.cpp which uses the `process(string_view)` overload (no hardware dependency) | `automated` (host test) |
| AC-003 | On real hardware: UART0 (COM port) shows all ESP_LOG output including "SerialReader initialized on USB Serial/JTAG" — no JSON data appears on UART0 | `integration` (flash + monitor on UART0 for 30s) |
| AC-004 | USB Serial/JTAG port (CDC-ACM) appears as a serial port on the host (e.g., `/dev/ttyACM0` on Linux) | `manual` (user confirms port visible with `ls /dev/tty*`) |
| AC-005 | JSON command sent to USB Serial/JTAG port receives correct JSON response | `manual` (user sends `{"cmd":"ping"}` to USB port, confirms `{"status":"ok"}` response) |
| AC-006 | Broadcast events (every 300ms) are visible on USB Serial/JTAG port | `manual` (user monitors USB port; sees `{"ts":...` every ~300ms) |
| AC-007 | `serial.isConnected()` returns true when USB cable is connected; false when disconnected | `manual` (user checks LED behavior: plugged in → LED off; unplugged → LED blue/green within 1s) |
| AC-008 | Transport SM reflects correct USB state: `TransportState::UsbActive` when USB connected, `BleConnected` when USB off + BLE on | `inspection` (logs on UART0 show transport state transitions) |
| AC-009 | BLE remains connected when USB connects (coexistence) — unlike old behavior where USB disconnects BLE | `manual` (user connects BLE, then plugs USB; BLE stays connected, commands work from both) |
| AC-010 | Core dump on panic still goes to UART0 (not USB) | `inspection` (verify `CONFIG_ESP_COREDUMP_ENABLE_TO_UART=y` is unchanged in sdkconfig.defaults) |
| AC-011 | `scripts/idf.sh flash` still works targeting UART0 bootloader | `automated` (smoke test flash step succeeds) |
| AC-012 | `scripts/idf.sh smoke` succeeds: build + flash + 30s monitor on UART0, no Guru/panic/WDT | `integration` (smoke test) |
| AC-013 | ESP_LOG capture → LogBuffer still works (logs visible on WebUI dashboard) | `manual` (user opens WebUI dashboard, sees log entries) |
| AC-014 | Broadcast over WebSocket unchanged | `manual` (user opens WebSocket client, sees extended broadcast JSON) |
| AC-015 | BLE NUS command/response unchanged | `integration` (`scripts/ble_test.py` — ping/pong over BLE) |
| AC-016 | No Guru Meditation, WDT panic, or crash during 120-second dual-port operation | `integration` (120s smoke test with both UART0 + USB monitored) |

## 8. Effort Estimate

| Step | Description | Files | Effort (hours) |
|------|-------------|-------|----------------|
| 1 | Update interface CMakeLists | 1 | 0.1 |
| 2 | Update SerialReader header | 1 | 0.2 |
| 3 | Rewrite SerialReader implementation | 1 | 1.0 |
| 4 | Update main.cpp connection logic | 1 | 0.5 |
| 5 | Deprecate `gUsbHandshakeReceived` | 1 | 0.1 |
| 6 | Update find_port.py | 1 | 0.3 |
| 7 | Update idf.sh | 1 | 0.2 |
| 8 | Update documentation | 2 | 0.3 |
| 9 | Smoke test + verification | N/A | 0.5 |
| **Buffer** | Debug & unexpected issues | N/A | 1.0 |
| **Total** | | **10 files modified** | **4.2 hours** |

**Estimated effort:** M (1–2 days of focused work, ~4 hours coding + testing)

## 9. Rollback Plan

If the smoke test fails or any AC is not met:

**Tier 1 — Quick revert (single file issue):**
- If `SerialReader` init fails: revert `serial.cpp` and `serial.hpp` to UART0
  init, undo CMakeLists change. All other changes (main.cpp connection logic,
  docs) are compatible with UART0 SerialReader. The system returns to original
  single-port mode.

**Tier 2 — Full rollback:**
```bash
git checkout -- components/interface/src/serial.cpp
git checkout -- components/interface/include/interface/serial.hpp
git checkout -- components/interface/CMakeLists.txt
git checkout -- main/main.cpp
git checkout -- scripts/find_port.py
git checkout -- scripts/idf.sh
git checkout -- docs/refs/gpio_pins_spec.md
git checkout -- docs/refs/project.md
git checkout -- components/domain/include/domain/types.hpp
```

Then verify:
```bash
./scripts/idf.sh build && ./scripts/idf.sh test && ./scripts/idf.sh smoke
```

If smoke passes, the system is back to original state.

**Tier 3 — Build fails resolution:**
- If `esp_driver_usb_serial_jtag` is not available in the installed ESP-IDF:
  - Check that IDF_PATH is set correctly (`~/.espressif/v6.0.1/esp-idf`)
  - Run `idf.py reconfigure` after CMakeLists changes
  - If component doesn't exist in installed IDF, switch to raw driver API
    without VFS (use `usb_serial_jtag_read_bytes/write_bytes` directly,
    bypassing VFS). SerialReader would need `process()` and `write()` rewrites.

## Appendix A: Key API Reference

### USB Serial/JTAG Driver API (ESP-IDF v6.0.1)
Header: `driver/usb_serial_jtag.h` in component `esp_driver_usb_serial_jtag`

```c
// Install driver
esp_err_t usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t *config);

// Read/write
int usb_serial_jtag_read_bytes(void* buf, uint32_t length, uint32_t ticks_to_wait);
int usb_serial_jtag_write_bytes(const void* src, size_t size, uint32_t ticks_to_wait);

// Connection detection (SOF-based)
bool usb_serial_jtag_is_connected(void);

// Flush TX
esp_err_t usb_serial_jtag_wait_tx_done(uint32_t ticks_to_wait);

// Cleanup
esp_err_t usb_serial_jtag_driver_uninstall(void);
```

### USB Serial/JTAG VFS API
Header: `driver/usb_serial_jtag_vfs.h` in component `esp_driver_usb_serial_jtag`

```c
// Register VFS at /dev/usbserjtag
esp_err_t usb_serial_jtag_vfs_register(void);

// Non-blocking mode (select()-compatible, polled reads)
void usb_serial_jtag_vfs_use_nonblocking(void);

// Driver mode (interrupt-driven reads, raw API)
void usb_serial_jtag_vfs_use_driver(void);
```

### Alternative: Raw Driver Mode (if VFS select() fails)
If `select()` on `/dev/usbserjtag` in non-blocking VFS mode does not work,
the `process()` method falls back to polling with zero-timeout:

```cpp
std::optional<std::string_view> SerialReader::process() noexcept {
    // Read with zero timeout (non-blocking poll)
    uint8_t buf[INPUT_BUF_SIZE];
    int n = usb_serial_jtag_read_bytes(buf, INPUT_BUF_SIZE, 0);
    if (n <= 0) {
        return std::nullopt;
    }
    return splitBuffer(std::string_view(reinterpret_cast<char*>(buf),
                                        static_cast<size_t>(n)));
}
```

However, this poll-only mode is less efficient than VFS+select(). The VFS
approach should be attempted first and tested.

## Appendix B: Port Detection Priority Logic

The `find_port.py` must handle the scenario where both a USB-UART bridge
(CP2102, CH340, FTDI) and the native USB Serial/JTAG (0x303A) are connected.

```
USB-UART bridge (VID ≠ 0x303A) ── UART0 (GPIO1/3)  → flash + monitor (debug logs)
ESP32-S3 native (VID = 0x303A)  ── USB D+/D- (GPIO19/20) → JSON data
```

**Resolution for scripts:**
- `idf.sh flash` → must target UART0 (bootloader protocol over UART)
- `idf.sh monitor` → must target UART0 (ESP_LOG output)
- `idf.sh smoke` → flash to UART0, monitor on UART0
- Future `scripts/usb_monitor.py` → targets USB Serial/JTAG (JSON data)

The `resolve_port()` function in `idf.sh` must:
1. If explicit port given, use it
2. Else find UART bridge port (VID in {0x10C4, 0x1A86, 0x0403})
3. Else fall back to first ESP32 VID
4. Else use /dev/ttyUSB0
