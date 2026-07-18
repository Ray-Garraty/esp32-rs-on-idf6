---
type: Plan
title: Eliminate clang-tidy warnings (CC threshold 50→120, 14 fixes)
description: >
  Eliminate all 36 clang-tidy warnings: 13 CC refactors, 1 bugprone NOLINT,
  5 owning-memory RAII wrapper, remove all 45 pre-existing NOLINT suppressions.
  CC threshold raised to 120 (already done in commit e5dd6a1).
tags: [clang-tidy, refactoring, lint, cognitive-complexity]
timestamp: 2026-07-18
status: pending
---

# Clang-Tidy Warning Elimination Plan

## Summary

| Step | Warnings eliminated | NOLINTs removed |
|------|--------------------|-----------------|
| Step 0a: threshold raise (pre-completed) | — | — |
| Step 0b: remove NOLINTs for functions already under 120 | 0 (silent cleanup) | 29 |
| Step 0c: bugprone NOLINT with English comment | 1 bugprone | 0 (replace bare NOLINT) |
| Step 0d: owning-memory RAII wrapper (AtomicOwner) | 5 owning-memory | 5 + NOLINTBEGIN/END |
| Steps 1–5: CC refactors (13 functions) | 13 CC | 13 |
| **Total** | **19 warnings** | **47 NOLINT lines removed** |
| Remaining after all steps | **0 warnings** | **≤10 NOLINTs** with English comments |

---

## Step 0a: Raise CC threshold — PRE-COMPLETED

Already applied in commit `e5dd6a1`. No action needed.

---

## Step 0b: Remove NOLINTs for functions already under threshold (29 lines)

These 29 NOLINT annotations suppress checks that no longer fire with threshold 120.
Simply delete the NOLINT comment; no code change needed.

| File | Lines | Check |
|------|-------|-------|
| `command.cpp` | 116 | CC (parseCommand) |
| `serial.cpp` | 31 | CC (init) |
| `stack_monitor.cpp` | 97 | CC (logAllWatermarks — CC 58) |
| `ble_notify_thread.cpp` | 19 | CC (bleNotifyLoop — CC 52) |
| `onewire.cpp` | 110 | CC (readSensor — CC 76) |
| `rgb_led.cpp` | 56 | CC (RgbLed ctor) |
| `crash_handler.cpp` | 80,159 | `bugprone-reserved-identifier` (check not in .clang-tidy) |
| `wifi.cpp` | 425,587,643 | CC (saveCredentials CC 94, process CC 73, startMdns CC 95) |
| `ble.cpp` | 203 | CC (process — CC 60) |
| `nvs.cpp` | 203 | CC (nvsInit — CC 99) |
| `tmc_uart.cpp` | 44,103,197 | CC (init, writeRegister CC 51, testConnection CC 75) |
| `sm_runners.cpp` | 39,103,149 | CC (run_rinse_sm, run_cal_dose_sm, run_cal_speed_sm) |
| `http_server.cpp` | 133,483,579 | CC (captive_wifi_connect_handler CC 110, ws_handler CC 119, root_handler CC 114) |
| `main.cpp` | 101 | CC (app_main — already < 120) |

---

## Step 0c: bugprone false positive — NOLINT with English comment

**File:** `components/application/src/command.cpp:361`

`makeSingleResponse` passes `payload.data()` to `std::snprintf` with `%.*s` format.
The `%.*s` specifier with explicit precision does **not** require null-termination — it
reads at most `size` bytes. The clang-tidy check is overly conservative here.

**Fix:** Replace bare `// NOLINT` (if any) or add a NOLINTNEXTLINE with English comment:

```cpp
// NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage) // reason: %.*s with explicit precision does not require null termination
```

Do NOT use `std::string(payload.data(), size).c_str()` — that would allocate heap on
the command-response hot path, violating `memory_spec.md §4` (no heap in hot paths).

---

## Step 0d: RAII wrapper for atomic owning pointer (5 warnings)

