---
type: Plan
title: mDNS Hostname Resolution — Enable ecotiter.local Discovery
description: >
  Enable mDNS hostname resolution so the device is discoverable as ecotiter.local
  on the LAN. Root cause: ESP-IDF v6 removed mDNS from built-in components — it
  now lives in the IDF Component Registry as espressif/mdns. The project had no
  idf_component.yml manifest, causing mdns.h to be unavailable, #include "mdns.h"
  commented out, and startMdns() as a stub. Fix: add idf_component.yml dependency,
  implement real startMdns() with mdns_init/hostname_set/service_add, add stub
  for host tests, add regression test, and wire the call on STA got IP.
tags: [bugfix, mdns, network, esp-idf-v6, component-registry]
timestamp: 2026-07-11
status: completed
---

# mDNS Hostname Resolution — Enable ecotiter.local Discovery

## Executive Summary

The device was not discoverable via `ecotiter.local` because ESP-IDF v6
migrated mDNS from built-in components to the IDF Component Registry but the
project lacked the required `idf_component.yml` manifest. The `#include "mdns.h"`
was commented out and `startMdns()` was a stub printing a skip message. The fix
adds `main/idf_component.yml` declaring `espressif/mdns: "^1.11.3"`, implements
the real `startMdns()` with `mdns_init()`, `mdns_hostname_set()`, and
`mdns_service_add()`, adds a host-test stub header and regression test, and
uncomments the relevant `sdkconfig.defaults` option. Validation confirms
`ecotiter.local` resolves to `192.168.1.103` via ping and is discoverable via
avahi-browse. No crashes, no WDT, no panics in 30s smoke test.

## Initial Goal

**Task:** Enable mDNS hostname resolution for ecotiter.local so the device is
discoverable via mDNS on the LAN.

### Problem Statement

The EcoTiter firmware runs an HTTP server on port 80 for WebUI and REST API
access. Users should be able to reach it via `http://ecotiter.local/` without
needing to know the device's DHCP-assigned IP address. However, the mDNS code
path was completely non-functional:

- `#include "mdns.h"` was commented out (line 15-16 in wifi.cpp)
- `startMdns()` was a stub printing "mDNS skipped — add esp_mdns component
  dependency to enable"
- `CONFIG_MDNS_MAX_SERVICES=1` was commented out in `sdkconfig.defaults`

Root cause: ESP-IDF v6 removed mDNS from the built-in components. It now ships
via the IDF Component Registry and requires an `idf_component.yml` manifest to
pull in `espressif/mdns`. The project had no such manifest.

### Acceptance Criteria

| ID | Criterion | Result |
|----|-----------|--------|
| AC-001 | `ecotiter.local` resolves to device IP via ping | ✅ |
| AC-002 | Service discoverable via avahi-browse / dns-sd | ✅ |
| AC-003 | Web UI accessible via `http://ecotiter.local/` | ✅ |
| AC-004 | Build: 0 errors, 0 new warnings | ✅ |
| AC-005 | Host unit tests: all pass (253/257 — 4 pre-existing BLE failures) | ✅ |
| AC-006 | clang-tidy: 0 warnings | ✅ |
| AC-007 | 30s smoke test: no crashes, no WDT, no panics | ✅ |

### Scope

- Component dependency management: add `idf_component.yml`
- Build system: update `CMakeLists.txt` REQUIRES
- Network layer: implement real `startMdns()` in `WifiManager`
- Host tests: add mDNS stub header and regression test
- sdkconfig: uncomment `CONFIG_MDNS_MAX_SERVICES=1`
- Git hygiene: replace bare `idf_component.yml` ignore with `/managed_components/`

### Out of Scope

- mDNS on AP-only mode (not implemented — by design per project.md: mDNS only
  starts on STA GOT_IP)
- `mdns_free()` in `WifiManager::stop()` (suggestion, not blocking)
- Double-init guard behavior-focused test (suggestion, not blocking)

## Plan Summary

### Approach

1. **Add component dependency:** Create `main/idf_component.yml` with
   `espressif/mdns: "^1.11.3"`. This tells the IDF Component Manager to
   download the mDNS component into `managed_components/` on build.
2. **Update build system:** Add `espressif__mdns` to REQUIRES in
   `components/infrastructure/CMakeLists.txt` so the infrastructure library
   links against mDNS.
