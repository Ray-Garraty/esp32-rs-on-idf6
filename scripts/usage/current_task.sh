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
sqlite3 -header -column "$DB" <<SQL
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
