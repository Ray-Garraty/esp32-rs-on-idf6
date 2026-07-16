---
type: Known Issue
title: No systematic task stack sizing process — sizes determined reactively after crashes
description: All 6 task stack sizes were bumped reactively after stack overflow crashes. No proactive measurement, no documented budgets, no CI gate. StackMonitor gaps leave tasks untracked. ~17 ResponseBuffer (2048 B each) allocated on stack across codebase.
tags: [stack, architecture, process, diagnostic]
timestamp: 2026-07-16
status: active
supersedes: docs/plans/pending/26_07_16_stack_sizing.md (merged into solution section)
---

# No systematic task stack sizing process

## Problem

Every task stack size in this firmware was determined reactively: write code → crash (stack overflow) → double the stack → test → forget. This pattern repeated 5 times across 6 tasks (LL-001, LL-010, LL-038, LL-043, LL-048, LL-050). There is no proactive methodology.

### Current stack sizes (all defined ad-hoc)

| Task | Current size | History (evolution) | Defined in |
|------|-------------|---------------------|------------|
| Main (`app_main`) | 32 KB | 2304 → 32768 (LL-001) | `sdkconfig.defaults` |
| Motor | 16 KB | single value, never crashed | `domain/types.hpp:76` |
| Temperature | 16 KB | single value, never crashed | `domain/types.hpp:80` |
| Net Owner | 20 KB | 16384 → 20480 | `domain/types.hpp:78` |
| Log Worker | 12 KB | 4096 → 8192 → 12288 (LL-043, LL-048) | `domain/types.hpp:79` |
| HTTP Server | 16 KB | 12288 → 16384 (LL-050) | `http_server.hpp:52` + `domain/types.hpp:83` |
| BLE Notify | 8 KB | single value | `domain/types.hpp:81` |

### Concrete gaps

1. **crash_handler does not dump all watermarks.** `crash_handler.cpp` only prints the current crashing task's single watermark via `uxTaskGetStackHighWaterMark(nullptr)`. `StackMonitor::logAllWatermarks()` exists but is **never called** from any production code path. Full task watermarks are lost on crash.

