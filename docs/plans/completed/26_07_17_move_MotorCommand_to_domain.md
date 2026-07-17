---
type: Plan
title: Move MotorCommand and MotorCommandType to domain layer
description: Eliminate application-layer dependency on infrastructure/motor_task.hpp by moving MotorCommand, MotorCommandType, and sub-types to domain
tags: [srp, refactoring, layering, completed]
timestamp: 2026-07-17
status: completed
---

# Move `MotorCommand` / `MotorCommandType` to domain layer

## Summary

After Phase 3 of ISSUE-004 (SRP violations), 4 application-layer files still include `infrastructure/motor_task.hpp` because `MotorCommand` and `MotorCommandType` are defined in the infrastructure layer. These are pure data types with no hardware dependency — they belong in the domain layer.

**Previous state:**
- `infrastructure/motor_task.hpp` defines `MotorCommandType` (enum), `MotorCommand` (struct), and sub-types (`StartRinseParams`, `StartCalDoseParams`, `StartCalSpeedParams`, `StartCalSpeedSeqParams`)
- `application/send_motor_command.hpp` includes `infrastructure/motor_task.hpp`
- 4 handler files construct `infrastructure::MotorCommand` objects directly

**Target state:**
- `domain/motor_command.hpp` defines all types in `ecotiter::domain` namespace
- `infrastructure/motor_task.hpp` adds `using MotorCommand = domain::MotorCommand;` etc. for backward compatibility
- Application handlers use `domain::MotorCommand`

## Chosen approach: Option B

Move `MotorCommandType`, `MotorCommand`, and sub-types to a new `domain/motor_command.hpp`. Add backward-compatibility aliases in `infrastructure/motor_task.hpp`. Update all references.

**Rationale:** Option B follows proper DDD — domain defines the types, application constructs them, infrastructure implements ports. Option A (JSON serialization) was rejected because it would add coupling through a string format, parsing overhead, and duplicate validation.

## Changes made

### Files created

| File | Lines | Purpose |
|------|-------|---------|
| `components/domain/include/domain/motor_command.hpp` | 75 | Defines `MotorCommandType`, `MotorCommand`, sub-types in `ecotiter::domain` |

### Files modified

| File | Change |
|------|--------|
| `components/infrastructure/include/infrastructure/motor_task.hpp` | Replaced inline type definitions with `using` aliases + `#include "domain/motor_command.hpp"` |
| `components/application/include/application/send_motor_command.hpp` | Parameter type: `infrastructure::MotorCommand&` → `domain::MotorCommand&` |
| `components/application/src/handlers/burette_ops.cpp` | 13 occurrences `infrastructure::` → `domain::`; removed `#include "infrastructure/motor_task.hpp"` |
| `components/application/src/handlers/burette_cal.cpp` | `infrastructure::MotorCommand` → `domain::MotorCommand`; removed `#include "infrastructure/motor_task.hpp"` |
| `components/application/src/handlers/sensors.cpp` | `infrastructure::MotorCommand` → `domain::MotorCommand`; kept `#include "infrastructure/motor_task.hpp"` (needs `gMotorCmdQueue`) |

### Files unchanged

4 infrastructure files (`internal.hpp`, `motion.cpp`, `sm_runners.cpp`, `task.cpp`) continue to compile via backward-compatible aliases.

## Verification results

| Check | Result |
|-------|--------|
| `scripts/idf.sh build` | ✅ 0 errors, 0 warnings |
| `scripts/idf.sh test` | ✅ 248 tests, 791 assertions, all pass |
| `scripts/idf.sh tidy` | ✅ 0 warnings |
| `scripts/idf.sh smoke` | ✅ BOOT OK on real ESP32-S3 hardware |

## Related documents

- [ISSUE-004 SRP violations (archived)](../../issues/archived/ISSUE-004-srp-violations.md)
- [Motor task types](../../components/infrastructure/include/infrastructure/motor_task.hpp)
- [Domain motor command](../../components/domain/include/domain/motor_command.hpp)
- [Coding style: Named constants](../refs/coding_style.md)
