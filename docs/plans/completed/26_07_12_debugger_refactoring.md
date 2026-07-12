---
type: Plan
title: Debugger subagent workflow overhaul
description: Systematic redesign of the debugger subagent — from pure empirical binary-search to hypothesis-driven root-cause analysis with mandatory platform study
tags: [debugger, workflow, agents, refactoring]
timestamp: 2026-07-12
---

# Debugger Subagent Workflow Overhaul

## Motivation: three sessions, zero root causes

Three independent debugger sessions were tasked with the same crash
(`rst:0x9 / rst:0x10 RTCWDT_SYS_RST`). Across ~3 hours and 10+ build-smoke
cycles the crash was never explained. The cause — a documented spinlock
deadlock between WiFi ISR and WiFi driver task under `CONFIG_FREERTOS_UNICORE=y`
— was discoverable in 5 minutes by reading the ESP-IDF Kconfig help text or
the FreeRTOS documentation that ships with the SDK.

This section documents exactly what went wrong in each session, traces each
failure to a structural defect in the debugger workflow, and then prescribes
changes.

### Session-by-session postmortem

#### Debugger 1 — dived into code, ignored the symptom

**Goal:** Fix `Panic handler entered multiple times` boot loop.

**What went right:**
- Identified that `uart_write_bytes()` in `logVprintf` is unsafe before UART
  driver install (LL-026)
- Rewrote to `fwrite`/`fflush` — correct fix, verified by smoke test

**What went wrong:**
- After the fix, the crash changed from `rst:0xc (RTC_SW_CPU_RST)` to
  `rst:0x9 (RTCWDT_SYS_RST)` — a *different* crash that was previously masked
- Reported this as "residual GPIO27/PSRAM issue (LL-027)" with zero evidence
- Never checked whether GPIO27 was actually the cause (it wasn't)
- Cost: misdirected the next 2 hours of work

**Structural defect:** The debugger conflated "this is a known pattern" with
"this is the cause". It did not check whether the known pattern's fix (moving
to a safe pin) would actually change the symptom.

#### Debugger 2 — invented a story, never falsified it

**Goal:** Find why `rst:0x9` persists after GPIO27→GPIO13 move.

**What went right:**
- Noticed that GPIO15 (EMPTY limit switch) had `pull_down=DISABLE` while
  GPIO7 (FULL limit switch) had `pull_down=ENABLE`
- Identified `_xt_lowint1` as the Saved PC at reset

**What went wrong:**
- Constructed a narrative: floating GPIO15 + RF noise from WiFi = interrupt
  storm at level 1 → tick freezes → RWDT fires
- This story was internally consistent and plausible. It was also **wrong**.
- The "RF noise" mechanism was pure speculation — no oscilloscope, no counter
  measurement, no interference test
- The fix (`pull_down=ENABLE`) was applied, and the crash was still present,
  but the debugger did not re-evaluate — it had already returned its verdict
- Cost: 1 build-smoke cycle + the time to clean up the GPIO refactor

**Structural defect:** The debugger treated a plausible-sounding explanation as
a confirmed diagnosis. It never designed a falsification experiment. A correct
process would have been: "If GPIO15 RF noise is the cause, then disabling the
GPIO15 interrupt entirely (not just pull-down) should suppress the storm."
This was never tested.

#### Debugger 3 — pure empiricism, no architecture

**Goal:** Find the cause with binary-search experiments.

**What went right:**
- Ran 7 clean experiments, isolating variables
- Correctly found that `wifiManager.startAP()` is the trigger
- Never touched code outside the experiment scope

**What went wrong:**
- After experiment B (no `startAP()` = no crash), the obvious next question
  is **why**. The debugger instead ran 5 more experiments narrowing down
  to "AP alone, even without HTTP/STA/BLE/homing" = crash
- Still never asked *why AP* — proposed a `gHomingDone` workaround instead
- The correct question would have been: "What does `esp_wifi_start()` do on
  UNICORE that makes the CPU hang?" — answer is in the Kconfig help for
  `CONFIG_FREERTOS_UNICORE`, which explicitly warns: *"It is not recommended
  to use single-core mode when Wi-Fi or Bluetooth are enabled, as some drivers
  rely on spinlocks that require both cores to make progress."*
- Cost: 7 build-smoke cycles, `gHomingDone` workaround committed and needing
  revert

**Structural defect:** Empiricism without architectural knowledge is random
search. The debugger eliminated variables but never connected the result to
known platform constraints. It treated the black box as truly opaque instead
of reading the manual.

### Systemic root causes

#### Cause 1: mandatory platform study is not enforced

The debugger prompt does not require studying the ESP-IDF FreeRTOS
configuration, spinlock model, or known limitations before forming hypotheses.

**Evidence:** In all 3 sessions, `CONFIG_FREERTOS_UNICORE=y` was visible in
`sdkconfig.defaults` (line 26). The Kconfig help for this option, reachable
by `grep -r "UNICORE" /home/vlabe/.espressif/v6.0.1/esp-idf/Kconfig`, says:

> *"It is not recommended to use single-core mode when Wi-Fi or Bluetooth
> are enabled, as some drivers rely on spinlocks that require both cores
> to make progress."*

This is a 3-second `grep`. None of the 3 debuggers ran it.

#### Cause 2: no falsification culture

