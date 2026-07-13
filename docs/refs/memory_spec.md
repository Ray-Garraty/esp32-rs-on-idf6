---
type: Architecture Reference
title: Memory Management Specification
description: >
  ESP32-S3 memory architecture, PSRAM strategy (CONFIG_SPIRAM_USE_CAPS_ALLOC),
  allocation patterns via PMR, runtime verification, and anti-patterns.
  Covers the DRAM Triangle interaction, DMA constraints, and cache eviction risks.
tags: [memory, psram, dram, esp32-s3, allocation, heap-caps, pmr]
timestamp: 2026-07-13
revision: 1 (post-implementation sync)
changelog:
  - 2026-07-13: Synced code listings to actual implementation.
    Key changes: throw→abort (no exceptions), computeRamp defaults,
    LogBuffer init() pattern, added ALLOW_BSS_SEG=n, moved monitoring to diag/.
---

# Memory Management Specification

Single source of truth for memory allocation strategy on ESP32-S3 with 8 MB Octal PSRAM.
Violations cause: DRAM fragmentation, ISR jitter, WDT resets, or silent data corruption.

**Authoritative references (read BEFORE writing memory-related code):**
- Online: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/external-ram.html
- Online: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/mem_alloc.html
- Local headers: `<device root>/home/vlabe/Downloads/esp-idf-master/components/esp_psram/include/esp_psram.h`
- Local headers: `<device root>/home/vlabe/Downloads/esp-idf-master/components/heap/include/esp_heap_caps.h`

---

## 1. Memory Architecture Overview

### 1.1 Physical Memory Layout

| Memory Type | Size | Speed | Latency | Usage |
|---|---|---|---|---|
| **Internal SRAM (DRAM)** | ~512 KB total, ~320 KB free after full init | 240 MHz (CPU clock) | 1 cycle | Stacks, ISR data, hot-path atomics, ESP-IDF drivers |
| **Internal SRAM (IRAM)** | ~160 KB | 240 MHz | 1 cycle | ISR handlers, panic handlers, critical code |
| **PSRAM (Octal SPI)** | 8 MB | Up to 80 MHz (ESP-IDF default ~40 MHz) | 10–100 cycles | Bulk data: JSON, HTTP buffers, logs, ramps |
| **Flash** | 16 MB (off-package) | ~80 MHz (QSPI) | 100+ cycles | Code, read-only data, NVS |

### 1.2 PSRAM Capabilities and Constraints

**What PSRAM gives us:**
- Massive capacity (8 MB vs 320 KB DRAM)
- Managed by ESP-IDF heap allocator via `heap_caps_malloc(MALLOC_CAP_SPIRAM)`
- Code/rodata can be served from PSRAM via `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y` + `CONFIG_SPIRAM_RODATA=y`
- WiFi/LWIP buffers can partially reside in PSRAM (`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`, saves ~10 KB DRAM)

**ESP-IDF v6 memory capability flags (notable additions):**
- `MALLOC_CAP_SPIRAM_NO_ENC` — allocate from unencrypted PSRAM region (for sharing data with external processors).
- `MALLOC_CAP_DMA_DESC_AHB` / `MALLOC_CAP_DMA_DESC_AXI` — DMA descriptor memory with explicit bus targeting.
- `MALLOC_CAP_CACHE_ALIGNED` — allocate on cache-line boundary for DMA coherency.
- `MALLOC_CAP_SIMD` — memory suitable for SIMD vector instructions.
- These are not required by the current strategy but are available for future optimisation.

**Hard constraints:**
- `MALLOC_CAP_INTERNAL` specifically means memory safe when flash cache is disabled. PSRAM is **not** `MALLOC_CAP_INTERNAL` — it becomes inaccessible during flash writes.
- `MALLOC_CAP_DMA` does **not** categorically exclude PSRAM on ESP32-S3 (`SOC_PSRAM_DMA_CAPABLE=1`). Many ESP-IDF v6 drivers (SPI, LCD, SDMMC) correctly use `MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA`. The exception is **RMT TX**, whose internal symbol DMA buffer explicitly disables external memory (`access_ext_mem = false`). For general-purpose DMA, check the driver's capability mask before assuming DRAM.
- DMA descriptors cannot be in PSRAM [external-ram.html].
- Cache is shared between flash and PSRAM. Accessing >32 KB of PSRAM data can evict flash from cache, slowing code execution.
- Flash operations block PSRAM. During NVS writes, OTA, or any flash write, PSRAM becomes inaccessible because flash cache is disabled. ISR accessing PSRAM during flash write → crash.
- ISR must never touch PSRAM. Cache miss in ISR causes 10–100 µs jitter — unacceptable for real-time control (endstops, motor ticks).
- Task stacks should stay in DRAM. PSRAM stack access during flash write = guaranteed crash.

