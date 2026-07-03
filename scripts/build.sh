#!/bin/bash
# ESP32 build environment setup + cargo invocation.
# Usage:
#   ./scripts/build.sh                    # build (xtensa target)
#   ./scripts/build.sh check              # cargo check (fast)
#   ./scripts/build.sh clippy             # clippy (xtensa target)
#   ./scripts/build.sh clippy-host        # clippy (host target, lib only)
#   ./scripts/build.sh test               # host unit tests
#   . ./scripts/build.sh                  # source to set env vars only
set -e

# ── ESP toolchain paths ──────────────────────────────────────────────
ESP_BASE="$HOME/.rustup/toolchains/esp"

# Cargo + rustc from ESP toolchain (bypasses broken .cargo/bin symlinks)
export PATH="$ESP_BASE/bin:$PATH"

# Xtensa C cross-compiler (for bindgen / libclang)
for d in "$ESP_BASE"/xtensa-esp-elf/esp-*/xtensa-esp-elf/bin; do
    [ -d "$d" ] && export PATH="$d:$PATH" && break
done

# clang / libclang for esp-idf-sys bindgen
for d in "$ESP_BASE"/xtensa-esp32-elf-clang/esp-*/esp-clang; do
    if [ -d "$d" ]; then
        export LIBCLANG_PATH="$d/lib"
        export CLANG_PATH="$d/bin/clang"
        break
    fi
done

# ESP-IDF tools (esptool, idf.py, etc.)
ESPIDF_TOOLS="$HOME/.espressif/tools"
for d in "$ESPIDF_TOOLS"/python/*/venv/bin; do
    [ -d "$d" ] && export PATH="$d:$PATH" && break
done

# esptool for flashing
for d in "$ESPIDF_TOOLS"/esptool/*/; do
    [ -f "$d/esptool.py" ] && export PATH="$d:$PATH" && break
done

# ── Verify toolchain ─────────────────────────────────────────────────
if ! type cargo >/dev/null 2>&1; then
    echo "ERROR: cargo not found. Check $ESP_BASE/bin/" >&2
    exit 1
fi

TARGET="xtensa-esp32-espidf"

# ── If sourced with no args, just set env and return ─────────────────
if [[ "${BASH_SOURCE[0]}" != "$0" ]] && [ $# -eq 0 ]; then
    echo "Environment set. cargo: $(type -p cargo)"
    return 0 2>/dev/null || exit 0
fi

# ── Dispatch ──────────────────────────────────────────────────────────
case "${1:-build}" in
    build)
        echo "=== Building for $TARGET ==="
        cargo build --target "$TARGET"
        ;;
    check)
        echo "=== Checking for $TARGET ==="
        cargo check --target "$TARGET"
        ;;
    clippy)
        echo "=== Clippy (xtensa target) ==="
        cargo clippy --target "$TARGET" -- -D warnings
        ;;
    clippy-host)
        echo "=== Clippy (host target, lib only) ==="
        cargo clippy --lib -- -D warnings
        ;;
    test)
        echo "=== Host unit tests ==="
        cargo test --lib
        ;;
    fmt)
        echo "=== Format check ==="
        cargo fmt --all -- --check
        ;;
    flash)
        PORT="${2:-/dev/ttyUSB0}"
        ELF="target/$TARGET/debug/ecotiter"
        if [ ! -f "$ELF" ]; then
            echo "ERROR: ELF not found at $ELF — build first" >&2
            exit 1
        fi
        echo "=== Flashing to $PORT ==="
        espflash flash --port "$PORT" "$ELF"
        ;;
    *)
        echo "Usage: $0 {build|check|clippy|clippy-host|test|fmt|flash [port]}" >&2
        exit 1
        ;;
esac
