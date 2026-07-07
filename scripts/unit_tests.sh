#!/usr/bin/env bash
set -euo pipefail

# ecotiter host unit tests (Catch2)
# Usage:
#   ./scripts/test.sh            # configure + build + run
#   ./scripts/test.sh --build    # configure + build only
#   ./scripts/test.sh --list     # list test cases
#   ./scripts/test.sh -- <filter>  # run specific test (Catch2 wildcard)

CMD="${1:-run}"

case "$CMD" in
  --build)
    cmake -B build-tests -S tests
    cmake --build build-tests
    ;;
  --list)
    cmake -B build-tests -S tests 2>&1 | grep -v "^--"
    cmake --build build-tests 2>&1 | tail -1
    ./build-tests/unit_tests --list-tests
    ;;
  run|--*)
    # Rebuild if CMakeLists or sources changed
    if [[ ! -d build-tests ]]; then
      cmake -B build-tests -S tests
    fi
    cmake --build build-tests 2>&1 | tail -1

    if [[ "$CMD" == run ]]; then
      exec ./build-tests/unit_tests
    else
      # Pass Catch2 filter (everything after --)
      exec ./build-tests/unit_tests "${@:2}"
    fi
    ;;
  *)
    echo "Usage: $0 {run|--build|--list|-- <filter>}"
    exit 1
    ;;
esac
