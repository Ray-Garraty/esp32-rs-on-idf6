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