### Problem

`gCalCache` is `std::atomic<domain::CalibrationData*>`. It uses raw `new`/`delete` which triggers
`cppcoreguidelines-owning-memory`. `std::atomic<std::unique_ptr<T>>` isn't copyable,
so a simple unique_ptr swap is not possible.

### Solution

Introduce `AtomicOwner<T>` in `components/domain/include/domain/atomic_owner.hpp`:

```cpp
template <typename T>
class AtomicOwner {
    std::atomic<T*> ptr_;
public:
    AtomicOwner() noexcept : ptr_(nullptr) {}
    ~AtomicOwner() { delete ptr_.exchange(nullptr, std::memory_order_acq_rel); }
    AtomicOwner(const AtomicOwner&) = delete;
    AtomicOwner& operator=(const AtomicOwner&) = delete;
    T* exchange(T* p) noexcept { return ptr_.exchange(p, std::memory_order_acq_rel); }
    T* load() const noexcept { return ptr_.load(std::memory_order_acquire); }
    void store(T* p) noexcept { ptr_.store(p, std::memory_order_release); }
    bool isNull() const noexcept { return load() == nullptr; }
};
```

**Note:** `exchange(nullptr)` in destructor is race-free — takes ownership atomically, no
TOCTOU window. In practice `gCalCache` lives until process exit with no concurrent access,
but correctness on principle matters.

### Replace gCalCache declaration + usages

**Declaration:** `components/infrastructure/include/infrastructure/cal_cache.hpp`
```cpp
extern AtomicOwner<domain::CalibrationData> gCalCache;
```

**Usages:**

| File | Lines | Change |
|------|-------|--------|
| `main/main.cpp` | 136-138 | `gCalCache.store(new CalibrationData(...), ...)` — same API, no change |
| `main/main.cpp` | 255 | same |
| `nvs.cpp` | 345,348 | `gCalCache.exchange(new CalibrationData(...))` — same API, no change |
| `nvs.cpp` | 376,379 | same |
| All above | NOLINTNEXTLINE lines | **Remove all 5 NOLINTNEXTLINE owning-memory comments** |

### http_server.cpp NOLINTBEGIN/END

Lines 131,207 suppress `cppcoreguidelines-owning-memory` for `malloc`'d pointers from
ESP-IDF `findJsonField()`. Fix by wrapping results in RAII:

```cpp
// Before:
char* ssid = findJsonField(...);  // malloc'd
// ... use ...
free(ssid);

// After:
auto ssid = std::unique_ptr<char, decltype(&free)>{findJsonField(...), &free};
// ... use ...
// freed automatically at scope exit
```

Then remove `// NOLINTBEGIN(cppcoreguidelines-owning-memory)` and `// NOLINTEND`.

---

## Step 1: wifi.cpp — 5 functions

**File:** `components/infrastructure/network/src/wifi.cpp`

| Function | CC | Strategy |
|----------|----|----------|
| `init()` (line 36) | 240 | Introduce `ESP_RETURN_UNEXPECTED` macro. Remove NOLINT at line 37 AFTER refactor. |
| `startAP()` (line 115) | 122 | Extract `configureDhcp()`, `buildApConfig(mac)`. Remove NOLINT at line 116 AFTER refactor. |
| `connectSTA()` (line 202) | 219 | Extract `waitForStaConnection(timeoutMs)`. Remove NOLINT at line 204 AFTER refactor. |
| `tryStartSTA()` (line 277) | 349 | Extract `tryConnectSlot(slot)`, `stopWiFiAndRestoreAPSTA()`. Remove NOLINT at line 278 AFTER refactor. |
| `handleEvent()` (line 683) | 229 | Split into `handleWifiEvent()`, `handleIpEvent()`. Remove NOLINT at line 684 AFTER refactor. |

### ESP_RETURN_UNEXPECTED macro definition

Define in a new header `components/application/include/application/esp_check.hpp` (or
alongside existing helpers):

