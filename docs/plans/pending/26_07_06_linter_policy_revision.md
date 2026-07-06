---
type: Plan
title: Linter policy revision — replace deny-level cast lints with warn
description: Remove #![deny(clippy::cast_*)] from lib.rs, promoting to warn in Cargo.toml, and delete ~60 redundant #[allow(cast_*)] annotations across the codebase.
tags: [linter, clippy, code-quality, refactoring]
timestamp: 2026-07-06
status: pending
---

# Linter policy revision — eliminate all unnecessary `#[allow]`

## Summary

### Context

Every `#[allow()]` / `#[expect()]` annotation is a [broken
window](https://en.wikipedia.org/wiki/Broken_windows_theory). It signals
"this rule doesn't apply here" and makes the next one easier to add
without thought. A codebase where suppressions are commonplace has already
lost its linting culture — the suppressions become invisible noise, and
real issues pass unnoticed.

The project currently has ~97 suppression attributes across 83 source
files. This is not a manageable amount of exceptions — it is a broken
culture. The goal is to reduce suppressions to a **rare, intentional few**,
each one a documented decision that stands out in code review.

### What went wrong

The project `deny`s four cast lints at the crate level:
`cast_possible_truncation`, `cast_sign_loss`, `cast_precision_loss`,
`cast_lossless`. This forces every function that performs an intentional,
bounded, and safe numeric cast to carry an `#[allow(cast_*)]` annotation.
The result is ~60 such annotations across 83 source files — noise.

The `deny` policy was meant to enforce safety, but it backfired:
- 60 suppressions became invisible routine, reviewed by nobody
- The real protection (domain bounds, clamping) is in the code, not in
  the lint attribute
- New code copies `#[allow]` from neighbouring functions without thought

### What this plan does

1. Removes the 4 `#![deny(cast_*)]` lines from `src/lib.rs`.
2. Adds the same 4 lints at `warn` level in `Cargo.toml` `[lints.clippy]`.
3. Deletes all ~60 `#[allow(cast_*)]` / `#[expect(cast_*)]` annotations.
4. Reaches **0 clippy warnings** by fixing all genuine issues.
5. Audits **every single** remaining `#[allow()]` / `#[expect()]` annotation
   across the codebase. Each must be justified, documented with an English
   inline comment, and either retained, removed, or replaced with `#[expect]`.
6. Replaces `#[allow]` with `#[expect]` where applicable (Rust 1.80+) so
   that stale suppressions automatically produce a warning.

The end state: **all suppressions are rare, intentional, and documented.**
The count drops from ~97 to a number in the tens, each with a comment.

## Steps / Execution log

### Step 1 — Remove `deny` from `src/lib.rs`

Edit `src/lib.rs`, delete lines 18–21:

```rust
#![deny(clippy::cast_possible_truncation)]
#![deny(clippy::cast_sign_loss)]
#![deny(clippy::cast_precision_loss)]
#![deny(clippy::cast_lossless)]
```

### Step 2 — Add `warn` to `Cargo.toml`

In `[lints.clippy]` (line 48), insert:

```toml
cast_possible_truncation = "warn"
cast_sign_loss = "warn"
cast_precision_loss = "warn"
cast_lossless = "warn"
```

### Step 3 — Remove all `#[allow(cast_*)]` / `#[expect(cast_*)]` annotations

Use `rg` to find every occurrence of `#[allow(clippy::cast_` and
`#[expect(clippy::cast_` across all `*.rs` files and strip them.

Expected deletions: ~60 annotations covering:
- `#[allow(clippy::cast_possible_truncation)]` (~35 occurrences)
- `#[allow(clippy::cast_sign_loss)]` (~18 occurrences)
- `#[allow(clippy::cast_precision_loss)]` (~10 occurrences)
- `#[allow(clippy::cast_lossless)]` (~2 occurrences)

Files affected: `calibration.rs`, `planner.rs`, `motor_state.rs`,
`system.rs`, `serial.rs`, `scheduler.rs`, `esp_safe.rs`, `black_box.rs`,
`heap_snapshot.rs`, `stack_monitor.rs`, `tick_watchdog.rs`, `ramp.rs`,
`adc_cal.rs`, `sensors.rs`, `onewire.rs`, `adc.rs`, and potentially others.

### Step 4 — Build and check all remaining warnings

```bash
scripts/build.sh clippy 2>&1 | tee /tmp/clippy_output.txt
```

Collect every warning line (cast warnings are expected, see Step 5). Also
check for any new warnings from the `deny` → `warn` change:

- Are there cast warnings on sites that **were not** previously annotated
  with `#[allow(cast_*)]`? These are genuine findings — investigate.
- Are there warnings from other lint groups that were previously hidden
  by the cast-lint noise? Capture them.

### Step 5 — Fix all clippy warnings → 0 warnings

Every clippy warning remaining after Step 3 must be resolved:

1. **A genuine code issue** — fix the code (e.g., add `#[must_use]`,
   remove unused variable, simplify expression, fix cast with `saturating_*`).
2. **A known acceptable pattern** — add `#[allow(lint_name)]` with an
   inline **justification comment** explaining why. No bare `#[allow]`
   without a comment.
3. **A false positive** — add `#[allow(lint_name)]` with a `// clippy
   false positive: <explanation>` comment.

The bar: **`scripts/build.sh clippy` exits with 0 warnings.**

### Step 6 — Audit all remaining `#[allow()]` / `#[expect()]` annotations

After removing cast suppressions (Step 3) and fixing all warnings (Step 5),
there will still be `#[allow()]` / `#[expect()]` annotations from:

- Crate-level allows in `lib.rs` (lines 30–35) and other crate roots
- Pre-existing justified suppressions (FFI, boot-time, Phase 3 stubs, etc.)
- New suppressions added in Step 5 for acceptable patterns

Every single one must be reviewed. The bar is strict — these are broken windows,
not routine paperwork:

| Criterion | Requirement |
|---|---|
| Comment | Every `#[allow()]` / `#[expect()]` **must** have an English `//` comment within the preceding 3 lines explaining why this specific code path requires suppression. No exceptions. |
| `#[expect]` vs `#[allow]` | Prefer `#[expect()]`. Use `#[allow()` ONLY when the lint may or may not fire depending on context or compilation target. If the lint always fires on this code, it must be `#[expect]`. |
| Still needed? | Remove any suppression where the underlying lint no longer fires (stale allow). |
| Overly broad? | Shrink to the narrowest scope: prefer item-level over module-level, prefer specific lint over lint group. A crate-level `#![allow()]` must be justified in the module's doc comment. |
| Broken windows audit | Every suppression MUST justify why the code cannot be changed instead. If the code CAN be changed to satisfy the lint, change it — do not suppress. |

Specific items from the audit requiring attention:

| Action | Item | Reason |
|---|---|---|
| Fix | `ble.rs:222` `#[allow(unused)]` | Replace with `#[expect(unused)]` or remove dead code |
| Fix | `ramp.rs:207` `#[allow(clippy::disallowed_types)]` | Remove `compute_ramp()` or mark `#[cfg(test)]` |
| Document | `lib.rs:30-35` (6 crate-level allows) | Add comment block per lint |
| Document | `logger.rs:6` `module_name_repetitions` | Add justification comment |
| Document | `esp_mutex.rs:13` `result_unit_err` | Add comment referencing upstream pattern |
| Document | `main.rs:45` `expect_used, too_many_lines` | Add justification comment |
| Document | `black_box.rs:212` `missing_const_for_thread_local` | Add Rust limitation note |
| Document | `stack_monitor.rs:33` `declare_interior_mutable_const` | Add const+Atomic pattern note |
| Document | `burette.rs:133` `needless_pass_by_value` | Add const fn + Copy reasoning |
| Document | `esp_safe.rs:498` `cast_sign_loss` | Add justification comment |

## Verification

| Check | Command | Expected result |
|---|---|---|
| No build errors | `scripts/build.sh` | Exit code 0 |
| No `#[allow(cast_*)]` remain | `rg '#\[allow\(clippy::cast_' src/` | 0 matches |
| No `#![deny(cast_*)]` in lib.rs | `rg 'cast_' src/lib.rs` | Only `#![warn(...)]` or nothing |
| 0 clippy warnings | `scripts/build.sh clippy 2>&1` | Exit code 0, no warnings |
| Every `#[allow]` has a comment | `rg '#\[allow\(' src/ --no-filename`; for each match, confirm a `//` line exists within preceding 3 lines | 100% compliance |
| No `#[allow]` where `#[expect]` suffices | Manual review of each suppression | The lint always fires → must be `#[expect]` |
| No `#[allow(unused)]` remain | `rg '#\[allow\(unused\)\]' src/` | 0 matches (replaced by `#[expect]` or removed) |
| Total suppression count | `rg '#\[allow\(' src/ --no-filename | wc -l` + `rg '#\[expect\(' src/ --no-filename | wc -l` | Significantly lower than 97 (target <40) |

## Files affected

| File | Change |
|---|---|
| `src/lib.rs` | Remove lines 18–21 (`#![deny(clippy::cast_*)]`); add comment block for crate-level allows (lines 30–35) |
| `Cargo.toml` | Add 4 cast lints at `warn` level in `[lints.clippy]` |
| `src/**/*.rs` (various) | Delete `#[allow(clippy::cast_*)]` annotations (~60) |
| `src/**/*.rs` (various) | Fix clippy warnings found in Step 5; add documented suppressions where justified |
| `src/infrastructure/network/ble.rs` | Fix `#[allow(unused)]` → `#[expect(unused)]` or remove dead code |
| `src/infrastructure/drivers/stepper/ramp.rs` | Fix `#[allow(clippy::disallowed_types)]` — remove `compute_ramp()` or mark `#[cfg(test)]` |
| `src/logger.rs` | Add comment for `#[allow(clippy::module_name_repetitions)]` |
| `src/esp_mutex.rs` | Add comment for `#[allow(clippy::result_unit_err)]` |
| `src/main.rs` | Add comment for `#[allow(clippy::expect_used, clippy::too_many_lines)]` |
| `src/diag/black_box.rs` | Add comment for `#[allow(clippy::missing_const_for_thread_local)]` |
| `src/diag/stack_monitor.rs` | Add comment for `#[allow(clippy::declare_interior_mutable_const)]` |
| `src/domain/burette.rs` | Add comment for `#[allow(clippy::needless_pass_by_value)]` |
| `src/esp_safe.rs` | Add comment for cast sign loss at line 498 |

No behavioural changes, no logic changes — purely linter attribute churn
reduction and policy tightening.
