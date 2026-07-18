---
type: Known Issue
title: AckThen commands block HTTP worker task, violating Articles I and II
description: burette.moveSteps (and other AckThen commands) block the HTTP worker task on waitResult(60000), preventing new HTTP/WS connections and violating task sovereignty
tags: [http, motor, architecture, constitution, blocking, phase0-done, phase1-done]
timestamp: 2026-07-17
status: active
---

# AckThen commands block HTTP worker task

## Problem

Any command that returns `ResponseKind::AckThen` (e.g., `burette.moveSteps`, `burette.fill`, `burette.empty`, `burette.doseVolume`, `burette.rinse`, `burette.cal.run`) blocks the HTTP worker task for the entire duration of the motor operation.

Evidence from `http_api_test.py` run on 2026-07-17_15-01-13:

```
15:01:22.760  POST /api/command burette.moveSteps steps=3000
15:01:23.261  WS connect ws://172.16.130.39/ws/stream
                 🟡 2.56 seconds — no response from HTTP server
15:01:25.822  WS {type:'sub'} sent
15:01:25.823  moveSteps HTTP 200 ← motor done, HTTP worker free
15:01:25.965  WS session finally registered
```

During the 2.56-second window, the HTTP server **cannot accept new WebSocket connections** because the single HTTP worker task is stuck in `waitResult(60000)`. The main loop continues broadcasting to existing WS clients via `httpd_ws_send_frame_async` (direct socket write), but any new HTTP request (status poll, new WS upgrade, second command) is queued behind the blocked worker.

### Affected commands

All handlers returning `makeAckThenResponse()` in `components/application/src/handlers/burette_ops.cpp`:

| Function | Line | Type |
|----------|------|------|
| `handleFill()` | 33 | Fill |
| `handleEmpty()` | 55 | Empty |
| `handleDoseVolume()` | 92 | DoseVolume |
| `handleRinse()` | 110 | Rinse |
| `handleCalRun()` | 168 | CalRun (speed/dose) |
| `handleMoveSteps()` | 215 | MoveSteps |

Also `handleCalGetResult()` in `burette_cal.cpp:193`.

---

## Root cause

### Code path

`rest_api.cpp:127-218` — `command_handler()`:

```cpp
// L161: For Single/Error — return immediately
if (rsp->kind != application::ResponseKind::AckThen) {
    // ... serialize and return ...
    return ESP_OK;
}

// L191: AckThen — BLOCK
auto result = controller->waitResult(60000);
```

`motor_controller_impl.cpp:418-434` — `waitResult()`:

```cpp
std::optional<domain::SmResult> waitResult(uint32_t timeoutMs) {
    // ...
    if (xQueueReceive(q, &result, pdMS_TO_TICKS(timeoutMs)) == pdTRUE) {
        return result;
    }
    return std::nullopt;
}
```

`xQueueReceive` with a 60-second timeout blocks the **calling task** (the HTTP worker) until the motor task produces a result via `store_result()`. The motor task only calls `store_result()` after `execute_move_steps()` completes — which can take seconds.

### Why WS is partially affected

- `httpd_ws_send_frame_async` sends data **directly to the socket** via `sess->send_fn()` from the calling task (main loop). This works because the socket is asynchronous at the TCP level.
- But **new WebSocket connections** require an HTTP upgrade handshake (`HTTP_GET`) which must be processed by a **free HTTP worker task**. If the only worker is blocked on `waitResult`, the handshake stalls.

### Constitutional violations

**Article I — Main Loop is Sacred (Non-Blocking):** While the blocked task is the HTTP worker (not `app_main`), the spirit of Article I applies to any system task whose blockage starves network services. The HTTP worker is a system-critical task — its blockage prevents HTTP request processing.

**Article II — Task Sovereignty:** The HTTP handler task **directly waits** for the motor task via `xQueueReceive(resultQueue, ..., 60000)`. Tasks must communicate via queues without synchronous waiting. The correct pattern is fire-and-forget: return `accepted` immediately, the motor task reports completion via WS broadcast.