### 1.3 Latency vs Capacity Tradeoff

```
DRAM:  Fast (1 cycle)  + small (320 KB)  → ISR, atomics, stacks, hot-path
PSRAM: Slow (10-100 cyc) + large (8 MB)  → JSON, HTTP, logs, ramps, assets
```

---

## 2. Strategy Decision: Explicit Allocation (USE_CAPS_ALLOC)

### 2.1 The Three CONFIG_SPIRAM_USE_* Options

ESP-IDF v6 exposes a mutually exclusive Kconfig choice group for PSRAM heap integration:

| Option | Behavior | Suitability for EcoTiter |
|--------|----------|--------------------------|
| `CONFIG_SPIRAM_USE_MALLOC=y` | `malloc()` can return PSRAM based on threshold (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`). Allocations ≤ threshold prefer DRAM; > threshold prefer PSRAM. Fallback to other type if preferred unavailable. | ⚠️ Possible but risky |
| `CONFIG_SPIRAM_USE_CAPS_ALLOC=y` | `malloc()` stays in DRAM by default. PSRAM only via explicit `heap_caps_malloc(MALLOC_CAP_SPIRAM)`. | ✅ **OUR CHOICE** |
| `CONFIG_SPIRAM_USE_MEMMAP=y` | PSRAM integrated into CPU memory map, outside heap system; manual management. | ❌ Too restrictive |

### 2.2 Why USE_CAPS_ALLOC Over USE_MALLOC

`CONFIG_SPIRAM_USE_MALLOC=y` is not "all malloc goes to PSRAM" — it uses a threshold strategy with `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` (default 1024 bytes). However, for EcoTiter it creates two concrete concerns:

1. **Unpredictable ISR behavior during flash operations.** When NVS writes or OTA are in progress, PSRAM becomes inaccessible. If a `std::vector` in an ISR-called function (e.g., log formatting triggered from endstop ISR) landed in PSRAM due to threshold logic, the ISR crashes with a cache access exception.

2. **Reduced auditability.** With threshold-based allocation, code review cannot distinguish "intended PSRAM" from "accidental PSRAM" — both look like `std::vector<T> v;`. Explicit `heap_caps_malloc` or PMR allocator makes the intent visible at the call site.

**Mitigation with USE_MALLOC:** If ever needed, set `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024` to keep allocations ≤1 KB in DRAM. This reduces but does not eliminate the flash-operation risk.

### 2.3 Why USE_CAPS_ALLOC is Correct

With `CONFIG_SPIRAM_USE_CAPS_ALLOC=y`:
- `malloc()` / `new` / `std::vector` → DRAM by default. ISR-safe out of the box.
- Bulk allocations (>1 KB) must **explicitly** request PSRAM via PMR allocator or `heap_caps_malloc`. Intent is visible in code.
- ESP-IDF internal drivers (WiFi, BLE, HTTP) work as designed — no surprises from redirected allocations.
- Audit is trivial: `rg "MALLOC_CAP_SPIRAM|psram_resource()"` finds all PSRAM usage.

### 2.4 Strategy Decision

`CONFIG_SPIRAM_USE_CAPS_ALLOC=y` is **mandatory**. All bulk allocations go through the project's PMR allocator (`ecotiter::memory::psram_resource()`) or explicit `heap_caps_malloc(MALLOC_CAP_SPIRAM)`. Naked `new` / `malloc()` for bulk data is **FORBIDDEN**.

---

## 3. Mandatory sdkconfig.defaults Configuration

```ini
# === PSRAM Core ===
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y                    # Octal SPI (8 MB PSRAM)
CONFIG_SPIRAM_USE_CAPS_ALLOC=y              # Explicit allocation (NOT USE_MALLOC)
# CONFIG_SPIRAM_USE_MALLOC is not set       # (implicit — choice group)
# CONFIG_SPIRAM_USE_MEMMAP is not set       # (implicit — choice group)

# === PSRAM Optimizations ===
# [INVESTIGATION] — enable after stability verification:
# CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y        # Code fetch via PSRAM cache
# CONFIG_SPIRAM_RODATA=y                    # Read-only data in PSRAM
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y      # WiFi/LWIP buffers in PSRAM (~10 KB DRAM saved)
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024    # Project policy (IDF default is 16384). No-op with USE_CAPS_ALLOC, defensive if switching strategy later.

# === PSRAM Safety ===
CONFIG_SPIRAM_BOOT_INIT=y                   # Init PSRAM at boot
CONFIG_SPIRAM_IGNORE_NOTFOUND=n             # Panic if PSRAM missing (hardware fault)
CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=n  # BSS stays in DRAM — ISR globals (gStopFull, etc.) must not land in PSRAM
# CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=4096  # Reserve 4 KB pool for MALLOC_CAP_INTERNAL-only allocations
```

### 3.1 Verification in Generated sdkconfig

After `scripts/idf.sh build`, the generated `sdkconfig` must contain:

```
CONFIG_SPIRAM_USE_CAPS_ALLOC=y
# CONFIG_SPIRAM_USE_MALLOC is not set
# CONFIG_SPIRAM_USE_MEMMAP is not set
# CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY is not set  (=n — BSS in DRAM)
CONFIG_FREERTOS_UNICORE=n
```

If `CONFIG_SPIRAM_USE_MALLOC=y` appears, revert and check `sdkconfig.defaults`.

---

## 4. Allocation Patterns (C++23)

### 4.1 PMR Allocator for PSRAM (Recommended)

All bulk PSRAM allocations go through a dedicated `std::pmr::memory_resource`. This makes intent explicit, auditable, and testable.

**File:** `components/infrastructure/include/infrastructure/memory/psram_resource.hpp`

```cpp
#pragma once

#include <memory_resource>
#include <esp_heap_caps.h>
#include <stdexcept>
#include <new>

namespace ecotiter::memory {

/// PMR memory resource that allocates from PSRAM via heap_caps_malloc.
/// Usage:
///   std::pmr::vector<uint32_t> ramp{&psram_resource()};
///   std::pmr::string json_out{&psram_resource()};
///
/// Note: ESP-IDF disables C++ exceptions by default, so allocation failure
/// calls std::abort() instead of throwing std::bad_alloc.
class PsramResource : public std::pmr::memory_resource {
protected:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        void* ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!ptr) {
            std::abort();  // ESP-IDF: exceptions disabled
        }
        return ptr;
    }

    void do_deallocate(void* p, std::size_t, std::size_t) noexcept override {
        heap_caps_free(p);
    }

    bool do_is_equal(const memory_resource& other) const noexcept override {
        return this == &other;
    }
};