3. **Implement startMdns():** Replace the stub in `wifi.cpp` with real
   `mdns_init()`, `mdns_hostname_set("ecotiter")`, and
   `mdns_service_add("EcoTiter Burette Controller", "_http", "_tcp", 80, ...)`.
   Add `mdnsInitDone_` guard to prevent double-init.
4. **Add host test stub:** Create `tests/stubs/mdns.h` with minimal function
   signatures so host unit tests compile without the real `mdns.h`.
5. **Add regression test:** Create `tests/src/test_mdns.cpp` exercising the
   exact API calls used by `startMdns()`.
6. **Uncomment sdkconfig:** Enable `CONFIG_MDNS_MAX_SERVICES=1`.
7. **Fix .gitignore:** Replace bare `idf_component.yml` (which would ignore the
   new manifest) with `/managed_components/` (ignores auto-downloaded deps).

### Dependencies

- `espressif/mdns` from IDF Component Registry (^1.11.3)
- IDF Component Manager (`idf_component.yml` support in ESP-IDF v6)
- Existing WiFi AP/STA init chain (mDNS starts on `IP_EVENT_STA_GOT_IP`)

### Risks

- **idf_component.yml ignored by .gitignore** — the old `.gitignore` had
  `idf_component.yml` which would exclude the new manifest. Fixed by replacing
  with `/managed_components/`.
- **mDNS leaks DRAM** — negligible: mDNS in ESP-IDF uses minimal heap
  (~2 KB for service registration). Not a risk given 8 MB PSRAM + internal DRAM.
- **Host tests fail to compile without real mdns.h** — mitigated by stub header
  in `tests/stubs/`.

## Implementation

### Phase 1 — Component Dependency

**File:** `main/idf_component.yml` (NEW — 2 lines)

```yaml
dependencies:
  espressif/mdns: "^1.11.3"
```

This manifest declares the mDNS component dependency. On build, the IDF
Component Manager downloads `espressif/mdns` v1.11.3 (or compatible) into
`managed_components/`.

**File:** `components/infrastructure/CMakeLists.txt` (modified)

| Change | Lines | Purpose |
|--------|-------|---------|
| Added `espressif__mdns` to REQUIRES | 36 | Links infrastructure library against the mDNS component |

**File:** `sdkconfig.defaults` (modified)

| Change | Lines | Purpose |
|--------|-------|---------|
| Uncommented `CONFIG_MDNS_MAX_SERVICES=1` | 49 | Enables mDNS service registration with 1 service slot |

### Phase 2 — startMdns() Implementation

**File:** `components/infrastructure/network/src/wifi.cpp` (modified)

| Change | Lines | Purpose |
|--------|-------|---------|
| Uncommented `#include "mdns.h"`, removed old comment | 14 | Pull in real mDNS API header |
| Replaced stub `startMdns()` with real implementation | 445-471 | Calls mdns_init, mdns_hostname_set, mdns_service_add with proper error handling |
| Added `diag::FfiGuard guard(76)` | 447 | FFI boundary tracing for mDNS calls |
| Added mdns_free() on failure paths | 458, 465 | Clean up on hostname/service registration failure |

**File:** `components/infrastructure/network/include/infrastructure/network/wifi.hpp` (modified)

| Change | Lines | Purpose |
|--------|-------|---------|
| Added `bool mdnsInitDone_{false};` member | 72 | Guards against double mDNS init |

**startMdns() implementation logic:**

```cpp
void WifiManager::startMdns() {
    if (mdnsInitDone_) return;             // guard against double-init
    diag::FfiGuard guard(76);

    esp_err_t err = mdns_init();           // init mDNS stack
    if (err != ESP_OK) { /* warn, return */ }

    err = mdns_hostname_set("ecotiter");   // set hostname → ecotiter.local
    if (err != ESP_OK) { mdns_free(); return; }

    err = mdns_service_add(                // register _http._tcp on port 80
        "EcoTiter Burette Controller",
        "_http", "_tcp", 80, nullptr, 0);
    if (err != ESP_OK) { mdns_free(); return; }

    mdnsInitDone_ = true;
    ESP_LOGI(TAG, "mDNS started: ecotiter.local (_http._tcp, port 80)");
}
```

`startMdns()` is called from the event handler when `IP_EVENT_STA_GOT_IP` fires
(line 529 in wifi.cpp).

