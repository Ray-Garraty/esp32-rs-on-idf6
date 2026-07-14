---
type: Known Issue
title: "log_worker and net_owner critically low stack headroom"
description: "log_worker at 96% (248B free) and net_owner at 92% (1.2KB free) — resolved by increasing stacks"
tags: [stack, log_worker, net_owner, GR-6, GR-14]
timestamp: 2026-07-14
status: solved
resolved: 2026-07-14
---

# ISSUE-001: log_worker and net_owner — critically low stack headroom

**Severity:** Critical  
**Detected:** 2026-07-13, serial log `serial_2026-07-13_07-25-02.log`  
**Resolved:** 2026-07-14  
**Related rule:** AGENTS.md §GR-6 (Stack Budget Is Law), §GR-14 (Task Independence)

## Symptom

Stack monitor report at T+29s (before fix):

```
I (29035) stack_monitor: Thread net_owner: cfg=16384B wmark=1284 used=92%
W (29037) stack_monitor: Thread net_owner: LOW STACK! 92% used
I (29050) stack_monitor: Thread log_worker: cfg=8192B wmark=248 used=96%
W (29051) stack_monitor: Thread log_worker: LOW STACK! 96% used
```

- **log_worker**: 8192 B stack, 248 B free (96 % used)
- **net_owner**: 16384 B stack, 1284 B free (92 % used)

## Root Cause

**log_worker (96 %):** 8 KB stack insufficient for JSON formatting + FreeRTOS call depth. At 248 B free, any deeper call chain would overflow into the stack guard (`0xa5a5a5a5` corruption pattern).

**net_owner (92 %):** 16 KB stack insufficient for WiFi + HTTP + BLE init plus the service loop with WsSendQueue drain.

## Evidence

- Log file: `logs/serial_2026-07-13_07-25-02.log` lines 496-501
- Thread budgets from `docs/refs/project.md`

## Resolution

Applied 2026-07-14 — stack sizes increased in 3 files:

| File | Change |
|------|--------|
| `components/domain/include/domain/types.hpp` | `NET_OWNER_STACK`: 16384→**20480**; added `LOG_WORKER_STACK = 12288` |
| `main/main.cpp:257,261` | Hardcoded `8192` → `ecotiter::domain::LOG_WORKER_STACK` (2 places) |
| `docs/refs/project.md` | Stack budgets replaced with `domain::*` constant references |

## Verification

- [x] `scripts/idf.sh build` — 0 errors, 0 warnings
- [x] `scripts/idf.sh test` — 7080 assertions, 264 test cases, all passed
- [x] `scripts/idf.sh smoke` — 30s on real ESP32-S3: no Guru Meditation, no crashes
- [x] Stack monitor after fix: **net_owner 88%** (was 92%), **log_worker 87%** (was 96%)
- [x] No `LOW STACK!` warnings in serial log after fix

## After-Fix Stack Report

```
I (2067) stack_monitor: Thread net_owner: cfg=20480B wmark=2332 used=88%
I (2099) stack_monitor: Thread log_worker: cfg=12288B wmark=1488 used=87%
```

Both threads below 90% — headroom restored.