/// Global singleton PSRAM resource.
inline PsramResource& psram_resource() {
    static PsramResource instance;
    return instance;
}

} // namespace ecotiter::memory
```

**Usage in net_owner or log_worker:**

```cpp
#include "infrastructure/memory/psram_resource.hpp"
#include <nlohmann/json.hpp>

namespace pmr = std::pmr;
using PsramString = pmr::string;
using PsramVector = pmr::vector<uint8_t>;

void handle_api_status(httpd_req_t* req) {
    nlohmann::json status_obj = build_status_object();
    auto dumped = status_obj.dump();

    // JSON output lives in PSRAM — does not consume DRAM
    PsramString json_out(dumped.begin(), dumped.end(),
                         &ecotiter::memory::psram_resource());

    httpd_resp_sendstr_chunk(req, json_out.c_str());
    httpd_resp_sendstr_chunk(req, nullptr);  // end response
}
```

### 4.2 RAII Wrapper for One-Shot PSRAM Buffers

For temporary HTTP response buffers or staging areas, use RAII to prevent leaks.

**File:** `components/infrastructure/include/infrastructure/memory/psram_buffer.hpp`

```cpp
#pragma once

#include <esp_heap_caps.h>
#include <stdexcept>
#include <cstddef>
#include <cstdint>
#include <new>