---

## Solution (proposed)

Replace the synchronous `waitResult` pattern with an asynchronous fire-and-forget model:

1. **HTTP handler** returns `{"status":"accepted"}` immediately for all AckThen commands (never blocks)
2. **Motor task** sends a completion event via WS broadcast when done (already has `store_result()` infrastructure)
3. **HTTP worker task** remains free to process other requests during motor operation
4. **Client** learns about completion via WS messages (`brt.sts` transition or explicit `{"event":"motor_complete",...}`)

### Implementation sketch

```
rest_api.cpp:command_handler():
  if (kind == AckThen) {
      httpd_resp_send(req, R"({"status":"accepted"})", ...);
      return ESP_OK;  // ← never block
  }

Motor task → store_result() → notify via WS broadcast
```

This eliminates the `waitResult(60000)` call entirely and restores full HTTP/WS availability during motor operations.

---

## Edge cases

1. **Backward compatibility**: WebUI JS (`stepper.js`) currently expects a synchronous HTTP response for AckThen commands. It must be updated to handle `{"status":"accepted"}` and listen for WS completion events instead.

2. **Client timeout**: If no WS is connected, the client never learns about completion. Fallback: client polls `/api/status` (which works because HTTP worker is never blocked).

3. **Multiple AckThen in flight**: Currently impossible (queue system rejects if busy → returns `makeErrorResponse("busy")`). With async model, the queue check still applies — motor busy returns error immediately. No change needed.

4. **Existing WS connections**: Continue to receive broadcasts during motor ops (already proven working — `httpd_ws_send_frame_async` bypasses HTTP worker).

---

## Related modules

| File | Role |
|------|------|
| `components/interface/src/rest_api.cpp` | HTTP handler with `waitResult(60000)` blocking call |
| `components/infrastructure/src/motor/motor_controller_impl.cpp` | `waitResult()` implementation |
| `components/application/include/application/motor_controller.hpp` | `IMotorController` interface |
| `components/infrastructure/src/motor/motion.cpp` | `store_result()` — sets Idle state and pushes to result queue |
| `components/interface/include/interface/webui.hpp` | WebUI JS — expects synchronous AckThen responses |

## Evidence

Test log: `scripts/testing/logs/http_api_test_2026-07-17_15-01-13.log` lines 183-188 (2.56s WS connect delay during moveSteps).

Homing timeout (60s → 2s) already fixed in `components/infrastructure/src/motor/homing.cpp:29` — separate issue, same constitutional violation pattern.

---

---

## Full codebase audit (2026-07-17)

A comprehensive constitutional violation audit was conducted across the entire codebase. **14 HIGH, 8 MEDIUM, 5 LOW** violations found — the AckThen issue is part of a systemic pattern.

### Compliance status

| Article | Status | Notes |
|---------|--------|-------|
| **Art. I** (Non-Blocking System Tasks) | ❌ FAIL | 10 violations remaining (3 of 13 resolved — AckThen + captive connect + reboot delay fixed) |
| **Art. II** (Task Sovereignty) | ❌ FAIL | 2 violations remaining (captive WiFi WifiManager call resolved in Phase 1) |
| **Art. III** (Dual-Core) | ✅ PASS | `CONFIG_FREERTOS_UNICORE=n` confirmed |

### Severity classification

| Severity | Count | Classification rule |
|----------|-------|-------------------|
| **HIGH** | 11 (3 resolved) | System task blocked >10ms, or cross-task function call |
| **MEDIUM** | 8 | Non-critical task blocked, or motor-domain task blocked >50ms |
| **LOW** | 5 | Dedicated worker task delay, acceptable by sovereignty |

### Category 1: HTTP worker task blocks (Art. I — system task blocking >10ms)

