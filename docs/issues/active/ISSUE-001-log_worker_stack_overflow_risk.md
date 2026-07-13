---
type: Known Issue
title: "log_worker and net_owner critically low stack headroom"
description: "log_worker at 96% (248B free) and net_owner at 92% (1.2KB free) stack usage detected by stack monitor"
tags: [stack, log_worker, net_owner, GR-6, GR-14]
timestamp: 2026-07-13
---

# ISSUE-001: log_worker and net_owner — critically low stack headroom

**Severity:** Critical  
**Detected:** 2026-07-13, serial log `serial_2026-07-13_07-25-02.log`  
**Related rule:** AGENTS.md §GR-6 (Stack Budget Is Law), §GR-14 (Task Independence)

## Symptom

Stack monitor report at T+29s:

```
I (29035) stack_monitor: Thread net_owner: cfg=16384B wmark=1284 used=92%
W (29037) stack_monitor: Thread net_owner: LOW STACK! 92% used
I (29050) stack_monitor: Thread log_worker: cfg=8192B wmark=248 used=96%
W (29051) stack_monitor: Thread log_worker: LOW STACK! 96% used
```

- **log_worker**: 8192 B stack, 248 B free (96 % used)
- **net_owner**: 16384 B stack, 1284 B free (92 % used)

## Root Cause (suspected)

**log_worker (96 %):** Stack budget is 8 KB. At 248 B free, any additional call depth or a slightly larger JSON payload will overflow into the stack guard, causing silent memory corruption (see LL-001 stack overflow pattern `0xa5a5a5a5`).

Per GR-14, log_worker must NEVER call network functions (`httpd_ws_send_frame_async`, `broadcastWsEvent`). If it does, the 8 KB stack is insufficient. Even without network calls, JSON formatting in log_worker may be consuming too much stack.

**net_owner (92 %):** Stack budget is 16 KB. With 1.2 KB free, the margin is thin. net_owner runs WiFi init, HTTP server, BLE init, and the main event loop — any new feature added to this thread will risk overflow.

## Evidence

- Log file: `logs/serial_2026-07-13_07-25-02.log` lines 496-501
- Thread budgets from `docs/refs/project.md`

## Proposed Fix

1. **log_worker**: Increase stack from 8 KB to 12 KB (verify with `uxTaskGetStackHighWaterMark()`), OR audit log_worker code path to ensure no network calls and reduce JSON formatting depth
2. **net_owner**: Increase stack from 16 KB to 20 KB
3. Update `docs/refs/project.md` with new budgets

## Acceptance Criteria

- [ ] Stack monitor reports < 80 % usage for both threads after fix
- [ ] 30 s smoke test: no stack canary corruption, no `0xa5a5a5a5` in backtrace
- [ ] All existing functionality continues to work

## Status

open