namespace ecotiter::memory {

/// RAII wrapper for PSRAM-allocated buffer.
/// Usage:
///   PsramBuffer<8192> http_response;
///   std::snprintf(reinterpret_cast<char*>(http_response.data()), ...);
///   httpd_resp_sendstr_chunk(req, reinterpret_cast<char*>(http_response.data()));
template <size_t N>
class PsramBuffer {
    uint8_t* data_ = nullptr;

public:
    PsramBuffer() {
        data_ = static_cast<uint8_t*>(
            heap_caps_malloc(N, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!data_) {
            std::abort();  // ESP-IDF: exceptions disabled
        }
    }

    ~PsramBuffer() noexcept {
        if (data_) {
            heap_caps_free(data_);
        }
    }

    PsramBuffer(const PsramBuffer&) = delete;
    PsramBuffer& operator=(const PsramBuffer&) = delete;

    PsramBuffer(PsramBuffer&& other) noexcept : data_(other.data_) {
        other.data_ = nullptr;
    }

    PsramBuffer& operator=(PsramBuffer&& other) noexcept {
        if (this != &other) {
            if (data_) heap_caps_free(data_);
            data_ = other.data_;
            other.data_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] uint8_t* data() noexcept { return data_; }
    [[nodiscard]] const uint8_t* data() const noexcept { return data_; }
    [[nodiscard]] constexpr size_t size() const noexcept { return N; }
};

} // namespace ecotiter::memory
```

### 4.3 computeRamp() with PSRAM Allocator

Motion profiles generate large vectors of step intervals — ideal PSRAM candidate.

**File:** `components/domain/include/domain/motion.hpp`

```cpp
#pragma once

#include <cstdint>
#include <memory_resource>
#include <vector>

namespace ecotiter::domain {

struct RampConfig {
    uint32_t accelSteps;
    uint32_t decelSteps;
    uint32_t minIntervalUs;  // Full speed (shortest interval)
    uint32_t maxIntervalUs;  // Start/stop (longest interval)
};

/// Compute trapezoidal motion profile.
/// Returns vector of per-step intervals in microseconds.
/// Accepts a PMR resource for PSRAM-backed allocation.
/// Default resource is get_default_resource() — callers in infrastructure/
/// pass &ecotiter::memory::psram_resource() explicitly to use PSRAM.
[[nodiscard]] std::pmr::vector<uint32_t> computeRamp(
    uint32_t totalSteps,
    const RampConfig& config,
    std::pmr::memory_resource* res = std::pmr::get_default_resource()
);

} // namespace ecotiter::domain
```

### 4.4 Copy-to-DRAM Pattern for RMT Transmission

RMT TX internal symbol DMA buffer must be in internal DRAM (GDMA channel configured `access_ext_mem = false`). The user payload (raw data passed to the encoder) may reside in PSRAM, but cache coherency during flash writes makes it unsafe for ISR-driven transmission. Recommended pattern: compute in PSRAM, copy to DRAM for transmission.

```cpp
void motor_task_dispatch_move(uint32_t steps, const RampConfig& cfg) {
    // 1. Compute ramp in PSRAM (large, bulk allocation)
    auto psram_ramp = ecotiter::domain::computeRamp(
        steps, cfg, &ecotiter::memory::psram_resource());

    // 2. Copy to DRAM for RMT (DMA requirement)
    std::vector<uint32_t> dram_ramp(psram_ramp.begin(), psram_ramp.end());
    // dram_ramp uses default allocator → DRAM

    // 3. Transmit via RMT
    auto result = stepper.move_steps_intervals(dram_ramp, &stop_flag_);
    // psram_ramp destructor frees PSRAM automatically
}
```

### 4.5 LogBuffer in PSRAM

Ring buffer with 1000+ entries in PSRAM. Uses a static `init()` method
for dependency injection — `domain/` layer cannot depend on `infrastructure/` headers.

**File:** `components/domain/include/domain/log_buffer.hpp`

```cpp
#pragma once

#include <cstdint>
#include <cstring>
#include <atomic>
#include <memory_resource>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace ecotiter::domain {

struct LogEntry {
    uint32_t timestampMs;
    char level[8];
    char message[128];
};

class LogBuffer {
public:
    static constexpr size_t MAX_MSG_LEN = 128;
    static constexpr size_t LOG_QUEUE_LENGTH = 16;