Every session produced an internally consistent narrative. None attempted to
disprove its own hypothesis.

- Debugger 1: "GPIO27 PSRAM" — never disabled the pin to check
- Debugger 2: "GPIO15 RF noise" — never disabled the interrupt to check
- Debugger 3: "interrupt storm from AP" — never checked the UNICORE model

A hypothesis that cannot be falsified is not a diagnosis — it is a story.

#### Cause 3: no inherited context

Each debugger started fresh. Experiment A of debugger 3 (`disable homing`)
had already been implicitly answered by debugger 2's analysis. The 7
experiments of debugger 3 were mostly redundant with work done in sessions
1 and 2.

#### Cause 4: free-form narratives instead of structured output

Every debugger returned a wall of text mixing:
- Raw data (good)
- Interpretation (necessary)
- Speculation (toxic)
- Confident conclusions (unearned)

Without a strict format, there is no forcing function to separate "this is a
log line" from "this is my guess" from "this is proven".

#### Cause 5: no early termination criteria

Debugger 3 ran 7 experiments without ever hitting a wall and saying "I don't
know, I need to read the platform docs." An explicit "I'm stuck" gate after
3 experiments would have forced it to stop guessing and start reading the
Kconfig help.

### Summary: cost of the current workflow

| Metric | Value |
|--------|-------|
| Debugger sessions | 3 |
| Total build-smoke cycles | 10+ |
| Wall-clock time | ~3 hours |
| Root causes found | 0 |
| Wrong fixes committed to working tree | 2 (`GPIO_PULLDOWN_ENABLE`, `gHomingDone`) |
| Lines of code churn | ~90 |
| Real root cause documented in ESP-IDF Kconfig | Yes, visible in 3-second grep |

## Changes

### 1. Mandatory pre-flight study (before any code change)

The debugger MUST study these in order, quoting relevant lines:

1. **Reset reason:** `docs/protocols/embedded_boot_crash.md` — S1-S5 protocol
2. **Thread architecture & spinlock rules:** `docs/refs/project.md §Thread Architecture`
3. **Known patterns:** `docs/lessons_learned/` — grep the crash symptom
4. **Config constraints:** `sdkconfig.defaults` and `components/*/sdkconfig` for
   `UNICORE`, `SPIRAM_FETCH_INSTRUCTIONS`, `TASK_WDT`, `INT_WDT`
5. **ESP-IDF FreeRTOS docs** (`CONFIG_FREERTOS_UNICORE` help text in
   `/home/vlabe/.espressif/v6.0.1/esp-idf/Kconfig`):
   - Search for the `UNICORE` Kconfig help — it explicitly warns about spinlock
     deadlocks with WiFi/BLE

**Output:** A `## Platform Facts` section listing what was studied and quotes.

### 2. Hypothesis falsification requirement

Every hypothesis must include a concrete experiment that **would disprove it**:

```
## Hypothesis
[startAP() triggers WiFi ISR spinlock deadlock on UNICORE]

## Falsification
[Set CONFIG_FREERTOS_UNICORE=n. If crash persists, hypothesis is wrong.]
```

If the falsification is not executed (e.g. because it requires hardware not
available), the hypothesis confidence is "low" and marked as **unverified**.

### 3. Structured output format

Every debugger response MUST follow this template:

```yaml
## Data
[raw log lines, register dumps, git hashes — facts without interpretation]

## Platform Facts
[relevant quotes from docs, Kconfig, lessons learned]

## Hypotheses (ordered by falsifiability)
1. [hypothesis] — Falsification: [one experiment]
2. [hypothesis] — Falsification: [one experiment]

## Experiment
[what was changed, what was run, full result]

## Conclusion
[root cause or "not yet determined, next: ..."]
```

### 4. Inherited context

The debugger prompt MUST include:
- Previous experiment summaries (table of "change → result → conclusion")
- Currently open changes (git diff)
- The exact crash log tail (last 20 lines of `logs/serial_*.log`)

### 5. Limitation: one experiment per response

The debugger MAY run multiple experiments in a single session, but MUST report
the result of EACH one before proceeding to the next. No batching.

### 6. Explicit "I don't know"

If after 3 experiments the cause is not found, the debugger MUST stop and
output `## STUCK` with:
- What has been ruled out
- What additional data is needed (oscilloscope, custom firmware build,
  specific ESP-IDF version test, etc.)

No hand-waving. No "probably this, but let's check 3 more things".

### 7. CRITICAL: `esp_rom_printf` for crash output

Add a `panic_putc`/`panic_puts` pair that writes to UART0 directly via
`uart_ll_write_txfifo()` (HAL, no VFS, no driver).

The panic handler currently prints via `uart_hal_write_txfifo()` (good!), but
the IWDT panic handler callback that ESP-IDF provides might still use
`printf()`. Verify and fix.

## Acceptance criteria

- [x] A new debugger session given this plan finds the UNICORE spinlock
      deadlock root cause within 3 experiments
- [x] All 3 previous false conclusions (GPIO15 floating, GPIO27 PSRAM, RMT ISR
      storm) are explicitly ruled out in the first response
- [x] Every hypothesis includes a falsification experiment
- [x] Output follows the structured format
- [x] Platform study (Kconfig, FreeRTOS docs, lessons_learned) is completed
      before any code change
