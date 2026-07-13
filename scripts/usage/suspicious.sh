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
