---
type: Plan
title: Move MotorCommand and MotorCommandType to domain layer
description: Eliminate application-layer dependency on infrastructure/motor_task.hpp by moving MotorCommand, MotorCommandType, and sub-types to domain
tags: [srp, refactoring, layering, pending]
timestamp: 2026-07-17
status: pending
---

# Move `MotorCommand` / `MotorCommandType` to domain layer

## Summary

After Phase 3 of ISSUE-004 (SRP violations), 4 application-layer files still include `infrastructure/motor_task.hpp` because `MotorCommand` and `MotorCommandType` are defined in the infrastructure layer. These are pure data types with no hardware dependency — they belong in the domain layer.

**Current state:**
- `infrastructure/motor_task.hpp` defines `MotorCommandType` (enum), `MotorCommand` (struct), and sub-types (`StartRinseParams`, `StartCalDoseParams`, `StartCalSpeedParams`, `StartCalSpeedSeqParams`)
- `application/send_motor_command.hpp` includes `infrastructure/motor_task.hpp` (valid — it wraps the queue, but the type comes from infra)
- 4 handler files construct `infrastructure::MotorCommand` objects directly
- `motor_controller_impl.cpp` and `task.cpp` in infrastructure also use these types (correct — their layer)

**Target state:**
- `domain/motor_command.hpp` defines all types in `ecotiter::domain` namespace
- `infrastructure/motor_task.hpp` adds `using MotorCommand = domain::MotorCommand;` etc. for backward compatibility
- Application handlers migrate to `domain::MotorCommand` (or use `IMotorController::sendCommand()` with JSON, eliminating the need to construct structs at all)

**Dual approach:** Either migrate handlers to `IMotorController::sendCommand("json")` (eliminating struct dependency entirely) or move types to domain. The plan explores both.

## Affected files

### Types source
| File | Role |
|------|------|
| `components/infrastructure/include/infrastructure/motor_task.hpp` | Currently defines `MotorCommandType`, `MotorCommand`, `StartRinseParams`, `StartCalDoseParams`, `StartCalSpeedParams`, `StartCalSpeedSeqParams` |
| `components/domain/include/domain/motor_command.hpp` | **New** — will define the same types in `ecotiter::domain` namespace |

### Files that construct `MotorCommand` directly (candidates for migration)
| File | Lines | Usage |
|------|-------|-------|
| `application/src/handlers/burette_ops.cpp` | 25, 47, 83, 104, 153, 160, 173, 185, 210, 225, 240, 255 | 12 sites constructing MotorCommand and passing to `sendMotorCommand()` |
| `application/src/handlers/burette_cal.cpp` | 184 | 1 site constructing MotorCommand |
| `application/src/handlers/sensors.cpp` | 335-336 | 1 site constructing MotorCommand for SetStallThreshold |
| `application/include/application/send_motor_command.hpp` | 7 | Accepts `infrastructure::MotorCommand&` |

### Files that use types internally (mostly ok)
| File | Usage | Notes |
|------|-------|-------|
| `infrastructure/src/motor/motor_controller_impl.cpp` | 232-358 | Builds MotorCommand from JSON strings — can stay as infra type or use domain type via alias |
| `infrastructure/src/motor/task.cpp` | 60, 107-218 | Switch on MotorCommandType. The motor task is in infra — using a domain type for commands is correct |

## Steps

### Option A — Full abstraction via `IMotorController::sendCommand()`

Eliminate all direct `MotorCommand` construction in application handlers by migrating every call site to `controller->sendCommand(jsonString)`.

**Step A1 — Extend `IMotorController::sendCommand()` to cover all command types**
Currently `sendCommand()` handles: stop, emergencyStop, home, setDirection, setSpeed, setAccel, moveSteps, setStallThreshold. Verify coverage for: fill, empty, doseVolume, rinse, calRun, calRunSpeed.

**Step A2 — Migrate application handlers**
- `burette_ops.cpp` — replace all 12 `sendMotorCommand(cmd)` with `controller->sendCommand(json)`
- `burette_cal.cpp` — replace `sendMotorCommand(cmd)` with `controller->sendCommand(json)` 
- `sensors.cpp` — already uses IMotorController for StallGuard threshold (Step 8), just remove the MotorCommand path
- `send_motor_command.hpp` — delete this file entirely (no longer needed)

**Step A3 — Remove types from infrastructure**
- `infrastructure/motor_task.hpp` — delete `MotorCommandType`, `MotorCommand`, sub-types, and the `using SmResult` alias (SmResult already in domain)
- Delete `#include "domain/types.hpp"` (no longer needed — types moved to domain)

**Pros:** Cleanest layering — application never touches motor command structs.
**Cons:** More serialization overhead (JSON parsing on each command), more changes in motor_controller_impl.cpp.

### Option B — Move types to domain, keep struct construction in handlers

Move `MotorCommandType`, `MotorCommand`, and sub-types to a new `domain/motor_command.hpp`. Add backward-compatibility aliases in `infrastructure/motor_task.hpp`. Update all references.

**Step B1 — Create `domain/motor_command.hpp`**
Define in `ecotiter::domain` namespace:
```cpp
enum class MotorCommandType : uint8_t { ... };
struct StartRinseParams { ... };
struct MotorCommand { ... };
```

**Step B2 — Add aliases in `infrastructure/motor_task.hpp`**
```cpp
using MotorCommandType = domain::MotorCommandType;
using MotorCommand = domain::MotorCommand;
```

**Step B3 — Migrate handlers to `domain::MotorCommand`**
- `burette_ops.cpp` — change `infrastructure::MotorCommand` → `domain::MotorCommand`
- `burette_cal.cpp` — same
- `sensors.cpp` — same
- `send_motor_command.hpp` — change parameter type to `const domain::MotorCommand&`

**Step B4 — Remove redundant includes**
- Remove `#include "infrastructure/motor_task.hpp"` from handler files when no longer needed

**Pros:** Minimal changes (type aliases). No JSON serialization overhead.
**Cons:** Application layer still constructs infrastructure-like structs. Not a complete decoupling.

## Verification

For both options:
1. `scripts/idf.sh build` — 0 errors, 0 warnings
2. `scripts/idf.sh test` — all unit tests pass (same count, no regressions)
3. `scripts/idf.sh smoke` — BOOT OK, no crashes, no panics
4. `scripts/idf.sh tidy` — clang-tidy clean (0 warnings)

## Related documents

- [ISSUE-004 SRP violations (archived)](../../issues/archived/ISSUE-004-srp-violations.md) — previous SRP refactoring that exposed this remaining issue
- [Motor task types](../../components/infrastructure/include/infrastructure/motor_task.hpp) — current location of MotorCommand/MotorCommandType
- [Coding style: Named constants](../refs/coding_style.md) — conventions for type definitions
