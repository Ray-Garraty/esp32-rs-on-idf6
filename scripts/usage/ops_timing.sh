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
