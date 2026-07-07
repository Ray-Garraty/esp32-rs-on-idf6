#!/usr/bin/env bash
set -euo pipefail

# ecotiter C++23 build wrapper
# Usage:
#   ./scripts/build.sh              # build firmware
#   ./scripts/build.sh flash        # flash to /dev/ttyACM0
#   ./scripts/build.sh monitor      # serial monitor with 30s timeout
#   ./scripts/build.sh test         # host unit tests (Catch2)
#   ./scripts/build.sh tidy         # clang-tidy
#   ./scripts/build.sh clean        # remove build/

CMD="${1:-build}"

case "$CMD" in
    build)
        idf.py build
        ;;
    flash)
        PORT="${2:-/dev/ttyACM0}"
        idf.py -p "$PORT" flash
        ;;
    monitor)
        PORT="${2:-/dev/ttyACM0}"
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
    clean)
        rm -rf build build-tests
        ;;
    *)
        echo "Usage: $0 {build|flash|monitor|test|tidy|clean}"
        exit 1
        ;;
esac