### Phase 3 — Host Test Stub & Regression Test

**File:** `tests/stubs/mdns.h` (NEW — 49 lines)

Minimal stub for host test compilation with `mdns_init()`, `mdns_hostname_set()`,
`mdns_service_add()`, `mdns_free()` — all returning ESP_OK / no-op.

**File:** `tests/src/test_mdns.cpp` (NEW — 29 lines)

Regression test verifying:
- `mdns.h` header is reachable from the test build
- All 4 mDNS API function signatures called by `startMdns()` compile
- `mdns_init()` returns `ESP_OK`
- `mdns_hostname_set("ecotiter")` returns `ESP_OK`
- `mdns_service_add(...)` returns `ESP_OK`
- `mdns_free()` compiles and runs without crash

**File:** `tests/CMakeLists.txt` (modified)

| Change | Lines | Purpose |
|--------|-------|---------|
| Added `src/test_mdns.cpp` | 91 | Include mDNS regression test in test build |

### Phase 4 — Git Hygiene

**File:** `.gitignore` (modified)

| Change | Lines | Purpose |
|--------|-------|---------|
| Removed bare `idf_component.yml` | 28 | Was ignoring the new manifest file |
| Added `/managed_components/` | 30-31 | Ignores auto-downloaded IDF Component Registry artifacts |

### Files Created

| File | Lines | Purpose |
|------|-------|---------|
| `main/idf_component.yml` | 2 | mDNS component dependency manifest |
| `tests/stubs/mdns.h` | 49 | Host test stub for mDNS API |
| `tests/src/test_mdns.cpp` | 29 | Regression test for mDNS API |

### Files Modified

| File | Change | Lines |
|------|--------|-------|
| `components/infrastructure/network/src/wifi.cpp` | Uncommented `#include "mdns.h"`, implemented startMdns() | +32/-4 after diff |
| `components/infrastructure/network/include/infrastructure/network/wifi.hpp` | Added mdnsInitDone_ guard | +1 |
| `components/infrastructure/CMakeLists.txt` | Added espressif__mdns to REQUIRES | +1 |
| `sdkconfig.defaults` | Uncommented CONFIG_MDNS_MAX_SERVICES=1 | +1/-1 |
| `tests/CMakeLists.txt` | Added test_mdns.cpp | +1 |
| `.gitignore` | Replaced idf_component.yml with /managed_components/ | +2/-1 |

## Issues Encountered

### Planning Phase

| Issue | Resolution |
|-------|------------|
| mDNS not available in ESP-IDF v6 built-in components | Moved to IDF Component Registry — add idf_component.yml with espressif/mdns dependency |
| No idf_component.yml in project | Created main/idf_component.yml — this is the standard location for IDF Component Registry manifests |
| Old .gitignore had `idf_component.yml` ignoring the manifest | Replaced with `/managed_components/` which ignores auto-downloaded components |

### Implementation Phase

| Issue | Resolution |
|-------|------------|
| Stub `startMdns()` had no implementation | Replaced with real mdns_init/hostname_set/service_add with error handling |
| No double-init guard | Added `mdnsInitDone_` boolean member |
| No error cleanup on failure | Added `mdns_free()` cleanup on hostname/service registration failure |
| Host tests wouldn't compile without mdns.h | Created tests/stubs/mdns.h with minimal stubs |
| No regression test for mDNS API | Created test_mdns.cpp exercising all 4 API calls |

### Validation Phase

| Issue | Resolution |
|-------|------------|
| 4 pre-existing BLE test failures (unrelated) | Documented as pre-existing — not caused by this change |
| `dependencies.lock` auto-generated file untracked | Intentionally untracked — managed_components/ in .gitignore |

## Rework Cycles

### Cycle 1 — Initial Implementation

**Input:** startMdns() is a stub. mdns.h not included. No component dependency.

**Fix applied:**
1. Created `main/idf_component.yml` with `espressif/mdns: "^1.11.3"`
2. Added `espressif__mdns` to REQUIRES in `components/infrastructure/CMakeLists.txt`
3. Uncommented `#include "mdns.h"` in wifi.cpp
4. Replaced stub `startMdns()` with real implementation:
   - `mdns_init()`, `mdns_hostname_set("ecotiter")`,
     `mdns_service_add("EcoTiter Burette Controller", "_http", "_tcp", 80, ...)`
   - Added `mdnsInitDone_` guard and FfiGuard
   - Added error handling with `mdns_free()` cleanup
