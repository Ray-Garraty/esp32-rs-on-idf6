# AGENTS.md — AI Agent Operational Rules (ESP32-S3 + C++23 + ESP-IDF v6)

This document defines the operational rules, safety protocols, and workflows for AI agents working on this repository.
- For architectural laws, see `docs/refs/CONSTITUTION.md`.
- For hardware/pinout, see `docs/refs/project.md` and `docs/refs/gpio_pins_spec.md`.
- For ESP32-S3 datasheet extracts (electrical, boot, memory, GPIO, power, watchdog, interrupt, RMT, ADC, Wi-Fi/BLE RF, USB/JTAG), see `docs/refs/datasheet_extracts/`.
- For coding conventions, see `docs/refs/coding_style.md`.
- **Path accuracy:** File paths in sub-agent prompts must be relative to the repo root (e.g. `legacy/arduino/src/`, not `arduino/src/` or `src/`). The sub-agent's working directory is the workspace root.
- **Legacy dir is gitignored — DO NOT use Glob for `legacy/`:** The `legacy/` directory is in `.gitignore`, so Glob (`**/*.h`, `**/stepper*`) does NOT find files there. Always use absolute or repo-relative paths with `Read` or `Grep` for legacy files.

## 1. CORE DIRECTIVES (Auto-Revert)

### GR-0: PLATINUM RULE — VERIFY EVERYTHING ON REAL HARDWARE
Before ANY commit or declaring a task done:
`scripts/idf.sh smoke` — the only final gate. Build/test/tidy may be used during work for syntax checks, but the sole acceptance criterion is smoke on real ESP32-S3. Do NOT ask permission, do NOT check connectivity, do NOT resolve the port — just run it blindly. Build and unit tests are INSUFFICIENT — only smoke on hardware proves the firmware works. If smoke fails, show logs and stop.

### GR-0.1: GREEN UNIT TESTS PROVE NOTHING
`valve.setPosition` returned `{"status":"ok"}` for 2+ years through 3 rewrites.
246 unit tests, 774 assertions — all green. The GPIO never toggled.
No review, no analysis, no audit caught it. Only hardware proved it.
**Green CI is not evidence. Every handler must be verified through
hardware state — broadcast fields, GPIO levels, serial logs.**


### GR-10: ONLY THE USER ASSESSES PHYSICAL STATE
The AI MUST NEVER claim physical observations (LED colors, motor movements, relay clicks, valve position).
- **Forbidden:** "LED turned green", "Motor moved successfully", "Valve switched", "Motor started rotating".
- **Required:** Present raw serial logs, ask the user via `question` tool, record their exact words as the only ground truth.

### GR-11: STUDY BEFORE CODEGEN
Before calling ESP-IDF APIs or porting Arduino logic:
- **Primary:** `<device root>/home/vlabe/Downloads/esp-idf-master` (Authoritative v6 headers).
- **Secondary:** `<repo root>/legacy/arduino` (Business logic only. DO NOT copy Arduino syntax).

### GR-13: STOP AND ESCALATE AFTER 3 FAILURES
If a subtask fails 3 times (build fails, tool errors, no answer):
- **STOP.** Do not attempt scope creep or unauthorized refactoring.
- Report what was tried, what was found, and what help is needed.
- Time budget: >15 mins without actionable output = escalate.

### GR-14: GIT IS READ-ONLY (NO DESTRUCTIVE COMMANDS)
Git is strictly read-only for all agents unless the user explicitly asks for a commit.
- **Allowed:** `log`, `show`, `diff`, `status`, `blame`, `rev-parse`, `describe`, `shortlog`, `grep`, `tag`, `branch -a`/`-r` (list only), `ls-files`.
- **Forbidden:** `checkout`, `restore`, `switch`, `stash`, `reset`, `merge`, `rebase`, `bisect`, `branch` (create/delete), `push`, `pull`, `fetch`.
- **Commit:** Allowed ONLY when the user explicitly says "commit" or equivalent. Never commit as part of any automated workflow or because "it seems done".