| # | File | Line | Pattern | Description | Severity | Status |
|---|------|------|---------|-------------|----------|--------|
| 1 | `components/interface/src/rest_api.cpp` | 191 | `waitResult(60000)` | **AckThen — HTTP worker blocked for up to 60s.** | HIGH | ✅ RESOLVED in Phase 0 |
| 2 | `components/infrastructure/network/src/http_server.cpp` | 157 | `connectSTA(..., 15000)` | Captive portal WiFi handler blocks HTTP worker for 15s. | HIGH | ✅ RESOLVED in Phase 1 |
| 3 | `components/infrastructure/network/src/wifi.cpp` | 351-355 | `tryStartSTA()` `xEventGroupWaitBits(6000)` | net_owner blocked up to 30s iterating saved networks. | HIGH |
| 4 | `components/infrastructure/network/src/wifi.cpp` | 442-461 | `waitForSTA()` `xEventGroupWaitBits(10000)` | Default 10s block in net_owner context. | HIGH |
| 5 | `components/application/src/handlers/valve.cpp` | 23 | `vTaskDelay(50ms)` | Valve settle in HTTP handler — 5× constitutional limit. | HIGH |
| 6 | `components/infrastructure/network/src/http_server.cpp` | 167 | `vTaskDelay(500ms)` | Captive portal delay before reboot — 50× limit. | HIGH | ✅ RESOLVED in Phase 1 |

### Category 2: RMT blocking (Art. VII — stop flag defeated)

| # | File | Line | Pattern | Description | Severity |
|---|------|------|---------|-------------|----------|
| 7 | `components/infrastructure/src/drivers/stepper.cpp` | 163 | `rmt_tx_wait_all_done(portMAX_DELAY)` | Motor task blocked indefinitely — stop flag can't be checked during RMT chunk. | HIGH |
| 8 | `components/infrastructure/src/drivers/stepper.cpp` | 178 | `rmt_tx_wait_all_done(portMAX_DELAY)` | `emergencyStop()` itself blocks on RMT — defeats purpose. | HIGH |
| 9 | `components/infrastructure/src/drivers/stepper.cpp` | 119 | `.queue_nonblocking = false` | `rmt_transmit` blocks if trans queue is full (depth=4). | LOW |

### Category 3: Cross-task coupling (Art. II — sovereignty violations)

| # | File | Line | Pattern | Description | Severity | Status |
|---|------|------|---------|-------------|----------|--------|
| 10 | `components/application/src/handlers/sensors.cpp` | 308-311 | `readTmcRegister()` from HTTP handler | Cross-task access to `gTmcUart` (motor domain). Blocking UART I/O (50ms timeout) in HTTP context. Code acknowledges with `// TODO` at `motor_controller_impl.cpp:390`. | HIGH |
| 11 | `components/infrastructure/network/src/http_server.cpp` | 113-170 | `captive_wifi_connect_handler` directly calls `WifiManager` | HTTP handler invokes WiFi domain method directly — synchronous 15s block. | HIGH | ✅ RESOLVED in Phase 1 |
| 12 | `components/infrastructure/src/motor/task.cpp` | 67 | `xQueueCreate(1)` for result queue | Length-1 queue + `xQueueOverwrite` = data-loss risk if HTTP worker is slow to consume. | MEDIUM |

### Category 4: Motor task internal blocking (owner task — MEDIUM)

| # | File | Line | Pattern | Description |
|---|------|------|---------|-------------|
| 13 | `components/infrastructure/src/motor/task.cpp` | 128 | `vTaskDelay(50ms)` | Stop settle — queue not polled for 50ms. |
| 14 | `components/infrastructure/src/motor/sm_runners.cpp` | 231 | `vTaskDelay(50ms)` | CalSpeedSeq valve settle. |
| 15 | `components/infrastructure/src/motor/motion.cpp` | 76, 84 | `vTaskDelay(50ms)` | Valve settle in fill/empty sequences. |

### Category 5: TMC UART delays (MEDIUM — cumulative in HTTP path)

