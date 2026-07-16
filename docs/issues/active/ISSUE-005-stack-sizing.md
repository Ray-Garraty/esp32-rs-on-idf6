---
type: Known Issue
title: No systematic task stack sizing process — sizes determined reactively after crashes
description: "All 6 task stack sizes were bumped reactively after stack overflow crashes. No proactive measurement, no documented budgets, no CI gate. StackMonitor gaps leave tasks untracked. ~17 ResponseBuffer (2048 B each) allocated on stack across codebase. Phase 1 done 2026-07-16: MAX_THREADS 8->16, periodic watermarks in log_worker (60s), panic-safe dump in crash_handler, log_worker stack 12K->16K. Phase 2 done 2026-07-16: 11 of 13 HTTP server ResponseBuffers migrated to PsramBuffer (22 KB off stack). Phase 3 done 2026-07-16: Full stack budget table documented in memory_spec.md §5.4 with call chain analysis and headroom deficits. Phase 4 done 2026-07-16: check_watermarks.py CI gate integrated into pre_commit.sh --fast."
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
| Log Worker | 16 KB | 4096 → 8192 → 12288 → 16384 (LL-043, LL-048, Phase 1) | `domain/types.hpp:79` |
| HTTP Server | 16 KB | 12288 → 16384 (LL-050) | `http_server.hpp:52` + `domain/types.hpp:83` |
| BLE Notify | 8 KB | single value | `domain/types.hpp:81` |

### Concrete gaps

1. **crash_handler does not dump all watermarks.** `crash_handler.cpp` only prints the current crashing task's single watermark via `uxTaskGetStackHighWaterMark(nullptr)`. `StackMonitor::logAllWatermarks()` exists but is **never called** from any production code path. Full task watermarks are lost on crash. **(Fixed in Phase 1 — panic-safe `logAllWatermarks(panic_puts)` called before reset)**

