---
type: Plan
title: WebSocket handler frame.len bugfix
description: >
  Fix ws_handler in http_server.cpp so WebSocket frames are correctly parsed
  and consumed from the TCP stream, eliminating 1-2s session drop cycle.
tags: [websocket, http_server, httpd_ws_recv_frame, network, bugfix]
timestamp: 2026-07-17
status: completed
updated: 2026-07-17
---

# WebSocket Handler frame.len Bugfix

## Executive Summary

WebSocket sessions dropped every 1-2 seconds because `frame.len` was
initialized to `sizeof(buf)` (1024) instead of 0, causing ESP-IDF's
`httpd_ws_recv_frame_internal()` to skip header parsing and leave the
frame payload unconsumed in the TCP buffer. A 3-part fix corrected the
frame length initialization, the `max_len` argument, and error handling
to preserve sessions on transient failures. Hardware smoke test confirms
zero drops and 17/17 integration tests passing.

## Initial Goal

**Problem:** WebUI disconnects every 1-2 seconds. ADC readings disappear
and control buttons deactivate. Serial log shows repeated WS session
additions for the same fd and intermittent `"Failed to receive payload"`
warnings.

**Acceptance Criteria:**
1. Zero `"Failed to receive payload"` warnings during normal broadcast
2. WS session remains connected for >30s without drops
3. WebUI controls remain interactive continuously
4. `check_ws_session_drops()` regression detects zero drops
5. No buffer overflows on zero-length or oversized frames

**Scope:**
- Modify only `components/infrastructure/network/src/http_server.cpp`
- Add regression check to `scripts/testing/http_api_test.py`
- No architectural or API changes

## Plan Summary

**Approach:** Remove the erroneous `frame.len = sizeof(buf)` initialization
(zero-init already sets `.len = 0`, which triggers correct header parsing),
pass `sizeof(buf)` as the `max_len` argument to `httpd_ws_recv_frame`,
and differentiate error handling so that transient recv failures do not
call `removeSession()`.

**Dependencies:** `ESP-IDF httpd_ws_recv_frame_internal()` behaviour
(documentation: if `frame->len == 0`, parse frame header; otherwise
skip parsing).

**Risks:**
- Zero-length text frames: guarded by `frame.len > 0` check
- Malformed frames: `httpd_ws_recv_frame` returns error, handler returns
  `ESP_OK` without removing session; connection self-heals
- Multiple WS clients: each fd tracked independently, fix applies equally

## Implementation

### Files Changed

| File | Lines Added | Lines Removed | Purpose |
|------|-------------|---------------|---------|
| `components/infrastructure/network/src/http_server.cpp` | 9 | 7 | Core bugfix: frame.len, max_len arg, error handling, OOB guard |
| `scripts/testing/http_api_test.py` | 68 | 0 | Regression check `check_ws_session_drops()` |
| `components/interface/include/interface/webui.hpp` | 11 | 5 | Pre-existing unrelated console.log additions (NOT part of fix) |

### Changes in Detail (`http_server.cpp`)

1. **Removed `frame.len = sizeof(buf)`** — value-initialization `{}` already
   zeroes `.len`, which tells ESP-IDF to parse the frame header.

2. **Changed `httpd_ws_recv_frame(req, &frame, frame.len)`** to
   `httpd_ws_recv_frame(req, &frame, sizeof(buf))` — the third argument is
   `max_len` (buffer capacity), not frame length. With the old code, after
   removing `frame.len = sizeof(buf)`, `frame.len` would be 0, and passing 0
   as `max_len` would always fail.

3. **Differentiated error handling:**
   - `ESP_ERR_INVALID_SIZE` → log warning with actual frame size, return
     `ESP_FAIL` (triggers clean WS close by HTTP server)
   - All other errors → log warning, return `ESP_OK` (session survives)
   - **Removed** `removeSession(fd)` from the generic error path — stale
     sessions are cleaned by the broadcast loop which calls
     `httpd_ws_get_fd_info()`