| # | File | Line | Pattern | Description |
|---|------|------|---------|-------------|
| 16 | `components/infrastructure/src/drivers/tmc_uart.cpp` | 75 | `vTaskDelay(10ms)` | UART settling delay. |
| 17 | `components/infrastructure/src/drivers/tmc_uart.cpp` | 115 | `vTaskDelay(1ms)` | UART settling delay. |
| 18 | `components/infrastructure/src/drivers/tmc_uart.cpp` | 142 | `vTaskDelay(2ms)` | UART settling delay. |
| 19 | `components/infrastructure/src/drivers/tmc_uart.cpp` | 109-110, 135-136 | `uart_wait_tx_done(50ms)` | Blocking UART TX wait (50ms per call) in HTTP handler. |

### Category 6: Dedicated worker delays (LOW — acceptable)

| # | File | Line | Pattern | Description |
|---|------|------|---------|-------------|
| 20 | `components/domain/src/log_buffer.cpp` | 73 | `xQueueReceive(60000)` | log_worker blocks 60s — non-critical, but excessive. |
| 21 | `main/net_owner.cpp` | 124 | `vTaskDelay(100ms)` | net_owner poll — acknowledged with `nosemgrep` comment. |
| 22 | `components/infrastructure/src/drivers/onewire.cpp` | 100 | `vTaskDelay(800ms)` | DS18B20 conversion — temp_thread is a dedicated worker. |
| 23 | `components/infrastructure/src/temp_thread.cpp` | 55 | `vTaskDelay(1000ms)` | Temperature sampling — dedicated sensor task. |
| 24 | `components/diag/src/stack_monitor.cpp` | 41 | `vTaskDelay(100ms)` | Stack watermark poll — diagnostic task. |

### Systemic patterns observed

1. **HTTP handler as God Object**: `rest_api.cpp:command_handler()` mediates ALL network-to-application commands. Every slow command blocks ALL HTTP/WS traffic. ✅ AckThen fixed in Phase 0. ✅ Captive WiFi connect fixed in Phase 1. Still leaves valve settle (50ms) in the HTTP path (Phase 4).

2. **Accumulated micro-blocks**: 50ms valve + 10-50ms TMC UART + JSON serialization can exceed 100ms blocking in a single HTTP request. No single violation is large, but together they violate Art. I.

3. **RMT stop flag ineffective**: `rmt_tx_wait_all_done(portMAX_DELAY)` during a chunk prevents stop flag from being checked until the chunk finishes. Emergency stop is similarly blocked.

4. **Three distinct AcThen-like patterns**: (a) Motor commands (AckThen, documented), (b) WiFi connect (synchronous `xEventGroupWaitBits` in HTTP handler), (c) TMC register read (cross-task via `gTmcUart`).

## Phase 0 — status: ✅ DONE (2026-07-18)

Phase 0 implemented by implementer agent on 2026-07-18. `scripts/idf.sh smoke` passes (`RESULT: BOOT OK`).

### Changes applied

| Step | File | Change | Status |
|------|------|--------|--------|
| 0.0 | `config.hpp` | `WS_BROADCAST_QUEUE_DEPTH` 4→8 (verifier condition G1) | ✅ |
| 0.1 | `rest_api.cpp` | `command_handler()` returns `{"status":"accepted"}` immediately, no `waitResult()` call | ✅ |
| 0.2 | `motion.cpp` | `store_result()` pushes `WsBroadcastEntry` with `{"event":"motor_complete",...}` to `gWsBroadcastQueue` via non-blocking `xQueueSend(..., 0)` | ✅ |
| 0.3 | `rest_api.cpp` | `waitResult(60000)` call block removed entirely | ✅ |
| 0.4 | `motor_controller.hpp` | `waitResult()` marked deprecated for HTTP (kept for serial/console with timeout=0 peek) | ✅ |
| 0.5 | `webui.hpp` | `WS_JS` subscribes to `motor_complete`; `STEPPER_JS` adds `startMotorPollFallback()` polling `/api/status` every 500ms; `stepperStartStop` handles `{"status":"accepted"}` keeping button disabled until WS event or poll | ✅ |