## 2. ARCHITECTURAL REFERENCES
Agents MUST read and obey `docs/refs/CONSTITUTION.md`. Key pillars:
1. **Non-Blocking Main Loop** (No `rmt_tx_wait_all_done`, `lock()`, or >10ms delays in main).
2. **Task Sovereignty** (No cross-task function calls, queues only, `net_owner` owns network).
3. **Dual-Core Mandatory** (`CONFIG_FREERTOS_UNICORE=n`).
4. **DRAM Init Triangle** (WiFi → HTTP → BLE).
5. **Hardware Safety** (RMT stop flags, strict avoidance of PSRAM pins 26-37).

## 3. WORKFLOW & TOOLING

### 3.1 Build & CI Commands
NEVER call `idf.py` directly. Always use `scripts/idf.sh`.
| Command | Purpose |
|---|---|
| `scripts/idf.sh build` | Clean build (auto-removes stale `sdkconfig`) |
| `scripts/idf.sh flash` | Flash firmware |
| `scripts/idf.sh monitor` | Serial monitor (live log) |
| `scripts/idf.sh smoke` | Automated smoke test (build + flash + 30s monitor) |
| `scripts/idf.sh test` | Host unit tests (Catch2) |
| `scripts/idf.sh tidy` | clang-tidy static analysis (requires build first) |
| `scripts/pre_commit.sh` | Pre-commit validation suite (see §3.5) |

### 3.5 Pre-Commit Script
| Mode | Steps included |
|---|---|
| `scripts/pre_commit.sh --fast` | Staged files scan → clang-format → semgrep → unit tests → docs OKF → sdkconfig constraint (~30s) |
| `scripts/pre_commit.sh` (full) | Fast + build + clang-tidy + serial API hardware test (~5 min) |

Each step fails fast with a clear error message. The hardware serial test (step 10) is the only non-fatal step.

**Policy:**
- **sdkconfig:** Edit only `sdkconfig.defaults` — never `sdkconfig` (auto-generated). Never run `idf.py menuconfig`.
- **Partitions:** Do not change `partitions.csv` without approval.
- **Constraint:** `CONFIG_ESP_WIFI_RX_BA_WIN` must be ≤ `CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM` in `sdkconfig.defaults`. Violation triggers a compile-time `#error`.

### 3.2 Serial & Python Safety
- **FORBIDDEN:** Launching background processes holding serial ports.
- **MANDATORY:** Use `timeout <seconds>` prefix for any blocking/serial tool.
- **PYTHON:** NEVER use `python -c "..."`. Write to `/tmp/opencode` temp file first.
- **LOGS:** NEVER read full `logs/*.log` with the `Read` tool (binary null bytes, context overflow). Use `crash_analyzer.py`, `rg`, `grep`, or `tail`.

### 3.3 Monitor Verbosity
`scripts/monitor.py` runs in quiet mode by default. AI MUST NOT use `--verbose`. The log file is the source of truth.

### 3.4 `.opencode/` Directory
- `Glob` tool does not work inside `.opencode/`. Use `Read`.
- Sub-agents CANNOT create `.md` or `.yaml` files inside `.opencode/` (except `.opencode/tmp/`).

## 4. CHECKLISTS

### Pre-Flight Checklist (Before Codegen)
- [ ] Thread identified & blocking ops moved to workers?
- [ ] Stack impact checked against budget?
- [ ] Init order dependency respected (WiFi→HTTP→BLE)?
- [ ] FFI boundary copies data before return?
- [ ] RMT stop flag included?
- [ ] Task independence maintained (push to queue, don't call network)?

### Final Commit Checklist
- [ ] `scripts/idf.sh build` — 0 errors, 0 warnings
- [ ] `scripts/idf.sh test` — all pass
- [ ] `scripts/idf.sh smoke` — 30s on real ESP32-S3: no Guru Meditation, no WDT panics
- [ ] No blocking ops in main loop (Constitution Art. I)
- [ ] No cross-task coupling (Constitution Art. II)
- [ ] `CONFIG_FREERTOS_UNICORE=n` (Constitution Art. III)
