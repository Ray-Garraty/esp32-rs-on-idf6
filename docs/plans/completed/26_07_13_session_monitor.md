---
type: Plan
title: Session monitoring scripts for teamlead token/usage tracking
description: >
  Create scripts/usage/ for teamlead to monitor sub-agent sessions via
  opencode SQLite DB: token breakdown, suspicious tool calls, daily stats,
  tool usage. Integrate metrics into Reporter agent's completion report.
tags: [opencode, monitoring, sqlite, scripts, tokens]
timestamp: 2026-07-13
status: completed
updated: 2026-07-13
---

# Session Monitoring & Cost Tracking

## Summary

The teamlead agent currently has no visibility into sub-agent session token
usage, tool calls, or suspicious activity. The opencode SQLite database at
`$HOME/.local/share/opencode/opencode.db` stores all session data with
parent-child relationships (`session.parent_id`), but there are no tools to
query it.

This plan creates a `scripts/usage/` directory with five shell scripts that
give the teamlead (and the human operator) insight into sub-agent behaviour,
and integrates the collected metrics into the Reporter agent's completion
report.

### Schema (relevant tables)

```
session (parent_id → session.id)   — parent_id links sub-agent to root task
  └── message (session_id → session.id)
        └── part (message_id → message.id)
              └── data (JSON): type=(text|tool|reasoning|step-start|step-finish),
                               tool="bash|read|edit|...",
                               state.input.command="..."
```

**Caveat:** `time_created` is in **milliseconds** (UNIX epoch × 1000).
All datetime conversions must divide by 1000.

### Schema deviations discovered during implementation

| Expected | Actual | Impact |
|----------|--------|--------|
| `input_tokens` / `output_tokens` | `tokens_input` / `tokens_output` | All `SUM/COALESCE` expressions renamed |
| `when` alias OK as-is | `when` is SQLite reserved word | All `when` aliases quoted as `"when"` |
| N/A | `strftime` returns string, not number | All `strftime(...) > value` comparisons need `+ 0` coercion |
| `s.time_created` used for filtering | Works, but `message.time_created` is more accurate for message-level queries | Used `message` join in suspicious.sh and tool_stats.sh for precision |
| `part` table has `time_created`/`time_updated` | May NOT have these columns | `ops_timing.sh` has try/fallback: primary uses `part` timestamps, fallback joins `message` and uses `message.time_created` |

---

## Steps / Execution log

### Step 1: Create `scripts/usage/current_task.sh`

Cost breakdown of the current (or specified) task, recursively walking
the session tree via `parent_id`. Root session = `parent_id IS NULL`.

```bash
#!/bin/bash
# scripts/usage/current_task.sh [session_id]
# Without argument — last root session. With argument — specific session.

DB="$HOME/.local/share/opencode/opencode.db"

if [ -z "$1" ]; then
    ROOT_ID=$(sqlite3 "$DB" "SELECT id FROM session WHERE parent_id IS NULL ORDER BY time_created DESC LIMIT 1;")
    [ -z "$ROOT_ID" ] && echo "No sessions found" && exit 1
else
    ROOT_ID="$1"
fi

echo "=== Task: ${ROOT_ID:0:16}... ==="
echo

sqlite3 -header -column "$DB" <<SQL
WITH RECURSIVE task_tree AS (
    SELECT id, parent_id, agent,
           COALESCE(tokens_input, 0) + COALESCE(tokens_output, 0) as tokens,
           time_created, time_updated,
           0 as depth
    FROM session WHERE id = '$ROOT_ID'
    UNION ALL
    SELECT s.id, s.parent_id, s.agent,
           COALESCE(s.tokens_input, 0) + COALESCE(s.tokens_output, 0),
           s.time_created, s.time_updated,
           t.depth + 1
    FROM session s JOIN task_tree t ON s.parent_id = t.id
)
SELECT
    printf('%*s%s', depth * 2, '', agent) as agent_tree,
    COUNT(*) as sessions,
    SUM(tokens) as tokens,
    ROUND(SUM(time_updated - time_created) / 1000.0, 1) as wall_sec
FROM task_tree
GROUP BY id
ORDER BY depth, tokens DESC;
SQL

echo
echo "--- Totals ---"
sqlite3 "$DB" <<SQL
WITH RECURSIVE task_tree AS (
    SELECT id,
           COALESCE(tokens_input, 0) + COALESCE(tokens_output, 0) as tokens,
           time_created, time_updated
    FROM session WHERE id = '$ROOT_ID'
    UNION ALL
    SELECT s.id,
           COALESCE(s.tokens_input, 0) + COALESCE(s.tokens_output, 0),
           s.time_created, s.time_updated
    FROM session s JOIN task_tree t ON s.parent_id = t.id
)
SELECT
    COUNT(*) as total_sessions,
    SUM(tokens) as total_tokens,
    ROUND(SUM(time_updated - time_created) / 1000.0, 1) as total_sec
FROM task_tree;
SQL
```

