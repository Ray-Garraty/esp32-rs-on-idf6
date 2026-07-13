---
type: Architecture Reference
title: Constitution
description: Unshakeable architectural invariants — main loop non-blocking, task sovereignty, dual-core mandatory, DRAM init order, radio coexistence, boundary safety, hardware protection, memory philosophy
tags: [architecture, invariants, constitution, c++23, esp32-s3]
timestamp: 2026-07-13
---

# Constitution of the EcoTiter Firmware

**Status:** Unshakeable Architectural Invariants
**Scope:** Applies to all code, human or AI-generated. Violations require immediate revert.
**Supersedes:** Any contradictory rule in AGENTS.md, project.md, or other documentation.

---

## Article I: The Main Loop is Sacred (Non-Blocking)

The FreeRTOS `main` task (`app_main` loop) must **NEVER** execute a blocking operation.

- **Forbidden:** `rmt_tx_wait_all_done()`, `vTaskDelay()` > 10 ms, `xQueueReceive(portMAX_DELAY)`, `std::mutex::lock()`, synchronous I/O.
- **Allowed:** `std::atomic` operations, `try_lock()`, non-blocking polling, `vTaskDelayUntil(10 ms)`.
- **Rationale:** A blocked main loop starves the WiFi/BLE stacks, DNS responders, and watchdog feeders, leading to silent system death.

---

## Article II: Task Sovereignty (Independence)

FreeRTOS tasks are sovereign, independent programs. They **NEVER** wait for each other, **NEVER** call each other's functions, and **NEVER** share stack contexts.

- **Communication:** Exclusively via FreeRTOS queues (`xQueueSend`/`xQueueReceive`) or `std::atomic`.
- **Network I/O:** Only the `net_owner` task touches the network stack. Other tasks (e.g., `log_worker`, `motor`) push data to queues (`ws_send_queue`, `status_queue`).
- **Forbidden:** `xTaskNotifyWait`, `xSemaphoreTake` across task domains, or calling `httpd_*` / `ble_*` functions from non-network tasks.

---

## Article III: Dual-Core is Mandatory

The ESP32-S3 is a dual-core chip. Single-core mode is strictly forbidden in production.

- **Mandatory:** `CONFIG_FREERTOS_UNICORE=n` in `sdkconfig.defaults`.
- **Rationale:** Single-core mode causes deterministic spinlock deadlocks between WiFi and BLE drivers contending for the same CPU — the tick interrupt freezes, triggering RWDT reset.

---

## Article IV: The DRAM Triangle (Initialization Order)

Due to strict DRAM fragmentation limits, network and peripheral initialization must follow a strict sequence inside `net_owner`:

1. **WiFi** — Driver init, AP start, async STA connect
2. **HTTP Server** — Binds to 0.0.0.0:80, requires contiguous internal DRAM
3. **BLE NimBLE** — Requires contiguous internal DRAM
4. **PHY Calibration Guard** — `ensureGpioReady()`: deinit + reinit PHY to prevent spinlock deadlock

- **Forbidden:** Any other order. BLE before HTTP causes `ESP_ERR_HTTPD_TASK`. HTTP before WiFi causes IP binding failures.

---

## Article V: Communication Channel Priority

WiFi is a service/diagnostic tool only (initial setup, diagnostics). BLE is the primary operational channel for client applications (when UART is not connected).

### Priority Policy

| Scenario | Coexistence Setting | Rationale |
|----------|--------------------|-----------|
| BLE client connected and active | `ESP_COEX_PREFER_BT` | BLE is the operational channel |
| No BLE client | `ESP_COEX_PREFER_BALANCE` | WiFi service functions |
| Captive portal / initial setup | `ESP_COEX_PREFER_BALANCE` | Must function without interference |

### Enforcement

- On BLE connect + session start: call `esp_coex_preference_set(ESP_COEX_PREFER_BT)`
- On BLE disconnect / idle: revert to `ESP_COEX_PREFER_BALANCE`
- WiFi performance degradation during active BLE use is **acceptable**, as WiFi is not an operational channel.

---

## Article VI: Boundary Safety & Memory

### No Raw Pointers Across Threads

