#!/bin/bash
set -e

HOOK_SRC="scripts/pre_commit.sh"
HOOK_DST=".git/hooks/pre-commit"

cat > "$HOOK_DST" << 'HOOK'
#!/bin/bash
exec bash scripts/pre_commit.sh --fast
HOOK
chmod +x "$HOOK_DST"

echo "Pre-commit hook installed: $HOOK_DST (runs pre_commit.sh --fast)"