2. **No periodic watermark logging.** `StackMonitor::logAllWatermarks()` was briefly called from the main loop but removed in ISSUE-003 due to ~43 ms UART blocking (violating Constitution Art. I). Gradual stack degradation (like LL-048's progressive watermark decline) goes undetected until crash.

3. **No CI gate for stack usage.** No step checks that stack watermarks stay above a threshold across builds. A PR that adds 2 KB of stack frames to a task with 1 KB headroom will pass CI silently.

4. **No per-task stack budget documentation.** There is no document listing worst-case call chain depth per task. When adding new handler code, developers have no way to estimate stack impact.

5. **MAX_THREADS = 8 is exceeded.** `StackMonitor` allocates a static array of 8 slots but currently 11 tasks are registered (main, Tmr Svc, ipc0, ipc1, wifi, phy_init, motor, temp, net_owner, log_worker, ble_notify). Three registrations are silently dropped. HTTP server task is NOT registered (it is created by `httpd_start()` internally). After adding it and spare slots, 16 are needed.

6. **ResponseBuffer (2048 B) allocated on stack in ~17 locations.** `ResponseBuffer` is `std::array<char, 2048>` defined in `domain/memory.hpp:15`. Stack allocations appear in:
   - **HTTP server task (16 KB stack):** 13 instances across 6 handlers (`rest_api.cpp` lines 89, 101, 128, 145, 163, 176, 191, 248; `http_server.cpp` lines 185, 236, 296, 374, 390). Most are in separate handler invocations (not nested), but worst-case concurrent depth could reach 2-3 depending on handler nesting.
   - **Main loop (32 KB stack):** 4 instances in separate scopes (lines 246, 318, 340, 485), never nested.
   - **CommandResponse::body** (`command.hpp:110`): embedded ResponseBuffer in struct — wherever CommandResponse is stack-allocated, another 2 KB appears.

   This is a systematic problem, not just HTTP-specific. Large stack-local buffers are a recurring pattern in every stack overflow crash (LL-001, LL-010, LL-043, LL-048, LL-050).

7. **No formal process for stack impact review.** When adding or modifying code that runs in a task context, there is no requirement to measure watermark impact or update the task's budget.

### ResponseBuffer usage audit (complete, 2026-07-16)

| # | File:Line | Context | Task | Stack | Call chain depth | Hot/Init | Priority |
|---|-----------|---------|------|-------|-----------------|----------|----------|
| 1 | `rest_api.cpp:89` | `ping_handler` local | HTTP server | 16 KB | 1 level | hot | MEDIUM |
| 2 | `rest_api.cpp:101` | `status_handler` local | HTTP server | 16 KB | 1→`handleStatusCore` | hot | MEDIUM |
| 3 | `rest_api.cpp:128` | `command_handler` parse-error path | HTTP server | 16 KB | 1 level | hot | MEDIUM |
| 4 | `rest_api.cpp:145` | `command_handler` sync-response | HTTP server | 16 KB | 2 levels | hot | HIGH |
| 5 | `rest_api.cpp:163` | `command_handler` timeout | HTTP server | 16 KB | 1 level | hot | MEDIUM |
| 6 | `rest_api.cpp:176` | `command_handler` result | HTTP server | 16 KB | 2 levels | hot | HIGH |
| 7 | `rest_api.cpp:191` | `valve_get_handler` local | HTTP server | 16 KB | 1→`handleCommandCore` | hot | MEDIUM |
| 8 | `rest_api.cpp:248` | `valve_post_handler` local | HTTP server | 16 KB | 1→`handleCommandCore` | hot | HIGH |
| 9 | `http_server.cpp:185` | `captive_wifi_status_handler` | HTTP server | 16 KB | 1 level | init | LOW |
| 10 | `http_server.cpp:236` | `status_root_handler` | HTTP server | 16 KB | 1 level | hot | MEDIUM |
| 11 | `http_server.cpp:296` | `log_handler` build-json | HTTP server | 16 KB | 1 level | hot | MEDIUM |
| 12 | `http_server.cpp:374` | `cal_handler` | HTTP server | 16 KB | 1 level | init | LOW |
| 13 | `http_server.cpp:390` | `log_handler` fetch-series | HTTP server | 16 KB | 1 level | hot | MEDIUM |
| 14 | `command.hpp:110` | `CommandResponse::body` embedded | any | varies | embedded in struct | both | HIGH |
| 15 | `main.cpp:246` | `sendResponse` lambda | Main loop | 32 KB | 1→`serializeToBuffer` | hot | LOW |
| 16 | `main.cpp:318` | compact broadcast | Main loop | 32 KB | 1→`serializeBroadcastCompact` | hot | LOW |
| 17 | `main.cpp:340` | extended broadcast | Main loop | 32 KB | 1→`serializeBroadcastExtended` | hot | LOW |
| 18 | `main.cpp:485` | SM result | Main loop | 32 KB | 1→`formatSmResult` | hot | LOW |

**Priority guide:**
- **HIGH:** in HTTP server (16 KB) with nested call chain or in BLE (8 KB via CommandResponse)
- **MEDIUM:** in HTTP server (16 KB) single-level handlers
- **LOW:** in main loop (32 KB) or init-only paths

## Root cause

1. **Historical:** The firmware evolved through three rewrites (Arduino → Rust → C++). Stack sizes were carried forward from each rewrite without re-validation. Each overflow was treated as an isolated incident rather than a symptom of a missing process.

2. **Architectural:** `StackMonitor` was designed for post-mortem analysis only (dump watermarks on crash). It was never intended for proactive monitoring or CI validation. The static 8-slot array cannot grow with the application. `crash_handler.cpp` only dumps the current crashing task's watermark, not all tasks.

3. **Process:** No review checklist requires stack impact analysis. The rule "After moving code between threads, verify with `uxTaskGetStackHighWaterMark()`" exists in `docs/refs/project.md` but is not enforced by CI or code review.

4. **Cultural:** "Double the stack and move on" is the path of least resistance. A proper worst-case analysis takes 30-60 minutes per task and requires instrumented builds — no one does it unless forced by a crash.

## Solution — 5-phase plan

### Phase 0 — Correct ISSUE-005 inaccuracies

**Status:** Done (2026-07-16)

Corrected:
- Gap 1: Self-registration of motor/temp/net_owner/ble_notify already exists — corrected description
- Gap 2: `logAllWatermarks()` is NEVER called from crash_handler (ISSUE-005 incorrectly stated it was) — corrected
- Gap 6: ResponseBuffer audit revealed ~18 instances, not 3 — audit table added
- PsramBuffer/PsramResource already exist — reflected in solution

### Phase 1 — Close StackMonitor blind spots

**Goal:** Every task's watermark visible in logs + periodic monitoring with zero main-loop latency impact.

| Step | File | Change | Risk | Verification |
|------|------|--------|------|-------------|
| 1.1 | `stack_monitor.hpp:13` | `MAX_THREADS` 8→16 | +160 B DRAM | Build passes |
| 1.2 | `log_worker` entry loop | Add `logAllWatermarks()` every 60s via `vTaskDelayUntil` | 43ms blocking in log_worker (acceptable — its primary function is I/O); stack impact must be verified | 60s after smoke, all 11+ tasks appear in log |
| 1.3 | `crash_handler.cpp` | Call `logAllWatermarks()` before reset | Redundant output in panic (acceptable — more data) | Crash dump contains all watermarks |
| 1.4 | `stack_monitor.hpp/cpp` | Optional: add `logWatermarkChunk(idx, count)` for chunked output | Extra API surface | Minimal |

**Design decision — periodic logging in log_worker (not main loop):**

`logAllWatermarks()` was removed from main loop in ISSUE-003 because it caused ~43ms UART blocking at 115200 baud, violating Constitution Art. I ("No blocking >10ms in main loop").

log_worker (12 KB stack, prio 0) is the correct home:
- Its primary function is I/O — 43ms blocking is within mission
- Constitution Art. I does not apply to worker tasks
- Single `vTaskDelayUntil` every 60s: `log_worker` already has a loop structure

**Stack budget check (log_worker):**
- Current stack: 12 KB
- `logAllWatermarks()` consumes ~1 KB of stack (12 lines of printf + format overhead)
- Need before/after watermark measurement to confirm headroom ≥25%

**Crash handler fix:**

`crash_handler.cpp` line 121:
```cpp
panic_puts("current watermark=");
```
Replace with:
```cpp
StackMonitor::instance().logAllWatermarks();
```

This is safe in panic context — all tasks are suspended, no contention, UART is in polling mode.

**Acceptance criteria:**
- `scripts/idf.sh smoke` + 60s → `rg "watermark" logs/serial_*.log` shows all 11+ tasks
- Intentional crash (e.g., `abort()`) → panic dump shows all watermarks
- `rg "MAX_THREADS" components/diag/src/stack_monitor.cpp` shows `16`

### Phase 2 — ResponseBuffer audit & PSRAM migration

**Goal:** Eliminate all ResponseBuffer stack allocations in constrained contexts (≤16 KB stack).

**Migration targets (from audit table above):**

| # | Location | Context | Stack | Priority | Action |
|---|----------|---------|-------|----------|--------|
| 1 | `rest_api.cpp:89` | `ping_handler` | 16 KB | MEDIUM | `PsramBuffer<2048>` |
| 2 | `rest_api.cpp:101` | `status_handler` | 16 KB | MEDIUM | Same |
| 3 | `rest_api.cpp:128` | `command_handler` error | 16 KB | MEDIUM | Same |
| 4 | `rest_api.cpp:145` | `command_handler` sync | 16 KB | HIGH | Same |
| 5 | `rest_api.cpp:163` | `command_handler` timeout | 16 KB | MEDIUM | Same |
| 6 | `rest_api.cpp:176` | `command_handler` result | 16 KB | HIGH | Same |
| 7 | `rest_api.cpp:191` | `valve_get_handler` | 16 KB | MEDIUM | Same |
| 8 | `rest_api.cpp:248` | `valve_post_handler` | 16 KB | HIGH | Same |
| 9 | `http_server.cpp:185` | `captive_wifi_status_handler` | 16 KB | LOW | Leave (init-only) |
| 10 | `http_server.cpp:236` | `status_root_handler` | 16 KB | MEDIUM | `PsramBuffer<2048>` |
| 11 | `http_server.cpp:296` | `log_handler` build-json | 16 KB | MEDIUM | Same |
| 12 | `http_server.cpp:374` | `cal_handler` | 16 KB | LOW | Leave (init-only) |
| 13 | `http_server.cpp:390` | `log_handler` fetch-series | 16 KB | MEDIUM | Same as #11 |
| 14 | `command.hpp:110` | `CommandResponse::body` | varies | HIGH | `PsramBuffer<2048>` or dynamic |
| 15 | `main.cpp:246` | `sendResponse` lambda | 32 KB | LOW | Leave (abundant stack) |
| 16-18 | `main.cpp:318,340,485` | broadcasts + SM result | 32 KB | LOW | Leave |

**Constitution Art. VI compliance — BLE notify (8 KB stack):**

`CommandResponse::body` (item #14, `std::array<char, 2048>`) is an embedded `ResponseBuffer`. If `CommandResponse` is stack-allocated in the BLE notify task call chain, it violates Art. VI: "large local arrays are forbidden in tasks with ≤ 8 KB stacks." This must be verified and fixed before any other migration — BLE notify has no headroom for a 2 KB stack-local buffer.

> If BLE notify does NOT use `CommandResponse` on its stack, this is a false alarm. Confirm by tracing BLE notify's call chain for `CommandResponse` allocation. The audit shows only `command.hpp:110` as the definition — actual usage depends on which functions BLE notify calls.

**Migration pattern — Option A (PsramBuffer, preferred):**

```cpp
// Before (stack, 2 KB):
domain::memory::ResponseBuffer buf{};

// After (PSRAM, 2 KB):
PsramBuffer<domain::memory::MAX_RSP_SIZE> buf{};
```

**Option B — PMR string (if function signatures must change):**

```cpp
std::pmr::string buf{&ecotiter::memory::psram_resource()};
auto result = handlePingCore(buf);
```

> Option B requires changing function signatures from `ResponseBuffer&` to `std::pmr::string&` or `std::span<char>`. Larger refactor. Option A is preferred for Phase 2 — minimal API disruption.

**Acceptance criteria:**
- Every HIGH/MEDIUM priority ResponseBuffer migrated to PsramBuffer
- Build passes, tests pass
- `scripts/idf.sh smoke` — no regressions
- Watermark on HTTP server task shows measurable improvement (≥2 KB freed)

### Phase 3 — Document stack budgets

**Goal:** Every task has a documented worst-case call chain, measured watermark, and 25% headroom.

**Steps:**

1. **Collect baseline measurements** (after Phase 1+2):
   - Run smoke test with 120s monitoring
   - Parse watermarks from log for all 11 tasks
   - Record in table

2. **Worst-case call chain analysis for each task:**

   | Task | Entry point | Deepest chain | Large locals |
   |------|-------------|---------------|--------------|
   | HTTP server | `httpd_*` → handler | `valve_post_handler` → `handleCommandCore` → `dispatchCommand` → `handleSetPosition` | After Phase 2: 0×ResponseBuffer |
   | Main | `app_main` loop | `sendResponse` → `serializeToBuffer` → `serializeStatusJson` | After Phase 2: 0×ResponseBuffer |
   | Motor | `motorTaskEntry` | `sm_run` → state handler → `moveToPosition` → RMT calls | None known |
   | Temperature | `tempTaskEntry` | `readTemperature` → UART read → parse | None known |
   | Net owner | `netTaskEntry` | WiFi init → HTTP start → event handler chain | None known |
   | Log worker | `logWorkerEntry` | `processLogQueue` → `logAllWatermarks` (60s) | 1×ResponseBuffer evicted? |
   | BLE notify | `bleNotifyTask` | `notify` → serialize → GATT send | 8 KB stack — critical |

3. **Add budget table to `docs/refs/project.md` §Thread Architecture:**

   ```markdown
   ### Stack budgets (2026-07-16)

   | Task | Stack (B) | Watermark (B) | Margin (B) | Usage | Headroom | Deepest call chain | Largest locals |
   |------|-----------|---------------|------------|-------|----------|-------------------|----------------|
   | HTTP | 16384 | TBD | TBD | TBD% | TBD% | `valve_post_handler → ...` | `PsramBuffer<2048>` (PSRAM) |
   | Main | 32768 | TBD | TBD | TBD% | TBD% | `sendResponse → ...` | none after P2 |
   | ⋮ | ⋮ | ⋮ | ⋮ | ⋮ | ⋮ | ⋮ | ⋮ |
   ```

4. **Set per-task headroom target:** All tasks must show ≥25% headroom. Any below that triggers stack increase or code refactoring before merging new code.

**Acceptance criteria:**
- `docs/refs/project.md` updated with full budget table
- Every task has ≥25% headroom
- Table verified against live hardware smoke test

### Phase 4 — CI gate

**Goal:** Every pre-commit run validates stack watermarks. PRs exceeding 75% usage fail.

| Step | File | Change |
|------|------|--------|
| 4.1 | `scripts/check_watermarks.py` | New script (see below) |
| 4.2 | `scripts/pre_commit.sh` | Add `python3 scripts/check_watermarks.py < logs/serial_*.log` to `--fast` mode |

**Script design (`scripts/check_watermarks.py`):**

```python
#!/usr/bin/env python3
"""
Parse StackMonitor::logAllWatermarks() output from serial log.
Exit 0 if all tasks <75% usage and all expected tasks present.
Exit 1 otherwise.
"""
import sys, re, glob
from pathlib import Path

EXPECTED_TASKS = {
    "main", "Tmr Svc", "ipc0", "ipc1", "wifi", "phy_init",
    "motor", "temp", "net_owner", "log_worker", "ble_notify",
    "http_server",
}
THRESHOLD_PCT = 75
TOLERANCE_PCT = 5
EFFECTIVE_THRESHOLD = THRESHOLD_PCT + TOLERANCE_PCT  # 80%

def main():
    log_path = None
    for arg in sys.argv[1:]:
        p = Path(arg)
        if p.exists():
            log_path = p; break
    if not log_path:
        matches = glob.glob("logs/serial_*.log")
        if matches:
            log_path = Path(matches[0])
    if not log_path:
        print("FAIL: no log file found"); sys.exit(1)

    text = log_path.read_text()
    pattern = re.compile(r"Thread (\S+): cfg=(\d+)B wmark=(\d+) used=(\d+)%")
    found_tasks = set()
    failures = []

    for name, cfg, wmark, pct in pattern.findall(text):
        found_tasks.add(name)
        pct_val = int(pct)
        if pct_val > EFFECTIVE_THRESHOLD:
            failures.append(f"{name}: {pct_val}% used (>{EFFECTIVE_THRESHOLD}%)")

    missing = EXPECTED_TASKS - found_tasks
    if missing:
        failures.append(f"missing tasks: {', '.join(sorted(missing))}")

    if failures:
        print("FAIL:")
        for f in failures: print(f"  {f}")
        sys.exit(1)
    print(f"OK: {len(found_tasks)} tasks, max usage within threshold")
    sys.exit(0)

if __name__ == "__main__":
    main()
```

**Pre-commit integration (pre_commit.sh --fast, after unit tests):**

```bash
# Step: Check stack watermarks
if ls logs/serial_*.log >/dev/null 2>&1; then
    python3 scripts/check_watermarks.py
fi
```

**Edge cases:**
- If no log file exists (no hardware test run), print a warning but do NOT fail — the smoke test in full mode catches it
- `http_server` may not appear if no HTTP request was served — ensure smoke triggers at least one request, or make optional in EXPECTED_TASKS
- Cold boot variance: effective threshold 80% (75% + 5% tolerance)

**Acceptance criteria:**
- `scripts/check_watermarks.py` exists and is executable
- With a log containing watermarks <75%, exit 0
- With a log containing any task >80% or missing task, exit 1
- `scripts/pre_commit.sh --fast` includes the check

### Phase 5 — Process enforcement

**Goal:** Stack impact is considered in every code review. Blind stack increases are forbidden.

| Step | File | Change |
|------|------|--------|
| 5.1 | `AGENTS.md` | Add hard rule: "Never blindly increase stack size" |
| 5.2 | `docs/refs/coding_style.md` §13 Pre-Merge Checklist | Add stack impact items |
| 5.3 | `.github/PULL_REQUEST_TEMPLATE.md` | Add Stack impact section |

**5.1 AGENTS.md rule (add to Core Directives):**

```markdown
### GR-NEW: NEVER BLINDLY INCREASE STACK SIZE
When a stack overflow is detected:
1. DO NOT double the stack size.
2. Analyze the call chain for hidden allocations (std::string, json, large arrays).
3. Move heavy objects to PSRAM via PsramBuffer or PMR allocator.
4. Increase stack only if mathematically proven necessary, and update budget table in project.md.
```

**5.2 coding_style.md §13 Pre-Merge Checklist additions:**

```markdown
- [ ] Stack impact: If this change adds frames to an existing task, measured watermark before/after
- [ ] Stack budget: If this change adds a new task, registered with StackMonitor and budget in project.md
- [ ] Buffer placement: No buffers >512 B on stack without justification (use PsramBuffer/heap)
```

**5.3 PR template:**

```
## Stack impact
- Task affected: {name} ({stack_size} B)
- Watermark before: {n} B ({pct}% used)
- Watermark after: {n} B ({pct}% used)
- Headroom: {n} B ({pct}%) — OK (≥25%) / **BELOW THRESHOLD**
```

**Acceptance criteria:**
- AGENTS.md contains the blind-increase rule
- coding_style.md checklist updated
- PR template exists with stack impact section

## Risks & mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| log_worker stack overflow from logAllWatermarks() | Medium | Crash during diagnostic | Measure watermark before/after in Phase 1; if <25% headroom, bump log_worker to 16 KB |
| UART bottleneck with 12 tasks logging every 60s | Low | 43ms burst every 60s in log_worker | Acceptable for worker task; monitor for log_worker queue backpressure |
| PsramBuffer allocation failure (PSRAM exhausted) | Low | HTTP 500 on response | PsramBuffer falls back to DRAM via heap_caps_malloc with SPIRAM fallback; document fallback behavior |
| CI check false positive on cold boot | Medium | CI blocks valid PR | ±5% tolerance band; document that check is advisory for cold-boot runs |

## Dependencies & estimates

| Phase | Depends on | Unlocks | Effort | Parallelizable |
|-------|-----------|---------|--------|----------------|
| 0 | — | — | Done | — |
| 1 | — | Phases 3, 4 (need periodic watermarks) | 2-3h | — |
| 2 | — | Phase 3 (need accurate watermark after eviction) | 4-6h | With 1 |
| 3 | Phase 1, 2 | Phase 4 (need documented budgets to know expected tasks) | 2-3h | After 1+2 |
| 4 | Phase 1 (periodic logging) | — | 1-2h | After 1 |
| 5 | All prior | — | 1h | After all |

**Total: ~10-15 hours**

## Edge cases

### MAX_THREADS capacity after increase
Bumping from 8 to 16 consumes an additional 8 × (pointer + uint8 + char[16]) ≈ 8 × 20 B = 160 B of DRAM. Acceptable for the diagnostic subsystem.

### Motor and temp task self-registration (ALREADY DONE)
Both tasks are created with `xTaskCreate(..., nullptr)` — the handle is not returned. Self-registration from the task body is already implemented in `motor/task.cpp:48` and `temp_thread.cpp:22`.

### Periodic logging ~ Art. I conflict
`logAllWatermarks()` caused ~43 ms UART blocking when called from main loop (ISSUE-003). Returning it to main loop would violate Constitution Art. I ("No blocking >10ms in main loop"). **Solution:** Offload to `log_worker` task (12 KB stack, runs at prio 0). The 43 ms burst is acceptable in log_worker context — it handles I/O as its primary function.

### memory_spec.md §7.3 — not affected by this plan
`memory_spec.md` §7.3 shows `print_heap_stats()` called from main loop every 60 seconds. This plan only moves `logAllWatermarks()` (12 printf calls, ~43ms blocking) to log_worker. `print_heap_stats()` reads `heap_caps_get_free_size` — fast, no UART flush bottleneck — and can remain in main loop. No doc sync needed, but Phase 3 should verify this still holds after all changes.

### PsramBuffer already exists
`PsramBuffer<N>` and `psram_resource()` PMR allocator already exist in the codebase (`psram_buffer.hpp`, `psram_resource.hpp`). No need to create them — only to use them.

### ResponseBuffer migration strategy
Not all 18 locations are equally critical. Priority:
1. HTTP server (16 KB) — 13 instances, hot path → `PsramBuffer<2048>`
2. `CommandResponse::body` (embedded) — affects any task using CommandResponse
3. Main loop (32 KB) — low priority, only if watermark analysis shows need

### CI gate false positives
A task at 76% usage on a cold boot might drop to 60% after warm-up. CI check uses effective threshold 80% (75% + 5% tolerance). Check is advisory for cold-boot-only runs.

## Related files

- [Constitution Art. I (Non-blocking main loop)](../refs/CONSTITUTION.md)
- [Constitution Art. VI (RAII/stack budget)](../refs/CONSTITUTION.md)
- [Constitution Art. VIII (Memory philosophy: no stack in PSRAM)](../refs/CONSTITUTION.md)
- [Thread architecture + stack constraints](../refs/project.md)
- [Memory spec — stack placement rules](../refs/memory_spec.md)
- [Diagnostic spec — StackMonitor + instrumentation](../refs/diagnostic_spec.md)
- [Stack overflow protocol](../protocols/stack_overflow.md)
- [StackMonitor header](../../components/diag/include/diag/stack_monitor.hpp)
- [StackMonitor source](../../components/diag/src/stack_monitor.cpp)
- [Domain types — all stack constants](../../components/domain/include/domain/types.hpp)
- [main.cpp — task creation](../../main/main.cpp)
- [Memory types — ResponseBuffer definition](../../components/domain/include/domain/memory.hpp)
- [PsramBuffer RAII wrapper](../../components/infrastructure/include/infrastructure/memory/psram_buffer.hpp)
- [PMR psram_resource()](../../components/infrastructure/include/infrastructure/memory/psram_resource.hpp)
- [Crash handler](../../components/diag/src/crash_handler.cpp)
- [AGENTS.md — Operational rules](../../../AGENTS.md)
- [Coding style — Pre-merge checklist](../refs/coding_style.md)
- [LL-001: main stack overflow](../lessons_learned/LL-001.yaml)
- [LL-010: UART FFI stack overflow](../lessons_learned/LL-010.yaml)
- [LL-038: system event task overflow](../lessons_learned/LL-038.yaml)
- [LL-043: log_worker overflow (4K→8K)](../lessons_learned/LL-043.yaml)
- [LL-048: log_worker overflow (8K→12K)](../lessons_learned/LL-048.yaml)
- [LL-050: HTTP server valve handler overflow (12K→16K)](../lessons_learned/LL-050.yaml)
- [SRP violations](ISSUE-004-srp-violations.md)
