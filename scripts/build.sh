#!/usr/bin/env bash
set -euo pipefail

# ecotiter C++23 build wrapper
# Usage:
#   ./scripts/build.sh              # build firmware
#   ./scripts/build.sh flash        # flash to /dev/ttyUSB0
#   ./scripts/build.sh monitor      # serial monitor with 30s timeout
#   ./scripts/build.sh test         # host unit tests (Catch2)
#   ./scripts/build.sh tidy         # clang-tidy
#   ./scripts/build.sh clean        # remove build/

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Self-contained ESP-IDF environment setup
IDF_PATH="${IDF_PATH:-$HOME/.espressif/v6.0.1/esp-idf}"
if [ -f "$IDF_PATH/export.sh" ]; then
    export IDF_PATH
    # shellcheck source=/dev/null
    source "$IDF_PATH/export.sh" > /dev/null 2>&1
fi

CMD="${1:-build}"

case "$CMD" in
    build)
        rm -f "$PROJECT_DIR/sdkconfig"
        idf.py build
        ;;
    flash)
        PORT="${2:-/dev/ttyUSB0}"
        idf.py -p "$PORT" flash
        ;;
    monitor)
        PORT="${2:-/dev/ttyUSB0}"
        timeout 30 idf.py -p "$PORT" monitor || true
        ;;
    test)
        mkdir -p build-tests
        cd build-tests
        cmake ../tests
        cmake --build .
        ctest --output-on-failure
        ;;
    tidy)
        ./scripts/lint.sh "${@:2}"
        ;;    
    uart)
        PORT="${2:-/dev/ttyUSB0}"
        timeout 30 python3 scripts/uart_test.py -p "$PORT"
        ;;
    reconfigure)
        rm -f "$PROJECT_DIR/sdkconfig"
        idf.py reconfigure
        ;;
    clean)
        rm -rf build build-tests
        ;;
    *)
        echo "Usage: $0 {build|flash|monitor|uart|reconfigure|test|tidy|clean}"
        exit 1
        ;;
esac