**Example output:**

```
=== Task: ses_0a57bca94f... ===

agent_tree              sessions  tokens    wall_sec
──────────────────────  ────────  ────────  ────────
build                   1         18450       234
  planner               1         22100      2508
  verifier              1         45230       456
  implementer           2         89400       892
    explore             3          6800       130
  validator             1         15600       187
  reviewer              1         28900       315
  reporter              1          8200        45

--- Totals ---
total_sessions  total_tokens  total_sec
──────────────  ────────────  ─────────
10              234680        4767
```

### Step 2: Create `scripts/usage/suspicious.sh`

Detect dangerous or suspicious bash commands in the last N hours.

```bash
#!/bin/bash
# scripts/usage/suspicious.sh [hours]
# Default: last hour

DB="$HOME/.local/share/opencode/opencode.db"
HOURS=${1:-1}

echo "=== Suspicious bash commands (last $HOURS hours) ==="
sqlite3 -header -column "$DB" <<SQL
SELECT
    s.agent,
    substr(json_extract(p.data, '$.state.input.command'), 1, 100) as command,
    datetime(m.time_created / 1000, 'unixepoch', 'localtime') as "when"
FROM part p
JOIN message m ON m.id = p.message_id
JOIN session s ON s.id = m.session_id
WHERE json_extract(p.data, '$.type') = 'tool'
  AND json_extract(p.data, '$.tool') = 'bash'
  AND s.time_created / 1000 > strftime('%s', 'now', '-' || $HOURS || ' hours') + 0
  AND (
      json_extract(p.data, '$.state.input.command') LIKE '%git stash%'
   OR json_extract(p.data, '$.state.input.command') LIKE '%git reset%'
   OR json_extract(p.data, '$.state.input.command') LIKE '%git checkout%'
   OR json_extract(p.data, '$.state.input.command') LIKE '%rm -rf%'
   OR json_extract(p.data, '$.state.input.command') LIKE '%sudo %'
  )
ORDER BY m.time_created DESC;
SQL
```

### Step 3: Create `scripts/usage/today.sh`

Per-agent aggregation for the current calendar day.

```bash
#!/bin/bash
# scripts/usage/today.sh

DB="$HOME/.local/share/opencode/opencode.db"

sqlite3 -header -column "$DB" <<SQL
SELECT
    agent,
    COUNT(*) as sessions,
    SUM(tokens_input + tokens_output) as total_tokens,
    ROUND(SUM(time_updated - time_created) / 1000.0, 0) as total_sec,
    datetime(MIN(time_created / 1000), 'unixepoch', 'localtime') as first,
    datetime(MAX(time_created / 1000), 'unixepoch', 'localtime') as last
FROM session
WHERE date(time_created / 1000, 'unixepoch', 'localtime') = date('now', 'localtime')
GROUP BY agent
ORDER BY total_tokens DESC;
SQL
```

### Step 4: Create `scripts/usage/tool_stats.sh`

Count tool calls per agent for the last N hours.

```bash
#!/bin/bash
# scripts/usage/tool_stats.sh [hours]
# Default: 24 hours

DB="$HOME/.local/share/opencode/opencode.db"
HOURS=${1:-24}

sqlite3 -header -column "$DB" <<SQL
SELECT
    s.agent,
    json_extract(p.data, '$.tool') as tool,
    COUNT(*) as calls
FROM part p
JOIN message m ON m.id = p.message_id
JOIN session s ON s.id = m.session_id
WHERE json_extract(p.data, '$.type') = 'tool'
  AND s.time_created / 1000 > strftime('%s', 'now', '-' || $HOURS || ' hours') + 0
GROUP BY s.agent, tool
ORDER BY s.agent, calls DESC;
SQL
```

### Step 5: Create `scripts/usage/ops_timing.sh`

Time spent by operation type (build, flash, monitor, test, git, read, edit, etc.)
— grouped, not per-command. No individual command durations.