ESP-IDF opaque pointers (`httpd_req_t*`, `httpd_ws_frame_t*`) must never cross thread boundaries. Data must be copied into `std::array` or sent via async APIs (`httpd_ws_send_frame_async`).

### RAII is Law

All ESP-IDF handles must be wrapped in RAII classes. Naked handles are forbidden.

```cpp
// CORRECT
class RmtChannel {
    rmt_channel_handle_t handle_ = nullptr;
public:
    explicit RmtChannel(const rmt_tx_channel_config_t& cfg) {
        ESP_ERROR_CHECK(rmt_new_tx_channel(&cfg, &handle_));
    }
    ~RmtChannel() { if (handle_) rmt_del_channel(handle_); }
    RmtChannel(const RmtChannel&) = delete;
    RmtChannel(RmtChannel&& o) noexcept : handle_(std::exchange(o.handle_, nullptr)) {}
};

// FORBIDDEN
rmt_channel_handle_t channel; // naked handle
```

### Stack Budget

Stack sizes are fixed by architecture. `std::format()`, `nlohmann::json::dump()`, and large local arrays are forbidden in tasks with ≤ 8 KB stacks.

---

## Article VII: Hardware Protection

### RMT Stop Flags

Every RMT motion function MUST accept a `std::atomic<bool>*` stop flag and check it between chunks. If set — abort immediately.

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

### Unsafe GPIOs

Pins 26–37 (PSRAM/Flash bus) are strictly forbidden for `gpio_set_direction()` or `gpio_config()`. `gpio_set_level()` on GPIO27 (TMC2209 EN) is the sole exception — the pin is on the PSRAM data bus but `gpio_set_level` does not reconfigure the pad mux.

See `docs/refs/gpio_pins_spec.md` for the full list, explanations, and safe alternatives.

---

## Article VIII: Memory Philosophy — DRAM for Speed, PSRAM for Bulk

The ESP32-S3 has 8 MB Octal PSRAM and ~320 KB free internal DRAM after WiFi/BLE/HTTP init. Memory allocation strategy is a core architectural decision.

### Core Principle: Explicit Allocation

**Strategy:** `CONFIG_SPIRAM_USE_CAPS_ALLOC=y` (NOT `USE_MALLOC`).

- **Default behavior:** `malloc()` / `new` / `std::vector` → Internal DRAM
- **Bulk allocations:** Explicit `MALLOC_CAP_SPIRAM` via `heap_caps_malloc()` or PMR allocator
- **Rationale:** Explicit allocation makes intent auditable, prevents ISR-accessed data from accidentally landing in PSRAM (causing jitter), and protects ESP-IDF internal allocations from breaking.

### Mandatory PSRAM Usage (> 1 KB)

| Data Type | Context | Rationale |
|-----------|---------|-----------|
| JSON serialization/parsing | `net_owner`, `log_worker` | Large temporary strings |
| HTTP response buffers | REST API handlers | Payloads > 1 KB |
| Motion profiles (`computeRamp()`) | Motor task setup | Thousands of step intervals |
| LogBuffer ring storage | `log_worker` | 1000+ entries |
| WebUI static assets | `webui.cpp` | Embedded HTML/CSS/JS |
| Core dump staging | Crash handler | Intermediate buffers before flash write |

### Forbidden PSRAM Usage

- **Task stacks** — `CONFIG_SPIRAM_ALLOW_STACK_IN_EXT_PSRAM=n` mandatory
- **ISR-accessed data** — endstop flags, tick counters, atomic state read by ISR
- **RMT DMA buffers** — RMT controller cannot DMA-address PSRAM
- **`MotorState` atomics** — hot-path CPU latency required
- **IRAM code** — ISR handlers, panic handlers must be in internal SRAM
- **ESP-IDF driver handles** — allocated by ESP-IDF itself, we do not intervene

### Enforcement

All bulk PSRAM allocations MUST use explicit `heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` or a project-provided PMR allocator. Naked `new` / `malloc()` for bulk data (> 1 KB) is FORBIDDEN.

See `docs/refs/memory_spec.md` for detailed patterns, anti-patterns, and runtime verification.