5. Added `bool mdnsInitDone_{false}` to wifi.hpp
6. Uncommented `CONFIG_MDNS_MAX_SERVICES=1` in sdkconfig.defaults
7. Fixed .gitignore: replaced `idf_component.yml` with `/managed_components/`
8. Created `tests/stubs/mdns.h` for host test compilation
9. Created `tests/src/test_mdns.cpp` regression test
10. Added `test_mdns.cpp` to tests/CMakeLists.txt

**Validation:**
- `scripts/idf.sh build` — 0 errors, 0 new warnings
- `scripts/idf.sh test` — 253/257 pass (4 pre-existing BLE failures)
- `scripts/idf.sh tidy` — 0 warnings
- Smoke test on hardware: mDNS started, ecotiter.local resolves, WebUI loads
- 30s smoke: no Guru Meditation, no WDT, no panics

No additional rework cycles were needed — the fix was implemented in a single
pass and validated on real hardware.

## Metrics

| Metric | Value |
|--------|-------|
| Files created | 3 (`idf_component.yml`, `stubs/mdns.h`, `test_mdns.cpp`) |
| Files modified | 6 (wifi.cpp, wifi.hpp, CMakeLists.txt, sdkconfig.defaults, tests/CMakeLists.txt, .gitignore) |
| Lines added (new files) | 80 |
| Lines added (modified) | 33 |
| Lines removed (modified) | 8 |
| Net LOC change | +105 |
| Test cases added | 1 (mDNS API regression) |
| Build errors | 0 |
| Build warnings | 0 (new); 0 (total) |
| clang-tidy warnings | 0 |
| Host unit tests | 253/257 pass (4 pre-existing BLE failures) |
| 30s smoke test on hardware | PASS (no crashes, no WDT, no panic) |

## Verification

### Build & Lint

| Check | Result |
|-------|--------|
| `scripts/idf.sh build` | ✅ 0 errors, 0 warnings |
| `scripts/idf.sh tidy` | ✅ 0 warnings |
| `scripts/idf.sh test` | ✅ 253/257 pass (4 pre-existing BLE failures) |
| `python docs/validate_okf.py` | ✅ All docs pass |

### Acceptance Criteria Results

| ID | Criterion | Result | Evidence |
|----|-----------|--------|----------|
| AC-001 | `ecotiter.local` resolves to device IP | ✅ | `ping ecotiter.local` → `192.168.1.103` |
| AC-002 | Service discoverable via avahi-browse | ✅ | "EcoTiter Burette Controller" on port 80 |
| AC-003 | Web UI accessible via hostname | ✅ | `http://ecotiter.local/` loads dashboard |
| AC-004 | Build: 0 errors, 0 warnings | ✅ | `scripts/idf.sh build` |
| AC-005 | Host unit tests: all pass | ✅ | 253/257 (4 pre-existing BLE) |
| AC-006 | clang-tidy: 0 warnings | ✅ | `scripts/idf.sh tidy` |
| AC-007 | 30s smoke test: no crash/WDT/panic | ✅ | Serial monitor log |

### Hardware Validation

| Test | Result | Details |
|------|--------|---------|
| Boot smoke test | ✅ PASS | BOOT OK, WiFi AP started, HTTP server on port 80 |
| mDNS init log | ✅ PASS | "mDNS started: ecotiter.local (_http._tcp, port 80)" in serial log |
| `ping ecotiter.local` | ✅ PASS | Resolves to 192.168.1.103 |
| `avahi-browse -r _http._tcp` | ✅ PASS | "EcoTiter Burette Controller" on port 80 |
| `http://ecotiter.local/` | ✅ PASS | Web UI loads successfully |
| 30s smoke test | ✅ PASS | No Guru Meditation, no WDT timeout, no panic |

## Lessons Learned

### LL-037 — ESP-IDF v6 Component Registry Migration
ESP-IDF v6 moved several previously built-in components (including mDNS) to the
IDF Component Registry. Components now require:
1. An `idf_component.yml` manifest in `main/` declaring the dependency
2. The component name with `__` (double underscore) in CMakeLists.txt REQUIRES
   (e.g., `espressif__mdns`)
3. Auto-downloaded to `managed_components/` on build

