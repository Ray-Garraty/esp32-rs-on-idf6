#!/bin/bash
set -e

PYTHON_PATH="/c/Users/vlbes/.pyenv/pyenv-win/versions/3.11.9"

fast_mode=false
[ "$1" = "--fast" ] && fast_mode=true

echo "=== 1. Format check ==="
cargo fmt --all -- --check

echo "=== 2. Host unit tests ==="
cargo test --lib

echo "=== 3. Clippy (host target) ==="
cargo clippy -- -D warnings

if [ "$fast_mode" = false ]; then
    xtensa() {
        PATH="$PYTHON_PATH:$PATH" cargo "$@"
    }

    echo "=== 4. Clippy (xtensa target) ==="
    xtensa clippy --target xtensa-esp32-espidf -- -D warnings

    echo "=== 5. Build (xtensa) ==="
    xtensa build --target xtensa-esp32-espidf

    echo "=== 6. Check for unsafe blocks ==="
    UNSAFE_COUNT=$(grep -r "unsafe {" src/ 2>/dev/null | wc -l)
    echo "Found $UNSAFE_COUNT unsafe blocks"
    if [ "$UNSAFE_COUNT" -gt 10 ]; then
        echo "WARNING: Too many unsafe blocks! Review if all are necessary."
    fi
fi

echo "=== 7. Check for unwrap/expect in library code ==="
UNWRAP_COUNT=$(grep -r "\.unwrap()" src/ --include="*.rs" 2>/dev/null | grep -v "main.rs" | wc -l)
EXPECT_COUNT=$(grep -r "\.expect(" src/ --include="*.rs" 2>/dev/null | grep -v "main.rs" | wc -l)
if [ "$UNWRAP_COUNT" -gt 0 ] || [ "$EXPECT_COUNT" -gt 0 ]; then
    echo "ERROR: Found unwrap/expect in library code!"
    grep -r "\.unwrap()" src/ --include="*.rs" 2>/dev/null | grep -v "main.rs"
    grep -r "\.expect(" src/ --include="*.rs" 2>/dev/null | grep -v "main.rs"
    exit 1
fi

echo "=== 8. Check for blocking calls in main loop ==="
python scripts/check_blocking.py

echo "=== All checks passed ==="
