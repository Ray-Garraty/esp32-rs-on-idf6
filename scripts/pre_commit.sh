#!/bin/bash
set -e

PYTHON_PATH=""  # Set to pyenv path on Windows if needed

fast_mode=false
[ "$1" = "--fast" ] && fast_mode=true

echo "=== 1. Format check ==="
cargo fmt --all -- --check

echo "=== 2. Host unit tests ==="
cargo test --lib

echo "=== 3. Clippy (host target, lib only — binary needs xtensa) ==="
cargo clippy --lib -- -D warnings

echo "=== 3.5. Blocking check regression tests ==="
python3 scripts/test_check_blocking.py

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

echo "=== 7. Check for unwrap/expect in production code ==="
python3 scripts/check_unwrap.py

echo "=== 8. Check for blocking calls in main loop ==="
python scripts/check_blocking.py

echo "=== 9. Docs OKF validation ==="
python docs/validate_okf.py

echo "=== All checks passed ==="
