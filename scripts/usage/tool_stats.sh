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