    using Callback = void(*)(const LogEntry& entry);

    /// Initialize the singleton with PSRAM backing.
    /// Must be called before first push() — fail to init = push() is no-op.
    static void init(size_t capacity, std::pmr::memory_resource* res);

    static LogBuffer& instance();

    void push(uint32_t timestampMs, const char* level, const char* message);
    void clear();
    void setCallback(Callback cb);

    [[nodiscard]] size_t fetch(LogEntry* out, size_t maxCount,
                                const char* levelFilter = nullptr) const;

    static void workerTaskEntry(void* pvParameters);

private:
    LogBuffer() = default;

    struct Slot {
        std::atomic<uint32_t> timestampMs{0};
        char level[8]{};
        char message[MAX_MSG_LEN]{};
    };

    std::pmr::vector<Slot> slots_;       // PSRAM-backed (after init)
    size_t capacity_ = 0;
    std::atomic<size_t> head_{0};
    std::atomic<bool> pushing_{false};
    Callback callback_{nullptr};
    QueueHandle_t queue_{nullptr};
    bool initialized_ = false;
};

} // namespace ecotiter::domain
```

**Initialization in** `main.cpp` (before log hook):

```cpp
#include "infrastructure/memory/psram_resource.hpp"

void app_main() {
    // ...
    ecotiter::domain::LogBuffer::init(
        1000, &ecotiter::memory::psram_resource());

    esp_log_set_vprintf(logVprintf);  // install log hook (uses PSRAM-backed buffer)
    // ...
}
```

---

## 5. Decision Matrix

### 5.1 Green Zone: Mandatory PSRAM (> 1 KB, non-ISR, non-hot-path)

| Data Type | Typical Size | Owner Task | Pattern |
|---|---|---|---|
| `nlohmann::json::dump()` output | 512 B – 8 KB | net_owner, log_worker | `PsramString` via PMR |
| `nlohmann::json::parse()` result | 1 KB – 16 KB | net_owner | `pmr::map` / `pmr::string` |
| HTTP REST response buffers | 1 KB – 8 KB | net_owner (HTTP handler) | `PsramBuffer<8192>` |
| `computeRamp()` output | 2 KB – 32 KB | motor task setup | PMR, then copy to DRAM for RMT |
| LogBuffer ring storage (1000+ entries) | 16 KB – 64 KB | log_worker | `pmr::vector<LogEntry>` |
| WebUI static assets (HTML/CSS/JS) | 32 KB – 512 KB | net_owner (HTTP handler) | `PsramBuffer` per asset |
| Core dump staging buffers | 16 KB – 64 KB | panic handler context | `PsramBuffer` |
| WiFi/LWIP buffers (auto) | 8 KB – 12 KB | WiFi task (auto) | Via `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` |

### 5.2 Red Zone: Internal DRAM Only (Forbidden in PSRAM)

| Data Type | Size | Rationale |
|---|---|---|
| Task stacks | 8 KB – 32 KB | PSRAM access during flash write = crash. FreeRTOS default (DRAM) is correct. |
| ISR-accessed data | 1 B – 256 B | Endstop flags, tick counters, `MotorState` atomics. Cache miss in ISR → 10–100 µs jitter → missed pulses → IWDT reset. |
| RMT TX internal DMA buffer | 512 B – 2 KB | GDMA channel configured `access_ext_mem = false`. Must be in internal DRAM. |
| IRAM code | 1 KB – 16 KB | ISR handlers, panic handlers, critical sections. |
| ESP-IDF driver handles | variable | Allocated by ESP-IDF itself; do not intervene. |
| Endstop flags (`std::atomic<bool>`) | 1 B | Read by GPIO ISR on pins 7, 15. |
| Motor position atomics (`std::atomic<int32_t>`) | 4 B | Read by motor task tick ISR. |

### 5.3 Yellow Zone: Context-Dependent

| Data Type | Decision Rule |
|---|---|
| `std::string` in init code | DRAM OK — init is not hot-path, single allocation |
| `std::vector` in config-change | DRAM OK — rare operation, short-lived |
| `std::array<uint8_t, 4096>` in task | DRAM if task stack ≥ 16 KB; PSRAM via `PsramBuffer` otherwise |
| JSON in main loop | DRAM for small status (< 512 B); PSRAM for large dumps |
| WiFi/LWIP internal buffers | Auto-managed by `TRY_ALLOCATE_WIFI_LWIP`; do not override |

---

## 6. Anti-Patterns (Auto-Revert)

### 6.1 Implicit PSRAM via USE_MALLOC

```cpp
// ❌ FORBIDDEN — CONFIG_SPIRAM_USE_MALLOC=y in sdkconfig.defaults
// Threshold-based allocation may accidentally place vectors in PSRAM
std::vector<uint32_t> intervals;  // Accidentally in PSRAM → ISR-accessible data crash during flash write
```

**Fix:** Use `CONFIG_SPIRAM_USE_CAPS_ALLOC=y` and explicit PMR allocator.

### 6.2 Naked heap_caps_malloc Scattered in Business Logic

```cpp
// ❌ FORBIDDEN — leak risk on early-return paths
void handle_request(httpd_req_t* req) {
    auto* buf = (char*)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    if (!fill_buffer(buf)) return;  // LEAK: forgot heap_caps_free
    httpd_resp_sendstr(req, buf);
    heap_caps_free(buf);
}
```

**Fix:** Use RAII wrapper (`PsramBuffer<4096>`) or PMR container.

### 6.3 PSRAM for ISR-Accessed Data

```cpp
// ❌ FORBIDDEN — ISR reads PSRAM → cache miss → 100 µs jitter
std::atomic<bool>* endstop_flag =
    static_cast<std::atomic<bool>*>(
        heap_caps_malloc(sizeof(std::atomic<bool>), MALLOC_CAP_SPIRAM));
void gpio_isr_handler(void* arg) {
    if (endstop_flag->load()) {  // 100 µs latency!
        emergency_stop();
    }
}
```

**Fix:** Allocate in DRAM: `new std::atomic<bool>(false)` (DRAM by default with USE_CAPS_ALLOC).

### 6.4 Task Stacks in PSRAM

```cpp
// ❌ FORBIDDEN — CONFIG_FREERTOS_PLACE_TASK_STACKS_IN_EXT_RAM=y
// Stack in PSRAM → crash during NVS write or OTA (flash cache disabled)
xTaskCreate(motor_task, "motor", 16384, ...);  // stack in PSRAM
```

**Fix:** Ensure `CONFIG_FREERTOS_PLACE_TASK_STACKS_IN_EXT_RAM=n` (IDF default is correct). Note: `CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM` defaults to `y` on ESP32-S3 in IDF v6 — this only enables `xTaskCreateStatic` with explicit PSRAM stack, not automatic placement. The critical guard is `PLACE_TASK_STACKS_IN_EXT_RAM=n`.

### 6.5 RMT TX Data in PSRAM Without Copy

```cpp
// ❌ FORBIDDEN — RMT TX DMA channel requires internal DRAM for its symbol buffer.
// User payload (ramp.data()) may *technically* be in PSRAM since the encoder
// copies into the internal TX buffer, but during flash writes PSRAM is
// inaccessible → cache miss in ISR → corruption or crash.
std::pmr::vector<uint32_t> ramp{&psram_resource()};
rmt_transmit(channel, encoder, ramp.data(), ramp.size() * sizeof(uint32_t), &tx_cfg);
```

**Fix:** Copy to DRAM before transmission (see §4.4). This ensures cache-safe access even during flash operations.

### 6.6 PSRAM Access During Flash Operations

```cpp
// ❌ FORBIDDEN — NVS write disables flash cache, PSRAM inaccessible
void isr_called_during_nvs_write(void* arg) {
    auto* entry = static_cast<LogEntry*>(arg);  // points to PSRAM
    LogBuffer::push(*entry);  // CRASH: PSRAM access with flash cache disabled
}
```

**Fix:** ISR-context data must always be in DRAM. Never pass PSRAM pointers to ISR handlers.

### 6.7 >32 KB Sequential PSRAM Reads in Hot-Path

```cpp
// ❌ FORBIDDEN — evicts flash from cache, slows code execution
void hot_path() {
    for (size_t i = 0; i < psram_buffer.size(); ++i) {
        process(psram_buffer[i]);  // sequential 64 KB read evicts flash cache
    }
}
```

**Fix:** For >32 KB sequential reads, copy to DRAM first or break into chunks.

---

## 7. Runtime Verification

### 7.1 Boot-Time Checks

After boot, serial log must show:

```
I (xxx) esp_psram: Found 8MB PSRAM device
I (xxx) heap_init: Initializing. RAM available for dynamic allocation:
```

If PSRAM init fails, the chip should panic (hardware fault, `CONFIG_SPIRAM_IGNORE_NOTFOUND=n`).

### 7.2 Post-Init Heap State

After full init (WiFi + HTTP + BLE), query heap state via `diag::print_heap_stats()`:

**File:** `components/diag/include/diag/heap_monitor.hpp`

```cpp
#pragma once

#include <cstdio>
#include "esp_heap_caps.h"
#include "esp_log.h"

namespace ecotiter::diag {

inline void print_heap_stats() {
    auto psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    auto psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    auto dram_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    auto dram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

    ESP_LOGI("heap", "PSRAM free=%lu largest=%lu | DRAM free=%lu largest=%lu",
             (unsigned long)psram_free, (unsigned long)psram_largest,
             (unsigned long)dram_free, (unsigned long)dram_largest);

    // Warning thresholds per §7.2
    if (psram_free < 4 * 1024 * 1024) {
        ESP_LOGW("heap", "CRITICAL: PSRAM free < 4 MB (%lu)", (unsigned long)psram_free);
    }
    if (psram_largest < 256 * 1024) {
        ESP_LOGW("heap", "CRITICAL: PSRAM largest block < 256 KB (%lu)", (unsigned long)psram_largest);
    }
    if (dram_free < 20 * 1024) {
        ESP_LOGW("heap", "CRITICAL: DRAM free < 20 KB (%lu)", (unsigned long)dram_free);
    }
    if (dram_largest < 4 * 1024) {
        ESP_LOGW("heap", "CRITICAL: DRAM largest block < 4 KB (%lu)", (unsigned long)dram_largest);
    }
}

} // namespace ecotiter::diag
```

Expected values after full init:

| Metric | Expected | Failure Threshold |
|--------|----------|-------------------|
| PSRAM free | > 6 MB | < 4 MB (leak or misconfiguration) |
| PSRAM largest block | > 1 MB | < 256 KB (fragmentation) |
| DRAM free | > 40 KB | < 20 KB (DRAM Triangle violation — see project.md §Init Order) |
| DRAM largest block | > 8 KB | < 4 KB (HTTP/BLE alloc will fail, see LL-004) |

### 7.3 Continuous Monitoring

Periodic heap logging in `main.cpp` (every 60 s). Uses `diag::print_heap_stats()`
declared in `components/diag/include/diag/heap_monitor.hpp`:

```cpp
#include "diag/heap_monitor.hpp"

void app_main() {
    // ... init ...

    // Boot-time check after full init
    ecotiter::diag::print_heap_stats();

    TickType_t last_heap_log = xTaskGetTickCount();
    while (true) {
        TickType_t now = xTaskGetTickCount();
        if (now - last_heap_log > pdMS_TO_TICKS(60000)) {
            ecotiter::diag::print_heap_stats();
            last_heap_log = now;
        }

        // ... main loop work ...
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));
    }
}
```

### 7.4 Heap Tracing (Development Only)

For diagnosing memory leaks during development:

```ini
# sdkconfig.defaults (dev only, disable in production)
CONFIG_HEAP_TRACING_STANDALONE=y
CONFIG_HEAP_TRACING_DESTINATION_FILES=y
```

```cpp
#include <esp_heap_trace.h>

static heap_trace_record_t trace_record[128];
heap_trace_init_standalone(trace_record, sizeof(trace_record) / sizeof(trace_record[0]));

heap_trace_start(HEAP_TRACE_LEAKS);
// ... code under test ...
heap_trace_stop();
heap_trace_dump();  // prints all allocations not freed
```