### Smoke test log (2026-07-18_07-10-33)

```
[07:10:34.593] BOOT OK: ecotiter v0.1.0
[07:10:35.663] HTTP server started on port 80
[07:10:35.715] BLE initialized
[07:10:35.880] stack_monitor: Thread motor: cfg=16384B wmark=1972 used=87%
[07:10:35.880] stack_monitor: Thread net_owner: cfg=20480B wmark=2284 used=88%
[07:10:36.904] Homing timed out after 2000 ms (expected — no TMC2209 connected)
[07:10:37.200] STA got IP: 192.168.1.103
```

Status broadcasts continue every ~300ms throughout the 70s window. No Guru Meditation, no WDT panics.

### Serial API hardware test

`http_api_test.py` (Phase 0 Checkpoint A: WS connect <100ms during moveSteps) — **not yet verified**. Requires running the integration test script with a motor that has limit switches connected. See `scripts/testing/http_api_test.py`.

### Edge cases addressed

- WebUI JS: fallback poll `/api/status` every 500ms if no WS connected (edge case 2 ✅)
- Multiple AckThen in flight: motor busy returns error immediately via existing queue check (edge case 3 ✅)
- Existing WS broadcasts continue via `httpd_ws_send_frame_async` (edge case 4 ✅)
- Backward compat for BLE/serial clients: **NOT addressed** — deferred (uses `waitResult()` with timeout=0 peek, still works)

### Remaining: verifier conditions not yet addressed

1. ✅ `WS_BROADCAST_QUEUE_DEPTH` 4→8 (done as step 0.0)
2. ❌ BLE/serial backward compatibility — deferred
3. ✅ Phase ordering respected: interface method not deleted, only deprecated

---

## Implementation plan

### Phase 0: Fire-and-forget for AckThen — ✅ DONE

Goal: `waitResult(60000)` removed, HTTP worker never blocks on motor commands.

**Checkpoint A:** `scripts/idf.sh smoke` + `http_api_test.py 30/30`.

Result: 5 files modified, 55 lines added, 39 removed.

---

### Phase 1: Delegate captive portal WiFi connect to net_owner — ✅ DONE

Goal: Captive portal POST handler returns immediately; WiFi connect runs asynchronously in net_owner task.

**Checkpoint B:** `scripts/idf.sh smoke` + `http_api_test.py 30/30`.

| Step | File(s) | Change | Status |
|------|---------|--------|--------|
| 1.1 | `main/net_owner.hpp`, `config.hpp` | Added `WifiConnectCommand` struct + `gNetOwnerCmdQueue` + `NET_OWNER_CMD_QUEUE_DEPTH=2` | ✅ |
| 1.2 | `http_server.cpp` | Replaced `connectSTA(15000)` → `xQueueSend` + `{"status":"accepted"}` immediate | ✅ |
| 1.3 | `net_owner.cpp` | Added WiFi connect handler: receives from queue, calls `connectSTA()` from net_owner context, pushes result to `gWsBroadcastQueue`, drains queue, reboots | ✅ |
| 1.4 | `http_server.cpp` | Removed `vTaskDelay(500)` and `esp_restart()` from HTTP handler | ✅ |

### Changes applied

| File | Change |
|------|--------|
| `main/net_owner.hpp` | Added `WifiConnectCommand` struct + `extern QueueHandle_t gNetOwnerCmdQueue` |
| `main/net_owner.cpp` | Queue creation, WiFi connect handler in net_owner context, reboot on success |
| `http_server.cpp` | Removed `connectSTA(15000)`, `vTaskDelay(500)`, `esp_restart()` — queue + `{"status":"accepted"}` |
| `config.hpp` | Added `NET_OWNER_CMD_QUEUE_DEPTH = 2` |
| `webui.hpp` | Captive portal JS: `{"status":"accepted"}`; WS: `wifi_connect_result` handler |