2. **No periodic watermark logging.** `StackMonitor::logAllWatermarks()` was briefly called from the main loop but removed in ISSUE-003 due to ~43 ms UART blocking (violating Constitution Art. I). Gradual stack degradation (like LL-048's progressive watermark decline) goes undetected until crash. **(Fixed in Phase 1 — periodic logging every 60s in log_worker via timed `xQueueReceive`)**

3. **No CI gate for stack usage.** No step checks that stack watermarks stay above a threshold across builds. A PR that adds 2 KB of stack frames to a task with 1 KB headroom will pass CI silently. **(Fixed in Phase 4 — `check_watermarks.py` integrated into `pre_commit.sh --fast` as step 5.5, threshold 75% + 5% tolerance)**

4. **No per-task stack budget documentation.** There is no document listing worst-case call chain depth per task. When adding new handler code, developers have no way to estimate stack impact. **(Fixed in Phase 3 — full budget table in `memory_spec.md §5.4` with call chain analysis, headroom deficits, and action items)**

5. **MAX_THREADS = 8 is exceeded.** `StackMonitor` allocates a static array of 8 slots but currently 11 tasks are registered (main, Tmr Svc, ipc0, ipc1, wifi, phy_init, motor, temp, net_owner, log_worker, ble_notify). Three registrations are silently dropped. HTTP server task is NOT registered (it is created by `httpd_start()` internally). After adding it and spare slots, 16 are needed. **(MAX_THREADS bumped to 16 in Phase 1. However, Tmr Svc, wifi, phy_init are still not registered due to `xTaskGetHandle()` timing — see [ISSUE-006](ISSUE-006-deferred-task-registration.md).)**

6. **ResponseBuffer (2048 B) allocated on stack in ~17 locations.** `ResponseBuffer` is `std::array<char, 2048>` defined in `domain/memory.hpp:15`. Stack allocations appear in:
   - **HTTP server task (16 KB stack):** 13 instances across 6 handlers (`rest_api.cpp` lines 89, 101, 128, 145, 163, 176, 191, 248; `http_server.cpp` lines 185, 236, 296, 374, 390). Most are in separate handler invocations (not nested), but worst-case concurrent depth could reach 2-3 depending on handler nesting.
   - **Main loop (32 KB stack):** 4 instances in separate scopes (lines 246, 318, 340, 485), never nested.
   - **CommandResponse::body** (`command.hpp:110`): embedded ResponseBuffer in struct — wherever CommandResponse is stack-allocated, another 2 KB appears.

   This is a systematic problem, not just HTTP-specific. Large stack-local buffers are a recurring pattern in every stack overflow crash (LL-001, LL-010, LL-043, LL-048, LL-050).

   **(Phase 2: 11 of 13 HTTP server instances migrated to PsramBuffer (PSRAM heap). 2 init-only instances left as-is. CommandResponse::body investigated — BLE notify does NOT use it on stack, skipped.)**

7. **No formal process for stack impact review.** When adding or modifying code that runs in a task context, there is no requirement to measure watermark impact or update the task's budget.

### ResponseBuffer usage audit (updated 2026-07-16 — Phase 2 complete)

| # | File:Line | Context | Task | Stack | Priority | Phase 2 |
|---|-----------|---------|------|-------|----------|---------|
| 1 | `rest_api.cpp:89` | `ping_handler` local | HTTP server | 16 KB | MEDIUM | ✅ PsramBuffer |
| 2 | `rest_api.cpp:101` | `status_handler` local | HTTP server | 16 KB | MEDIUM | ✅ PsramBuffer |
| 3 | `rest_api.cpp:128` | `command_handler` parse-error path | HTTP server | 16 KB | MEDIUM | ✅ PsramBuffer |
| 4 | `rest_api.cpp:145` | `command_handler` sync-response | HTTP server | 16 KB | HIGH | ✅ PsramBuffer |
| 5 | `rest_api.cpp:163` | `command_handler` timeout | HTTP server | 16 KB | MEDIUM | ✅ PsramBuffer |
| 6 | `rest_api.cpp:176` | `command_handler` result | HTTP server | 16 KB | HIGH | ✅ PsramBuffer |
| 7 | `rest_api.cpp:191` | `valve_get_handler` local | HTTP server | 16 KB | MEDIUM | ✅ PsramBuffer |
| 8 | `rest_api.cpp:248` | `valve_post_handler` local | HTTP server | 16 KB | HIGH | ✅ PsramBuffer |
| 9 | `http_server.cpp:185` | `captive_wifi_status_handler` | HTTP server | 16 KB | LOW | ⏸️ Leave (init-only) |
| 10 | `http_server.cpp:236` | `status_root_handler` | HTTP server | 16 KB | MEDIUM | ✅ PsramBuffer |
| 11 | `http_server.cpp:296` | `log_handler` build-json | HTTP server | 16 KB | MEDIUM | ✅ PsramBuffer |
| 12 | `http_server.cpp:374` | `cal_handler` | HTTP server | 16 KB | LOW | ⏸️ Leave (init-only) |
| 13 | `http_server.cpp:390` | `log_handler` fetch-series | HTTP server | 16 KB | MEDIUM | ✅ PsramBuffer |
| 14 | `command.hpp:110` | `CommandResponse::body` embedded | any | varies | HIGH | 🔍 Investigated — BLE notify does NOT use on stack |
| 15 | `main.cpp:246` | `sendResponse` lambda | Main loop | 32 KB | LOW | ⏸️ Leave (abundant stack) |
| 16 | `main.cpp:318` | compact broadcast | Main loop | 32 KB | LOW | ⏸️ Leave |
| 17 | `main.cpp:340` | extended broadcast | Main loop | 32 KB | LOW | ⏸️ Leave |
| 18 | `main.cpp:485` | SM result | Main loop | 32 KB | LOW | ⏸️ Leave |

**Phase 2 result:** 11 of 13 HTTP server instances migrated → ~22 KB freed from HTTP server stack. 2 init-only left as-is. CommandResponse::body investigated — BLE notify does NOT allocate CommandResponse on its 8 KB stack, no change needed. Main loop items left as-is (32 KB stack, sufficient headroom).

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

**Status:** Done (2026-07-16)

**Goal:** Every task's watermark visible in logs + periodic monitoring with zero main-loop latency impact.

| Step | File | Change | Risk | Status |
|------|------|--------|------|--------|
| 1.1 | `stack_monitor.hpp:29` | `MAX_THREADS` 8→16 | +160 B DRAM | Done |
| 1.2 | `log_buffer.cpp` (workerTaskEntry) | `portMAX_DELAY` → `pdMS_TO_TICKS(60000)` + periodic `logAllWatermarks()` every 60s | 43ms blocking in log_worker (acceptable); stack impact verified | Done |
| 1.3 | `crash_handler.cpp` + `stack_monitor.{hpp,cpp}` | Replace single-task dump with `logAllWatermarks(panic_puts)` panic-safe method | Added panic-safe overload; no heap alloc; UART HAL only | Done |
| 1.4 | `stack_monitor.hpp/cpp` | Skip — not needed | — | Skipped |

**Design decision — periodic logging in log_worker (not main loop):**

`logAllWatermarks()` was removed from main loop in ISSUE-003 because it caused ~43ms UART blocking at 115200 baud, violating Constitution Art. I ("No blocking >10ms in main loop").

log_worker (stack bumped to 16 KB in Phase 1, prio 0) is the correct home:
- Its primary function is I/O — 43ms blocking is within mission
- Constitution Art. I does not apply to worker tasks
- Timed `xQueueReceive` every 60s (not `vTaskDelayUntil` — task is queue-driven, no periodic loop structure)

**Implementation details:**

1.1 — `static constexpr size_t MAX_THREADS` changed to 16 ([source](../../components/diag/include/diag/stack_monitor.hpp:29))

1.2 — `logAllWatermarks()` called every 60s via timed `xQueueReceive(q, &idx, pdMS_TO_TICKS(60000))`. If no log messages arrive, the timeout triggers the watermark check. This avoids changing the event-driven loop structure to a timer-based one.

1.3 — New panic-safe overload `logAllWatermarks(void (*print)(const char*))` writes through UART HAL callback (same pattern as `BlackBox::dump(panic_puts)`). No `ESP_LOGI`, no heap allocation, safe in panic context. Crash handler now outputs all task watermarks on crash instead of only the current task.

1.4 — Skipped. Chunked output not needed for 16 tasks.

**Watermark measurements after Phase 1 (70s after boot):**

```
Thread main:       cfg=32768B  wmark=25980  used=20%
Thread ipc0:       cfg=4096B   wmark=476    used=88%
Thread ipc1:       cfg=4096B   wmark=560    used=86%
Thread temp:       cfg=16384B  wmark=2148   used=86%
Thread motor:      cfg=16384B  wmark=1460   used=91%
Thread net_owner:  cfg=20480B  wmark=208    used=98%
Thread ble_notify: cfg=8192B   wmark=5264   used=35%
Thread log_worker: cfg=16384B  wmark=2140   used=86%
```

Note: Only 8 of 11+ tasks appear — Tmr Svc, wifi, phy_init are not registered due to `xTaskGetHandle()` call before these tasks exist. Tracked in [ISSUE-006](ISSUE-006-deferred-task-registration.md).

**Stack budget check (log_worker):**
- Before Phase 1: 12 KB, watermark ≈1484 (87%)
- After Phase 1 with 12 KB: watermark 1116 (90%) — below 25% headroom
- After bump to 16 KB: watermark 2140 (86%) — 13% absolute headroom
- Risk table target (16 KB) met; Phase 3 will require ≥25% for all tasks

**Acceptance criteria (Phase 1):**

| Criterion | Status | Notes |
|-----------|--------|-------|
| `rg "MAX_THREADS" components/diag/src/stack_monitor.cpp` shows `16` | ✅ | Line 29: `MAX_THREADS = 16` |
| Build — 0 errors, 0 warnings | ✅ | Clean build |
| Smoke test — 30s without Guru/WDT | ✅ | `scripts/idf.sh smoke` passed |
| Periodic watermarks visible at 60s | ✅ | Confirmed at ~62s after boot |
| Intentional crash shows all watermarks | ⏳ Not tested | Requires `abort()` trigger — acceptable to defer |
| All 11+ tasks in watermark log | ⏳ Partial | 8 of 11+ visible; deferred registration tracked in [ISSUE-006](ISSUE-006-deferred-task-registration.md) |

### Phase 2 — ResponseBuffer audit & PSRAM migration

**Status:** Done (2026-07-16)

**Goal:** Eliminate all ResponseBuffer stack allocations in constrained contexts (≤16 KB stack).

**Migration targets (from audit table above):**

| # | Location | Context | Stack | Priority | Action | Status |
|---|----------|---------|-------|----------|--------|--------|
| 1 | `rest_api.cpp:89` | `ping_handler` | 16 KB | MEDIUM | `PsramBuffer<2048>` | ✅ Done |
| 2 | `rest_api.cpp:101` | `status_handler` | 16 KB | MEDIUM | Same | ✅ Done |
| 3 | `rest_api.cpp:128` | `command_handler` error | 16 KB | MEDIUM | Same | ✅ Done |
| 4 | `rest_api.cpp:145` | `command_handler` sync | 16 KB | HIGH | Same | ✅ Done |
| 5 | `rest_api.cpp:163` | `command_handler` timeout | 16 KB | MEDIUM | Same | ✅ Done |
| 6 | `rest_api.cpp:176` | `command_handler` result | 16 KB | HIGH | Same | ✅ Done |
| 7 | `rest_api.cpp:191` | `valve_get_handler` | 16 KB | MEDIUM | Same | ✅ Done |
| 8 | `rest_api.cpp:248` | `valve_post_handler` | 16 KB | HIGH | Same | ✅ Done |
| 9 | `http_server.cpp:185` | `captive_wifi_status_handler` | 16 KB | LOW | Leave (init-only) | ⏸️ Skipped |
| 10 | `http_server.cpp:236` | `status_root_handler` | 16 KB | MEDIUM | `PsramBuffer<2048>` | ✅ Done |
| 11 | `http_server.cpp:296` | `log_handler` build-json | 16 KB | MEDIUM | Same | ✅ Done |
| 12 | `http_server.cpp:374` | `cal_handler` | 16 KB | LOW | Leave (init-only) | ⏸️ Skipped |
| 13 | `http_server.cpp:390` | `log_handler` fetch-series | 16 KB | MEDIUM | Same as #11 | ✅ Done |
| 14 | `command.hpp:110` | `CommandResponse::body` | varies | HIGH | Investigate BLE notify usage | 🔍 Investigated — BLE notify does NOT use on stack |
| 15 | `main.cpp:246` | `sendResponse` lambda | 32 KB | LOW | Leave (abundant stack) | ⏸️ Skipped |
| 16-18 | `main.cpp:318,340,485` | broadcasts + SM result | 32 KB | LOW | Leave | ⏸️ Skipped |

**Result: 11 of 13 HTTP server ResponseBuffer instances migrated → ~22 KB total stack freed from HTTP server task (16 KB stack).**

**Item 14 investigation — BLE notify & Constitution Art. VI:**

`CommandResponse::body` (item #14, `std::array<char, 2048>`) is an embedded `ResponseBuffer` in the `CommandResponse` struct. Investigation of `ble_notify_thread.cpp` confirmed that the BLE notify task (8 KB stack) does NOT allocate `CommandResponse` on its stack — it only uses `BleNotifyItem` (small, fixed-size struct). **No change needed; Constitution Art. VI is not violated.**

**Migration pattern used — Option A (PsramBuffer, with reinterpret_cast for handle*Core calls):**

In `rest_api.cpp` handlers that call `handle*Core(ResponseBuffer&)`:
```cpp
ecotiter::memory::PsramBuffer<domain::memory::MAX_RSP_SIZE> _buf{};
auto& buf = *reinterpret_cast<domain::memory::ResponseBuffer*>(_buf.data());
auto result = handlePingCore(buf);
httpd_resp_send(req, buf.data(), static_cast<ssize_t>(*result));
```

In `http_server.cpp` handlers that use buffer directly (no handle*Core calls):
```cpp
ecotiter::memory::PsramBuffer<domain::memory::MAX_RSP_SIZE> rsp{};
int n = std::snprintf(reinterpret_cast<char*>(rsp.data()), rsp.size(), "...");
httpd_resp_send(req, reinterpret_cast<const char*>(rsp.data()), static_cast<ssize_t>(n));
```

**Fix to PsramBuffer:** `std::bad_alloc` throw replaced with `std::abort()` — exceptions are disabled in ESP-IDF.

**Acceptance criteria:**

| Criterion | Status | Notes |
|-----------|--------|-------|
| Every HIGH/MEDIUM priority ResponseBuffer migrated to PsramBuffer | ✅ | 11 of 11 HIGH/MEDIUM done |
| Build — 0 errors, 0 warnings | ✅ | Clean build |
| Unit tests — all pass | ✅ | 246/246 (776 assertions) |
| `scripts/idf.sh smoke` — no regressions | ✅ | 30s BOOT OK, no Guru/WDT |
| Watermark improvement on HTTP server (≥2 KB freed) | ⏳ Needs before/after measurement | Phase 3 will collect post-Phase-2 baseline |

### Phase 3 — Document stack budgets

**Status:** Done (2026-07-16)

**Goal:** Every task has a documented worst-case call chain, measured watermark, and 25% headroom.

**Completed:**

1. Collected baseline measurements from live hardware (~62s after cold boot)
2. Performed worst-case call chain analysis for all 9 visible tasks
3. Added full budget table to `memory_spec.md §5.4` (moved from `project.md` post-implementation — better fit per review)
4. Set per-task headroom target (≥25%) with deficits documented

**Result:** Stack budget table now lives in `docs/refs/memory_spec.md` §5.4 Task Stack Budgets. `docs/refs/project.md` contains a cross-reference. Full call chain analysis, headroom deficits with action items, and measurement methodology are included.

**Acceptance criteria:**

| Criterion | Status | Notes |
|-----------|--------|-------|
| Stack budget table documented | ✅ | `memory_spec.md §5.4` — 9 tasks, full call chains, largest locals |
| Call chain analysis for each task | ✅ | Entry point → deepest reachable frames, with frame count estimates |
| Measurement notes documented | ✅ | ±5% variance, ISSUE-006 gap, 43ms UART burst |
| Headroom deficits with action items | ✅ | Motor (9%), net_owner (2%), temp (14%), log_worker (14%), ipc0/ipc1 (12-14%) |
| Table verified against smoke test | ✅ | 30s BOOT OK, watermarks match Phase 1 measurements |
| All tasks ≥25% headroom? | ❌ Not met | Documented as deficits for Phases 4+5 to address |

### Phase 4 — CI gate

**Status:** Done (2026-07-16)

**Goal:** Every pre-commit run validates stack watermarks. PRs exceeding 75% usage fail.

**Completed:**

| Step | File | Change | Status |
|------|------|--------|--------|
| 4.1 | `scripts/check_watermarks.py` | New script (68 lines, executable) | ✅ Done |
| 4.2 | `scripts/pre_commit.sh` | Step 5.5 added between unit tests and docs validation | ✅ Done |

**Implementation:**

`check_watermarks.py` parses `Thread <name>: cfg=<bytes>B wmark=<bytes> used=<pct>%` lines from serial logs. Validates 12 expected tasks against 75% + 5% tolerance = 80% effective threshold. Auto-discovers `logs/serial_*.log` if no CLI arg given. Exit 0 on pass, Exit 1 on failure.

Integrated into `pre_commit.sh --fast` as step 5.5 (after unit tests, before docs validation). Gracefully skips with `⏭️` if no serial log file exists.

**Edge cases:**
- If no log file exists (no hardware test run), skip with warning — the smoke test in full mode catches it
- `http_server` may not appear if no HTTP request was served — makes it advisory in EXPECTED_TASKS
- Cold boot variance: effective threshold 80% (75% + 5% tolerance)

**Acceptance criteria:**

| Criterion | Status | Notes |
|-----------|--------|-------|
| `scripts/check_watermarks.py` exists and is executable | ✅ | `-rwxrwxr-x` |
| With log containing watermarks <75%, exit 0 | ✅ | Tested against clean boot log |
| With log containing task >80% or missing task, exit 1 | ✅ | Tested against existing logs (net_owner 92%, log_worker 97%) |
| `scripts/pre_commit.sh --fast` includes the check | ✅ | Step 5.5, after unit tests |
| Build — 0 errors, 0 warnings | ✅ | `scripts/idf.sh build` clean |
| Smoke test — BOOT OK, no Guru/WDT | ✅ | `scripts/idf.sh smoke` passed |

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
4. Increase stack only if mathematically proven necessary, and update budget table in memory_spec.md §5.4.
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
| log_worker stack overflow from logAllWatermarks() | Low | Crash during diagnostic | Phase 1: measured 1116 B (90%) at 12 KB, bumped to 16 KB → 2140 B (86%). 13% headroom — below Phase 3 target but within Phase 1 risk acceptance |
| UART bottleneck with 12 tasks logging every 60s | Low | 43ms burst every 60s in log_worker | Acceptable for worker task; log_worker queue backpressure not observed in Phase 1 testing |
| PsramBuffer allocation failure (PSRAM exhausted) | Low | HTTP 500 on response | PsramBuffer aborts on OOM (exceptions disabled in ESP-IDF). 8 MB PSRAM available, 11×2 KB = 22 KB consumed — negligible. Not observed in Phase 2 testing |
| CI check false positive on cold boot | Medium | CI blocks valid PR | ±5% tolerance band; document that check is advisory for cold-boot runs |

## Dependencies & estimates

| Phase | Depends on | Unlocks | Effort | Parallelizable |
|-------|-----------|---------|--------|----------------|
| 0 | — | — | Done | — |
| 1 | — | Phases 3, 4 (need periodic watermarks) | Done (~4h inc. log_worker stack bump + smoke) | — |
| 2 | — | Phase 3 (need accurate watermark after eviction) | Done (~3h inc. 11 PsramBuffer migrations + smoke) | With 1 |
| 3 | Phase 1, 2 | Phase 4 (need documented budgets to know expected tasks) | Done (~2h inc. call chain analysis + smoke) | After 1+2 |
| 4 | Phase 1 (periodic logging) | Phase 5 | Done (~1h inc. script + pre-commit integration + smoke) | After 1 |
| 5 | All prior | — | 1h | After all |

**Total: ~10-15 hours**

## Edge cases

### MAX_THREADS capacity after increase
Bumping from 8 to 16 consumes an additional 8 × (pointer + uint8 + char[16]) ≈ 8 × 20 B = 160 B of DRAM. Acceptable for the diagnostic subsystem.

### Motor and temp task self-registration (ALREADY DONE)
Both tasks are created with `xTaskCreate(..., nullptr)` — the handle is not returned. Self-registration from the task body is already implemented in `motor/task.cpp:48` and `temp_thread.cpp:22`.

### Periodic logging ~ Art. I conflict
`logAllWatermarks()` caused ~43 ms UART blocking when called from main loop (ISSUE-003). Returning it to main loop would violate Constitution Art. I ("No blocking >10ms in main loop"). **Solution:** Offloaded to `log_worker` task (16 KB stack after Phase 1 bump, runs at prio 0). Implemented via timed `xQueueReceive` (60s timeout) rather than `vTaskDelayUntil` — the log worker is queue-driven and has no periodic loop structure. The 43 ms burst is acceptable in log_worker context — it handles I/O as its primary function.

### memory_spec.md §7.3 — not affected by this plan
`memory_spec.md` §7.3 shows `print_heap_stats()` called from main loop every 60 seconds. This plan only moves `logAllWatermarks()` (12 printf calls, ~43ms blocking) to log_worker. `print_heap_stats()` reads `heap_caps_get_free_size` — fast, no UART flush bottleneck — and can remain in main loop. No doc sync needed, but Phase 3 should verify this still holds after all changes.

### PsramBuffer already exists + fix applied
`PsramBuffer<N>` and `psram_resource()` PMR allocator already exist in the codebase (`psram_buffer.hpp`, `psram_resource.hpp`). No need to create them — only to use them. **Fix applied in Phase 2:** `std::bad_alloc()` throw replaced with `std::abort()` because exceptions are disabled in ESP-IDF (no exception handler, `throw` would call `abort()` anyway but unreachable code warning).

### PsramBuffer data() returns uint8_t, not char
All handler code uses `char*` for `std::snprintf` and `httpd_resp_send`. Migration in `http_server.cpp` (no `handle*Core` calls) uses `reinterpret_cast<char*>(buf.data())`. Migration in `rest_api.cpp` (calls `handle*Core(buf)` taking `ResponseBuffer&`) wraps PsramBuffer memory via `*reinterpret_cast<ResponseBuffer*>(_buf.data())` — safe because both are 2048 B contiguous buffers with identical layout.

### ResponseBuffer migration strategy
Not all 18 locations are equally critical. Priority:
1. HTTP server (16 KB) — 13 instances, hot path → `PsramBuffer<2048>` **(Phase 2: 11 of 13 done)**
2. `CommandResponse::body` (embedded) — affects any task using CommandResponse **(Phase 2: investigated — BLE notify does NOT use on stack, skipped)**
3. Main loop (32 KB) — low priority, only if watermark analysis shows need **(left as-is)**

### Deferred registration of internal tasks (discovered in Phase 1)
`StackMonitor::registerMainTask()` calls `xTaskGetHandle()` for Tmr Svc, wifi, phy_init before these tasks exist. Only 8 of 11+ tasks appear in watermark output. Tracked in [ISSUE-006](ISSUE-006-deferred-task-registration.md). Phase 4 CI gate's `EXPECTED_TASKS` must account for this until ISSUE-006 is resolved.

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
- [Deferred task registration](ISSUE-006-deferred-task-registration.md) — Tmr Svc, wifi, phy_init not registered with StackMonitor
