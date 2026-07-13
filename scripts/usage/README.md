# Session Monitoring Scripts

**2026-07-13**

Monitor opencode SQLite sessions for token usage, tool calls, and suspicious activity.

## Quick Aliases

Add to `~/.bashrc`:

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

## Usage

```bash
# Show last root session token tree
bash scripts/usage/current_task.sh

# Show specific session token tree
bash scripts/usage/current_task.sh <session_id>

# Suspicious commands (last N hours, default: 1)
bash scripts/usage/suspicious.sh 24

# Today's per-agent stats
bash scripts/usage/today.sh

# Per-agent tool usage (last N hours, default: 24)
bash scripts/usage/tool_stats.sh 24

# Operation timing breakdown (last N hours, default: 24)
bash scripts/usage/ops_timing.sh 24
```

## DB Path

All scripts use `$HOME/.local/share/opencode/opencode.db`.