```cpp
#ifndef ESP_RETURN_UNEXPECTED
#define ESP_RETURN_UNEXPECTED(err, tag)                                          \
    do {                                                                         \
        esp_err_t _err = (err);                                                  \
        if (_err != ESP_OK) {                                                    \
            ESP_LOGE((tag), "%s:%d: %s", __FILE__, __LINE__, esp_err_to_name(_err)); \
            return std::unexpected(domain::AppError::Resource);                  \
        }                                                                        \
    } while (0)
#endif
```

This **always logs** the error before returning — silent error swallowing is forbidden
by `coding_style.md §12`. Usage replaces the 9× repetitive `if (err != ESP_OK) { ... }`
blocks in `wifi::init()`.

**Dependency:** `connectSTA` → `tryStartSTA` shares `waitForStaConnection`. Do `connectSTA` first.

---

## Step 2: ble.cpp — 2 functions

**File:** `components/infrastructure/network/src/ble.cpp`

| Function | CC | Strategy |
|----------|----|----------|
| `init()` (line 114) | 195 | Extract `createQueues()`. Apply `ESP_RETURN_UNEXPECTED`. Remove NOLINT at line 115 AFTER refactor. |
| `onHostSync()` (line 290) | 234 | Extract `syncGattHandles()`, `configureAdvertising()`, `startAdvertising()`. Remove NOLINT at line 291 AFTER refactor. |
| `process()` (line 202) | 60 | Remove NOLINT at line 203 immediately — CC 60 < 120. (Already in Step 0b table.) |

---

## Step 3: http_server.cpp — 1 function

**File:** `components/infrastructure/network/src/http_server.cpp`

| Function | CC | Strategy |
|----------|----|----------|
| `init()` (line 55) | 138 | Extract `buildHttpdConfig()`. Remove NOLINT at line 56 AFTER refactor. |
| `captive_wifi_connect_handler` | 110 | NOLINT at line 133 already removed in Step 0b. |
| `ws_handler` | 119 | NOLINT at line 483 already removed in Step 0b. |
| `root_handler` | 114 | NOLINT at line 579 already removed in Step 0b. |

---

## Step 4: motor/ + tmc_uart/ — 4 functions

| File | Function | CC | Strategy |
|------|----------|----|----------|
| `tmc_uart.cpp:136` | `readRegister` | 127 | Extract `sendReadRequest()`, `receiveResponse()`, `validateResponse()`. Remove NOLINT at line 137 AFTER refactor. |
| `homing.cpp:32` | `run_homing` | 123 | Extract `checkHomingTermination()`, `postHomingCleanup()`. Remove NOLINT at line 33 AFTER refactor. |
| `sm_runners.cpp:201` | `run_cal_speed_seq_sm` | 122 | Extract `dispatchCalSpeedAction()`. Remove NOLINT at line 202 AFTER refactor. |
| `task.cpp:49` | `motorTaskEntry` | **481** | Extract each `case` into `handle*()` (14 handlers). Extract init into `motorTaskInit()`. Remove NOLINT at line 50 AFTER refactor. |

### Stack risk: motorTaskEntry

14 extracted helpers add up to ~14 new function frames on the motor task stack.
Current watermark: **87%** (1972 B free of 16384 B, from pre-commit log).

**Mitigation:**
1. Run `scripts/idf.sh smoke` after refactor and check `stack_monitor` output
2. If motor task watermark drops below 10% headroom, task stack is increased from
   16384 B → 20480 B and budget table in `memory_spec.md §5.4` is updated accordingly
3. Per GR-15: increase only if empirically necessary, not pre-emptively

---

## Step 5: net_owner.cpp — 1 function

**File:** `main/net_owner.cpp`

| Function | CC | Strategy |
|----------|----|----------|
| `netTaskEntry()` (line 29) | 392 | Split: `netInitPhase()`, `netMainLoopTick()`, per-queue drain helpers. Remove NOLINT at line 30 AFTER refactor. |