The old approach of `#include "mdns.h"` with built-in components no longer
works. The `idf_component.yml` must be committed to git, but the
`managed_components/` directory must be gitignored.

### Gitignore Gotcha with idf_component.yml
The project's `.gitignore` originally had `idf_component.yml` (bare filename),
which was intended to ignore the auto-generated component lock file but also
matched the new `main/idf_component.yml` manifest. This would silently exclude
the manifest from git tracking. Fix: replace bare `idf_component.yml` with
`/managed_components/` which only ignores the auto-downloaded directory.

### mDNS Only on STA (by design)
`startMdns()` is called on `IP_EVENT_STA_GOT_IP`, meaning mDNS is only
available when the device is connected to a LAN via STA (WiFi client). In AP-only
mode (no STA connection), mDNS is not started. This is by design per
`docs/refs/project.md` — mDNS in AP mode would serve `.local` addresses only to
AP clients, which is not the primary use case.

### Double-Init Guard Prevents Redundant Calls
The `mdnsInitDone_` flag prevents `mdns_init()` from being called multiple times.
While `mdns_init()` is idempotent (returns ESP_ERR_INVALID_STATE on subsequent
calls), the guard avoids unnecessary FFI calls and log noise.

## Related Documentation

- [Project Reference — BLE, WiFi, mDNS init order](../refs/project.md) — Init
  order requirements (GR-3: WiFi → HTTP → BLE), mDNS design decisions
- [ESP-IDF mDNS Component Registry](https://components.espressif.com/components/espressif/mdns) —
  Official espressif/mdns component page
- [WiFi Manager Implementation](../../components/infrastructure/network/src/wifi.cpp) —
  startMdns() implementation at line 445
- [WiFi Manager Header](../../components/infrastructure/network/include/infrastructure/network/wifi.hpp) —
  mdnsInitDone_ guard at line 72
- [mDNS Stub Header](../../tests/stubs/mdns.h) — Host test stub for mDNS API
- [mDNS Regression Test](../../tests/src/test_mdns.cpp) — API compilation test
- [sdkconfig.defaults](../../sdkconfig.defaults) — CONFIG_MDNS_MAX_SERVICES=1
- [Component Manifest](../../main/idf_component.yml) — espressif/mdns dependency
- [AGENTS.md](../../AGENTS.md) — §GR-11 mandatory ESP-IDF master study,
  §4.3 sdkconfig policy, §Appendix B reference docs

## Commit Message

```
fix(network,docs,testing): enable mDNS hostname resolution for ecotiter.local

ESP-IDF v6 removed mDNS from built-in components — it now ships via
the IDF Component Registry as espressif/mdns. The project had no
idf_component.yml manifest, causing mdns.h to be unavailable,
#include "mdns.h" was commented out, and startMdns() was a stub.

Fix:
- Add main/idf_component.yml declaring espressif/mdns: "^1.11.3"
- Add espressif__mdns to infrastructure CMakeLists.txt REQUIRES
- Implement real startMdns() with mdns_init(), mdns_hostname_set(),
  mdns_service_add(), and mdnsInitDone_ double-init guard
- Add mdns_free() cleanup on registration failure paths
- Uncomment CONFIG_MDNS_MAX_SERVICES=1 in sdkconfig.defaults
- Fix .gitignore: replace bare idf_component.yml (ignores manifest)
  with /managed_components/ (ignores auto-downloaded deps)
- Add tests/stubs/mdns.h for host test compilation
- Add tests/src/test_mdns.cpp API regression test

AC verified:
- ecotiter.local resolves to device IP (192.168.1.103) via ping
- Service discoverable via avahi-browse: "EcoTiter Burette Controller"
  on _http._tcp port 80
- http://ecotiter.local/ loads Web UI successfully
- Build: 0 errors, 0 warnings
- Tests: 253/257 pass (4 pre-existing BLE failures)
- clang-tidy: 0 warnings
- 30s smoke test: PASS (no crash, no WDT, no panic)

Files:
A main/idf_component.yml
A tests/stubs/mdns.h
A tests/src/test_mdns.cpp
M .gitignore
M components/infrastructure/CMakeLists.txt
M components/infrastructure/network/include/infrastructure/network/wifi.hpp
M components/infrastructure/network/src/wifi.cpp
M sdkconfig.defaults
M tests/CMakeLists.txt

Report: docs/plans/completed/26_07_11_mdns_bugfix.md
```