4. **WS CLOSE still removes session** — preserved as-is.

5. **Guarded `buf[frame.len] = '\0'`** with
   `std::min<size_t>(frame.len, sizeof(buf) - 1)` — prevents out-of-bounds
   write if frame payload exceeds buffer size.

## Issues Encountered

### Phase: Implementation — None

The fix was determined by a single analysis pass. No iteration or rework
was needed. All ACs were verified on the first attempt.

### Phase: Validation — None

Smoke test passed on first run. No regressions found.

## Rework Cycles

**Zero rework cycles.** Single-pass fix:

| Cycle | Issue | Resolution | Time |
|-------|-------|------------|------|
| 1 | Initial implementation | All ACs met, smoke pass, review approved | N/A |

## Metrics

### Build & Test

| Metric | Value |
|--------|-------|
| Build warnings | 0 |
| Build errors | 0 |
| Host test cases | 248 |
| Host assertions | 791 |
| clang-tidy files | 46 (clean) |
| Smoke test runtime | 61s |
| Integration tests | 17/17 pass |
| WS broadcasts (20s) | 30 |

### Code Changes (fix only)

| File | +Lines | -Lines |
|------|--------|--------|
| `http_server.cpp` | 9 | 7 |
| `http_api_test.py` | 68 | 0 |
| **Total** | **77** | **7** |

### Cost Breakdown

```
agent_tree     sessions  tokens  wall_sec
-------------  --------  ------  --------
teamlead       1         49487   2012.8
  implementer  1         88735   538.2
  verifier     1         75923   159.3
  planner      1         66380   76.6
  validator    1         59606   792.6
  reviewer     1         56374   90.0
  verifier     1         55887   72.6
  planner      1         53828   119.0
  reporter     1         23246   10.4
-----------------------------------------
Total          9         529466  3871.5
```

### Session Activity (2026-07-17)

| Agent | Sessions | Tokens | Wall Time |
|-------|----------|--------|-----------|
| build | 2 | 531,517 | 5,657s |
| verifier | 3 | 196,720 | 378s |
| implementer | 2 | 149,506 | 969s |
| planner | 2 | 120,208 | 196s |
| explore | 4 | 119,380 | 157s |
| validator | 1 | 59,606 | 793s |
| reviewer | 1 | 56,374 | 90s |
| teamlead | 1 | 49,487 | 2,013s |
| reporter | 1 | 23,246 | 10s |

### Tool Usage (last 24h, top agents)

| Agent | Top Tools | Calls |
|-------|-----------|-------|
| implementer | read(43), bash(18), edit(15) | 90 |
| verifier | grep(45), read(42), glob(17) | 118 |
| validator | bash(30), read(4), grep(4) | 44 |
| planner | read(35), grep(12), glob(8) | 62 |
| reviewer | read(8), grep(3) | 11 |

### Operation Timing (last 24h)

| Operation | Calls | Avg (s) | Total (s) |
|-----------|-------|---------|-----------|
| build | 5 | 121.4 | 606.8 |
| smoke | 4 | 121.7 | 486.6 |
| tidy | 3 | 39.4 | 118.1 |
| test | 4 | 11.9 | 47.7 |
| flash | 1 | 31.0 | 31.0 |
| monitor | 1 | 31.9 | 31.9 |

### Suspicious Commands

None detected in the last 24 hours.

## Verification

### Acceptance Criteria Results

| # | Criterion | Result | Evidence |
|---|-----------|--------|----------|
| 1 | Zero "Failed to receive payload" warnings | ✅ PASS | Serial log: zero occurrences during 61s smoke |
| 2 | WS session remains connected >30s | ✅ PASS | User confirms manual WebUI stable >30s |
| 3 | WebUI controls interactive continuously | ✅ PASS | User confirms ADC readings and controls working |
| 4 | `check_ws_session_drops() = 0` | ✅ PASS | Regression check returns 0 drops |
| 5 | No buffer overflow on edge cases | ✅ PASS | `std::min` guard prevents OOB; CLOSE path tested |