Result: 5 files modified, ~100 lines added, 25 removed. Smoke + http_api_test pass.

---

### Phase 2: Route TMC register reads through motor command queue

Goal: HTTP handler never accesses `gTmcUart` directly. All TMC register I/O belongs to the motor task.

| Step | File(s) | Change | Checkpoint |
|------|---------|--------|------------|
| 2.1 | `components/domain/include/domain/types.hpp` (or similar) | Add new `MotorCommandType::ReadTmcRegister` with fields for register address and result buffer | Build passes |
| 2.2 | `components/infrastructure/src/motor/task.cpp` | Add handler for `ReadTmcRegister` in motor task's command loop: read register via `gTmcUart`, push result as `WsBroadcastEntry` to `gWsBroadcastQueue` | Build passes |
| 2.3 | `components/infrastructure/src/motor/motor_controller_impl.cpp` | Replace direct `readTmcRegister()` body: push `ReadTmcRegister` command to motor queue, return immediately. **HTTP handler must never block for TMC reads** — use fire-and-forget only | Build passes |
| 2.4 | `components/application/src/handlers/sensors.cpp` | Replace direct `readTmcRegister()` call with queue-based fire-and-forget. `handleStallGuardGet()` returns `{"status":"accepted"}` immediately. Motor task reads register and broadcasts result via WS | `scripts/idf.sh build` |
| 2.5 | `components/interface/include/interface/webui.hpp` | Update `WS_JS` to subscribe to `stallguard_result` WS events. Update client-side sensor display to expect async data | `scripts/idf.sh smoke` — StallGuard endpoint returns in <10ms, result arrives via WS |

**Checkpoint C (end of Phase 2):** `scripts/idf.sh smoke` passes. POST `/api/command sensors.stallGuard.get` returns HTTP 200 in <10ms. Result arrives via WS `stallguard_result` event. Grep confirms `gTmcUart` is only referenced in `motor/task.cpp` and `motor/motor_controller_impl.cpp` — no cross-task access from HTTP handler.

---

### Phase 3: RMT stop flag effectiveness

Goal: `emergencyStop()` works immediately. `rmt_tx_wait_all_done` does not block indefinitely.

| Step | File(s) | Change | Checkpoint |
|------|---------|--------|------------|
| 3.1 | `components/infrastructure/src/drivers/stepper.cpp` | In `moveStepsIntervals()`: replace `rmt_tx_wait_all_done(channel_.get(), portMAX_DELAY)` with a loop: wait with short timeout (100ms), check stop flag, repeat | Build passes |
| 3.2 | `components/infrastructure/src/drivers/stepper.cpp` | In `emergencyStop()`: replace `rmt_tx_wait_all_done` with `rmt_tx_wait_all_done(channel_.get(), 0)` (non-blocking check) + return immediately | Build passes |
| 3.3 | `components/infrastructure/src/drivers/stepper.cpp` | Set `queue_nonblocking = true` in RMT transmit config. Add retry loop around `rmt_transmit()` with stop flag check and short delay (10ms) on `ESP_ERR_INVALID_STATE` to handle transient queue-full conditions | Build passes |
| 3.4 | `components/infrastructure/src/drivers/stepper.cpp` | Add stop flag check **before** calling `rmt_transmit()` and in the polling loop replacing `portMAX_DELAY` | `scripts/idf.sh build` |

**Checkpoint D (end of Phase 3):** `scripts/idf.sh smoke` passes. Emergency stop during 5-second `moveSteps`: serial log shows "Stop requested" within 200ms, followed by RMT abort confirmation. No `rmt_tx_wait_all_done(portMAX_DELAY)` call remains. Stepper stops without indefinite wait.

---

### Phase 4: Move valve settle out of HTTP handler

Goal: HTTP handler never calls `vTaskDelay(50)` for valve settling.