```bash
#!/bin/bash
# scripts/usage/ops_timing.sh [hours]
# Default: 24 hours

DB="$HOME/.local/share/opencode/opencode.db"
HOURS=${1:-24}

# Try primary query using p.time_updated / p.time_created from part table
OUTPUT=$(sqlite3 -header -column "$DB" <<SQL 2>&1
SELECT
  CASE
    WHEN json_extract(p.data, '$.state.input.command') LIKE '%scripts/idf.sh build%' THEN 'build'
    WHEN json_extract(p.data, '$.state.input.command') LIKE '%scripts/idf.sh flash%' THEN 'flash'
    WHEN json_extract(p.data, '$.state.input.command') LIKE '%scripts/idf.sh smoke%' THEN 'smoke'
    WHEN json_extract(p.data, '$.state.input.command') LIKE '%scripts/monitor.py%' THEN 'monitor'
    WHEN json_extract(p.data, '$.state.input.command') LIKE '%scripts/crash_analyzer.py%' THEN 'crash_analyzer'
    WHEN json_extract(p.data, '$.state.input.command') LIKE '%scripts/idf.sh test%' THEN 'test'
    WHEN json_extract(p.data, '$.state.input.command') LIKE '%scripts/idf.sh tidy%' THEN 'tidy'
    WHEN json_extract(p.data, '$.state.input.command') LIKE '%git %' THEN 'git'
    WHEN json_extract(p.data, '$.tool') = 'question' THEN 'question'
    WHEN json_extract(p.data, '$.tool') = 'read' THEN 'read'
    WHEN json_extract(p.data, '$.tool') IN ('write','edit') THEN 'write_edit'
    ELSE 'other'
  END as operation,
  COUNT(*) as calls,
  ROUND(AVG((p.time_updated - p.time_created) / 1000.0), 1) as avg_sec,
  ROUND(SUM((p.time_updated - p.time_created) / 1000.0), 1) as total_sec
FROM part p
WHERE json_extract(p.data, '$.type') = 'tool'
  AND p.time_updated > p.time_created
  AND p.time_created / 1000 > strftime('%s', 'now', '-' || $HOURS || ' hours') + 0
GROUP BY operation
ORDER BY total_sec DESC;
SQL
)

if echo "$OUTPUT" | grep -qi "no such column"; then
    # Fallback: part table lacks time_updated/time_created, use message timestamps
    sqlite3 -header -column "$DB" <<SQL
SELECT
  CASE
    WHEN json_extract(p.data, '$.state.input.command') LIKE '%scripts/idf.sh build%' THEN 'build'
    WHEN json_extract(p.data, '$.state.input.command') LIKE '%scripts/idf.sh flash%' THEN 'flash'
    WHEN json_extract(p.data, '$.state.input.command') LIKE '%scripts/idf.sh smoke%' THEN 'smoke'
    WHEN json_extract(p.data, '$.state.input.command') LIKE '%scripts/monitor.py%' THEN 'monitor'
    WHEN json_extract(p.data, '$.state.input.command') LIKE '%scripts/crash_analyzer.py%' THEN 'crash_analyzer'
    WHEN json_extract(p.data, '$.state.input.command') LIKE '%scripts/idf.sh test%' THEN 'test'
    WHEN json_extract(p.data, '$.state.input.command') LIKE '%scripts/idf.sh tidy%' THEN 'tidy'
    WHEN json_extract(p.data, '$.state.input.command') LIKE '%git %' THEN 'git'
    WHEN json_extract(p.data, '$.tool') = 'question' THEN 'question'
    WHEN json_extract(p.data, '$.tool') = 'read' THEN 'read'
    WHEN json_extract(p.data, '$.tool') IN ('write','edit') THEN 'write_edit'
    ELSE 'other'
  END as operation,
  COUNT(*) as calls,
  ROUND(AVG((m.time_updated - m.time_created) / 1000.0), 1) as avg_sec,
  ROUND(SUM((m.time_updated - m.time_created) / 1000.0), 1) as total_sec
FROM part p
JOIN message m ON m.id = p.message_id
WHERE json_extract(p.data, '$.type') = 'tool'
  AND m.time_updated > m.time_created
  AND m.time_created / 1000 > strftime('%s', 'now', '-' || $HOURS || ' hours') + 0
GROUP BY operation
ORDER BY total_sec DESC;
SQL
else
    echo "$OUTPUT"
fi
```

### Step 6: Update `reporter.md` — add Step 5 Metrics Collection

Inserted a new Step 5 (Collect Usage Metrics) between Step 4 (Generate Commit Message)
and the old Step 5 (renumbered to Step 6: Validate & Save). The reporter runs all 5
usage scripts, embeds output under `## Metrics`, and includes stderr on failure.

**Actual content added** (in `reporter.md`):

```markdown
### Step 5: Collect Usage Metrics

Run all usage scripts against the opencode DB and embed output into the
Completion Report under `## Metrics`:

```bash
ROOT_ID=$(sqlite3 "$HOME/.local/share/opencode/opencode.db" \
  "SELECT id FROM session WHERE parent_id IS NULL
   ORDER BY time_created DESC LIMIT 1;")

echo '### Cost Breakdown'
bash scripts/usage/current_task.sh "$ROOT_ID"

echo '### Suspicious Commands (last 24h)'
bash scripts/usage/suspicious.sh 24

echo '### Today'\''s Sessions'
bash scripts/usage/today.sh

echo '### Tool Usage (last 24h)'
bash scripts/usage/tool_stats.sh 24

echo '### Operation Timing (last 24h)'
bash scripts/usage/ops_timing.sh 24
```

If any script fails (non-zero exit), include its stderr as a code block under
the relevant heading. An error is still valid evidence.
```
---

### Step 7: Update `teamlead.md` — Step 7 metrics review

Replaced the old Step 7 with a metrics review section. Teamlead reviews
`## Metrics` for red flags before presenting the commit message:

| What to look for | Why |
|------------------|-----|
| **Token anomalies** | One agent using >2× tokens than others suggests inefficiency |
| **Suspicious commands** | `git stash`, `git checkout`, `sudo`, `rm -rf` in unexpected agents |
| **Tool imbalance** | Agent calling build 10× in a session → sdkconfig not cached? |
| **Missing hardware actions** | Validator never ran `scripts/idf.sh flash` → ACs likely deferred |
| **Operation timing** | build avg >60s or flash avg >30s — possible issue |
| **Excessive tokens** | Total >500K tokens for a simple task → plan/scope issue |

Also added `"scripts/usage/*": allow` to the bash permissions for the teamlead.

Each completed plan document in `docs/plans/completed/` now includes a
full `## Metrics` section — this becomes a permanent record for token
usage analysis and workflow optimisation.

### Step 8: Bash aliases (for human use)

Document in `scripts/usage/README.md`:

```bash
alias oc-last='sqlite3 -header -column ~/.local/share/opencode/opencode.db "
  SELECT substr(id,1,16) id, agent,
         datetime(time_created/1000,\"unixepoch\",\"localtime\") when
  FROM session WHERE parent_id IS NULL
  ORDER BY time_created DESC LIMIT 10;"'

alias oc-today='sqlite3 -header -column ~/.local/share/opencode/opencode.db "
  SELECT agent, COUNT(*) n,
         SUM(tokens_input+tokens_output) tok
  FROM session
  WHERE date(time_created/1000,\"unixepoch\",\"localtime\")=date(\"now\",\"localtime\")
  GROUP BY agent ORDER BY tok DESC;"'
```

---

## Verification (Actual Results)

All scripts were run against the live opencode DB on 2026-07-13.

| # | Script | Result |
|---|--------|--------|
| 1 | `current_task.sh` | ✓ Tree displays build→general hierarchy, totals match |
| 2 | `suspicious.sh 24` | ✓ Detected `sudo apt-get install -y sqlite3` in build |
| 3 | `today.sh` | ✓ 3 agents active (build, explore, general), correct token sums |
| 4 | `tool_stats.sh 24` | ✓ Lists bash, read, write, edit, grep, glob, question, todowrite, task |
| 5 | `ops_timing.sh 24` | ✓ Groups by operation type, no per-command noise. AVG/SEC totals reasonable |
| 6 | Reporter integration | Pending — requires next workflow execution

---

## Files affected

| File | Action |
|------|--------|
| `scripts/usage/current_task.sh` | **Create** — token + time breakdown by sub-agent tree |
| `scripts/usage/suspicious.sh` | **Create** — detect dangerous bash commands |
| `scripts/usage/today.sh` | **Create** — per-agent daily stats with time |
| `scripts/usage/tool_stats.sh` | **Create** — per-agent tool usage count |
| `scripts/usage/ops_timing.sh` | **Create** — grouped time by operation type |
| `scripts/usage/README.md` | **Create** — aliases and usage examples, date header |
| `.opencode/agents/reporter.md` | **Modified** — add Step 5 Metrics Collection |
| `.opencode/agents/teamlead.md` | **Modified** — Step 7: metrics review + `scripts/usage/*` permission |

---

## Dependencies

- `sqlite3` CLI must be installed (`which sqlite3`)
- Write access to `$HOME/.local/share/opencode/opencode.db`
- Script paths are relative to repo root — run from project directory

## Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| opencode changes DB schema | Scripts return NULL/empty | All scripts fail predictably — output is still valid evidence |
| opencode changes data directory | DB path broken | Single env-configurable `$DB` variable |
| bash/sqlite3 unavailable mid-workflow | Metrics section will be empty or error text | Error output documents the problem itself |
| JSON paths undocumented | Silent NULL results after opencode update | Teamlead detects empty metrics during review |