### Build & Static Analysis

- `scripts/idf.sh build` — **0 errors, 0 warnings**
- `scripts/idf.sh test` — **248 test cases, 791 assertions — ALL PASS**
- `scripts/idf.sh tidy` — **46 files clean, clang-tidy PASS**
- `scripts/pre_commit.sh --fast` — **ALL steps pass**

### Hardware Validation

- `scripts/idf.sh smoke` (61s on real ESP32-S3) — **PASS**
- No Guru Meditation, no WDT panic
- Integration test: **17/17 pass**, 30 WS broadcasts over 20s
- Manual WebUI by user: **PASS** — stable >30s

### Code Review

- **Verdict:** Approved
- **Issues:** None

## Lessons Learned

1. **Review ESP-IDF semantics before relying on frame.len.** The ESP-IDF
   `httpd_ws_recv_frame_internal()` documentation states: *"If frame len is 0,
   will get frame len from req. Otherwise regard frame len already achieved
   by calling httpd_ws_recv_frame before."* Initializing `frame.len` to a
   non-zero value causes the function to skip header parsing entirely.

2. **Distinguish `max_len` from `frame.len`.** The third argument to
   `httpd_ws_recv_frame` is the buffer capacity (`max_len`), not the
   expected frame length. Passing `frame.len` as `max_len` couples two
   independent parameters.

3. **Session removal should be conservative.** Removing a WebSocket session
   on every transient recv failure causes unnecessary reconnect storms.
   Let the broadcast loop (which calls `httpd_ws_get_fd_info()`) handle
   stale session cleanup.

4. **Guard buffer writes at the call site.** Even when the frame claims to
   contain `frame.len` bytes, the buffer boundary must be verified with
   `std::min()` to prevent OOB writes from malformed or oversized frames.

## Related Documentation

- **Issue report:** [docs/issues/active/ws_handler_frame_len_bug.md](../../issues/active/ws_handler_frame_len_bug.md)
  (to be archived to `docs/issues/closed/`)
- **Source file:** `components/infrastructure/network/src/http_server.cpp`
- **Test file:** `scripts/testing/http_api_test.py`
- **ESP-IDF ref:** `httpd_ws_recv_frame_internal()` in
  `components/esp_http_server/src/httpd_ws.c`

## Commit Message

```
fix(network): fix ws_handler frame header parsing and session drops

Root cause: frame.len was initialized to 1024 (sizeof(buf)) instead of
0, causing httpd_ws_recv_frame_internal() to skip frame header parsing.
The payload remained unconsumed in the TCP buffer, misaligning the next
frame and triggering session removal on every recv failure.

Fix:
- Remove frame.len = sizeof(buf) — value-init zeroes .len, enabling
  correct ESP-IDF header parsing
- Pass sizeof(buf) as max_len to httpd_ws_recv_frame (was frame.len,
  which was 0 after the fix above)
- Differentiate errors: ESP_ERR_INVALID_SIZE returns ESP_FAIL (clean
  close); all other errors return ESP_OK (session survives)
- Guard buf[frame.len] = '\0' with std::min(frame.len, sizeof(buf)-1)
  to prevent OOB write on oversized frames
- Remove removeSession(fd) from generic error path — broadcast loop
  handles stale session cleanup via httpd_ws_get_fd_info()

AC verified:
- Zero "Failed to receive payload" warnings during 61s smoke test
- WS session remains connected >30s (user confirmed stable WebUI)
- check_ws_session_drops() = 0 drops
- 17/17 integration tests pass, 30 WS broadcasts received
- No Guru Meditation, no WDT panic

Files:
- components/infrastructure/network/src/http_server.cpp (+9 -7)
- scripts/testing/http_api_test.py (+68 -0)

Report: docs/plans/completed/26_07_17_ws_handler_frame_len_bugfix.md
```