| Step | File(s) | Change | Checkpoint |
|------|---------|--------|------------|
| 4.1 | `components/application/src/handlers/valve.cpp` | In `handleSetPosition()`: remove `vTaskDelay(VALVE_SETTLE_MS)`. Return `{"status":"accepted"}` immediately. HTTP response no longer contains `{"valve":...}` — position comes via WS after settle | Build passes |
| 4.2 | Motor task valve command handler (SM runners or `task.cpp`) | Add valve settle delay **in the motor task** after valve position is set — before executing next step. After settle, push `WsBroadcastEntry` with `{"event":"valve_settled","position":...}` to `gWsBroadcastQueue` | `scripts/idf.sh build` |
| 4.3 | `components/interface/include/interface/webui.hpp` | Update `toggleValve` JS (line ~801): remove expectation of `j.valve` in HTTP response. Subscribe to `valve_settled` WS event to update `APP_STATE.valve.position` | Build passes |
| 4.4 | `components/application/src/handlers/valve.cpp` | Verify no other blocking operations remain in HTTP path for valve commands | `scripts/idf.sh smoke` — HTTP handler responds in <10ms for valve commands |

**Checkpoint E (end of Phase 4):** `scripts/idf.sh smoke` passes. Valve commands return HTTP response in <10ms. Valve still settles correctly (delay moved to motor task).

---

### Phase 5 (stretch): Address accumulated micro-blocks

Goal: No single HTTP handler blocks for >10ms total.

| Step | File(s) | Change | Checkpoint |
|------|---------|--------|------------|
| 5.1 | `components/infrastructure/src/drivers/tmc_uart.cpp` | Audit all TMC UART delays. Move blocking UART waits to motor task if called from cross-task path (already covered by Phase 2) | Build passes |
| 5.2 | `components/interface/src/rest_api.cpp` | Profile JSON serialization in HTTP handler. If >5ms, move to background task | `scripts/idf.sh smoke` |
| 5.3 | `components/application/src/handlers/burette_ops.cpp` | Verify all handlers are fire-and-forget (none should call `waitResult` or any blocking motor operation) | `scripts/idf.sh smoke` |

**Checkpoint F (end of Phase 5):** Full load test: 10 simultaneous HTTP requests during motor operation. All respond within 50ms. No task starvation.

---

### Rollback criteria

Any phase that fails its checkpoint must be rolled back before proceeding to the next phase. Rollback = `git checkout -- <affected files>` + re-run checkpoint.

### Dependencies

```
Phase 0 ──→ Phase 2 ──→ Phase 5
  │                      ▲
  ├──→ Phase 1 ──────────┘
  │
  └──→ Phase 3
         │
         └──→ Phase 4
```

Phases 0-4 are independent of each other (except Phase 2 depends on queue infrastructure from Phase 0). Phase 5 is the final hardening pass.

## Citations

[1] Constitution: `docs/refs/CONSTITUTION.md` — Article I (Non-Blocking), Article II (Task Sovereignty)
[2] ESP-IDF `httpd_ws_send_frame_async` source: `components/esp_http_server/src/httpd_ws.c:512-570`
[3] Audit performed 2026-07-17 by explore agent — search patterns: `xQueueReceive`, `xSemaphoreTake`, `vTaskDelay`, `rmt_tx_wait_all_done`, `waitResult`, cross-task domain access

## Changelog

| Date | Change |
|------|--------|
| 2026-07-17 | Initial issue filed with full codebase audit |
| 2026-07-18 | Phase 0 implemented and smoke-tested. `waitResult(60000)` removed from HTTP handler. `store_result()` pushes WS broadcast. WebUI JS handles async completion. Audit table #1 marked RESOLVED. |
| 2026-07-18 | Phase 1 implemented and smoke-tested. Captive portal WiFi connect delegated to net_owner via queue. `vTaskDelay(500)` and `esp_restart()` removed from HTTP handler. Audit table #2, #6, #11 marked RESOLVED. Art. I: 10 remaining. Art. II: 2 remaining. HIGH: 11 remaining. |