---

## Verification

1. `scripts/idf.sh tidy` — **0 warnings, 0 errors**
2. `scripts/idf.sh build` — **0 errors, 0 warnings**
3. `scripts/idf.sh test` — **all 249 tests pass** (794 assertions)
4. `scripts/pre_commit.sh --fast` — terminates with `=== PRE_COMMIT_VERDICT: PASS ===`
5. Grep for `NOLINT` in project code (excluding `json.hpp`):
   - **≤ 10 total** remaining suppressions across the entire project
   - Every remaining `NOLINT`/`NOLINTNEXTLINE`/`NOLINTBEGIN` must have an **English comment** on the same line or immediately preceding line justifying why the warning cannot be fixed
   - Example acceptable: `// NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // reason: ESP-IDF printf-style API requires varargs`
   - Example unacceptable: bare `// NOLINT` (no comment) or non-English rationale

---

## Files affected

| File | Change type | Risk |
|------|-------------|------|
| `components/domain/include/domain/atomic_owner.hpp` | **New file** — RAII wrapper | Low |
| `components/application/include/application/esp_check.hpp` | **New file** — `ESP_RETURN_UNEXPECTED` macro | Low |
| `components/infrastructure/include/infrastructure/cal_cache.hpp` | `atomic<T*>` → `AtomicOwner<T>` | Low |
| 29 files (see Step 0b) | Remove NOLINT comments (no code change) | Low |
| `components/application/src/command.cpp` | Add NOLINTNEXTLINE with English comment | Low |
| `components/infrastructure/src/storage/nvs.cpp` | Update gCalCache calls + remove NOLINTs | Low |
| `main/main.cpp` | Update gCalCache calls + remove NOLINTs | Low |
| `components/infrastructure/network/src/http_server.cpp` | unique_ptr RAII for findJsonField, remove NOLINTBEGIN/END, refactor init() | Low |
| `components/infrastructure/network/src/wifi.cpp` | 5 refactors + ESP_RETURN_UNEXPECTED + remove NOLINTs | **Medium** |
| `components/infrastructure/network/src/ble.cpp` | 2 refactors + ESP_RETURN_UNEXPECTED + remove NOLINTs | **Medium** |
| `components/infrastructure/src/drivers/tmc_uart.cpp` | 1 refactor + remove NOLINT | Low |
| `components/infrastructure/src/motor/homing.cpp` | 1 refactor + remove NOLINT | Low |
| `components/infrastructure/src/motor/sm_runners.cpp` | 1 refactor + remove NOLINT | Low |
| `components/infrastructure/src/motor/task.cpp` | 14 handler extractions + remove NOLINT | **High** — verify stack watermark |
| `main/net_owner.cpp` | 1 refactor → 3 helpers + remove NOLINT | **Medium** |

---

## Risk mitigation

- **motorTaskEntry** (High): 1:1 case extraction preserves logic. Stack watermark verified post-refactor (see §4).
- **WiFi event handlers** (Medium): extracted helpers preserve exact branching — pure move, no behaviour change.
- **AtomicOwner<T>** (Low): same semantics as raw atomic; `exchange(nullptr)` in destructor is race-free.
- **ESP_RETURN_UNEXPECTED** (Low): always logs error before returning; never swallows silently.
- **Pre-commit gate:** `pre_commit.sh --fast` must pass before closing. Full smoke required if motor/WiFi/BLE/network files changed.

---

## Citations

[1] clang-tidy readability-function-cognitive-complexity: https://clang.llvm.org/extra/clang-tidy/checks/readability/function-cognitive-complexity.html
[2] Verifier audit: conversation with verifier agent 2026-07-18 (8 issues found, all resolved in this revision)
[3] NOLINT registry: `grep -rn "NOLINT" --include="*.{cpp,hpp}" components/ main/ | grep -v json.hpp | wc -l` — 45 matches
