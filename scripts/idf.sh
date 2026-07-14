#!/usr/bin/env bash
set -euo pipefail

# ecotiter C++23 firmware toolchain — single entry point for all build ops

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

if [ -z "${IDF_PATH:-}" ]; then
    for dir in "$HOME/.espressif"/v*/esp-idf; do
        if [ -f "$dir/export.sh" ]; then
            IDF_PATH="$dir"
            break
        fi
    done
fi
if [ -n "${IDF_PATH:-}" ] && [ -f "$IDF_PATH/export.sh" ]; then
    export IDF_PATH
    source "$IDF_PATH/export.sh" > /dev/null 2>&1
fi
export IDF_CCACHE_ENABLE=1
export CCACHE_ENABLE=1

# File-based mutex: only one idf.sh instance at a time (like Cargo)
LOCKDIR="$PROJECT_DIR/.idf_lock"
LOCK_TIMEOUT=300
acquire_lock() {
    local waited=0
    while ! mkdir "$LOCKDIR" 2>/dev/null; do
        if [ "$waited" -eq 0 ]; then
            echo "⏳ Waiting for another idf.sh instance (PID $(cat "$LOCKDIR/pid" 2>/dev/null || echo '?'))..."
        fi
        local lock_pid
        lock_pid=$(cat "$LOCKDIR/pid" 2>/dev/null || echo "")
        if [ -n "$lock_pid" ] && ! kill -0 "$lock_pid" 2>/dev/null; then
            echo "⚠️  Stale lock from PID $lock_pid — removing"
            rm -rf "$LOCKDIR"
            continue
        fi
        sleep 1
        waited=$((waited + 1))
        if [ "$waited" -ge "$LOCK_TIMEOUT" ]; then
            echo "❌ Timed out waiting for lock (${LOCK_TIMEOUT}s)"
            echo "   To force: rm -rf $LOCKDIR"
            exit 1
        fi
    done
    echo "$$" > "$LOCKDIR/pid"
    trap 'rm -rf "$LOCKDIR"' EXIT SIGTERM SIGINT
}
acquire_lock

# Auto-detect ESP32-S3 port by VID/PID, fallback to /dev/ttyUSB0
resolve_port() {
    local port="${1:-}"
    if [ -n "$port" ]; then
        echo "$port"
        return 0
    fi
    local detected
    detected=$(python3 "$SCRIPT_DIR/find_port.py" 2>/dev/null || true)
    if [ -n "$detected" ]; then
        echo "$detected"
        return 0
    fi
    echo "/dev/ttyUSB0"
}

# Clean build with metadata injection
do_build() {
    rm -rf "$PROJECT_DIR/build"
    rm -f "$PROJECT_DIR/sdkconfig" "$PROJECT_DIR/sdkconfig.old"

    BUILD_DATE=$(date '+%Y-%m-%d %H:%M:%S')
    GIT_HASH=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
    export BUILD_DATE GIT_HASH
    echo "Building firmware: $BUILD_DATE (git: $GIT_HASH)"

    # --- ccache config for clean-build pattern ---
    export CCACHE_BASEDIR="$PROJECT_DIR"
    export CCACHE_NOHASHDIR=1
    export CCACHE_SLOPPINESS="time_macros,include_file_mtime,include_file_ctime,file_macro,locale,pch_defines"
    export CCACHE_COMPILERCHECK=content
    # --------------------------------------------

    mkdir -p "$PROJECT_DIR/logs"
    local build_log="$PROJECT_DIR/logs/build.log"
    {
        echo "# Build: $BUILD_DATE (git: $GIT_HASH)"
        echo ""
    } > "$build_log"

    local build_timeout=300
    local idf_exit=0

    BUILD_START=$(date +%s)
    timeout "$build_timeout" idf.py build 2>&1 | tee -a "$build_log" | \
        grep -iE '(^Building firmware|^Executing action|warning:|error:|fatal:|FAILED|^Project build complete|Full build log|binary size)' || true
    idf_exit=${PIPESTATUS[0]}
    BUILD_END=$(date +%s)

    local elapsed=$((BUILD_END - BUILD_START))
    local elapsed_str="$((elapsed / 60))m $((elapsed % 60))s"

    # Determine outcome
    local outcome=""
    local exit_code=0
    if [ $idf_exit -eq 124 ]; then
        outcome="TIMEOUT"
        exit_code=124
    elif [ $idf_exit -ne 0 ]; then
        outcome="FAILED (exit $idf_exit)"
        exit_code=$idf_exit
    elif [ ! -f "$PROJECT_DIR/build/ecotiter.bin" ]; then
        outcome="FAILED (binary not created)"
        exit_code=1
    else
        # Write build metadata
        mkdir -p "$PROJECT_DIR/build"
        {
            echo "BUILD_DATE=\"$BUILD_DATE\""
            echo "GIT_HASH=$GIT_HASH"
        } > "$PROJECT_DIR/build/.build_meta"
        outcome="SUCCESS"
    fi

    # Summary — last line is the unambiguous verdict
    echo ""
    echo "=== BUILD RESULT ==="
    echo "  Time:    $elapsed_str"
    if command -v ccache &>/dev/null; then
        ccache -s 2>/dev/null | grep -E "Hits:|Cache size" || true
    fi
    echo "  Outcome: $outcome"
    if [ "$outcome" = "SUCCESS" ]; then
        echo "  ✅ Build succeeded"
    else
        echo "  ❌ Build failed: $outcome"
    fi
    echo "  Log:     $build_log"

    return "$exit_code"
}

# Erase entire flash chip
do_erase_flash() {
    local port
    port=$(resolve_port "${1:-}")
    echo "⚠️  This will ERASE THE ENTIRE FLASH CHIP on $port."
    echo "    All data will be lost (bootloader, app, NVS calibration, WiFi credentials)."
    echo -n "    Type 'yes' to continue: "
    read -r confirm
    if [ "$confirm" != "yes" ]; then
        echo "❌ Aborted."
        exit 1
    fi
    echo "Erasing entire flash on $port..."
    idf.py -p "$port" erase-flash
    echo "✅ Flash erased. Run './scripts/idf.sh flash' to re-program."
}

# Erase NVS partition only (parttool.py, fallback esptool)
do_erase_nvs() {
    local port
    port=$(resolve_port "${1:-}")
    echo "⚠️  This will ERASE THE NVS PARTITION on $port."
    echo "    Calibration data, WiFi credentials, and NVS settings will be lost."
    echo "    The app firmware will remain intact."
    echo -n "    Type 'yes' to continue: "
    read -r confirm
    if [ "$confirm" != "yes" ]; then
        echo "❌ Aborted."
        exit 1
    fi
    local parttool="$IDF_PATH/components/partition_table/parttool.py"
    if [ -f "$parttool" ]; then
        echo "Erasing NVS partition via parttool.py..."
        python3 "$parttool" --port "$port" erase_partition --partition-name=nvs
    else
        echo "⚠️  parttool.py not found, falling back to esptool with known offsets."
        echo "    Erasing region 0x9000 +0x6000 (NVS for SINGLE_APP_LARGE)..."
        esptool.py --port "$port" erase-region 0x9000 0x6000
    fi
    echo "✅ NVS partition erased."
}

CMD="${1:-build}"

case "$CMD" in
    build)
        do_build
        ;;

    flash|monitor|uart)
        PORT=$(resolve_port "${2:-}")
        echo "Port: $PORT"
        case "$CMD" in
            flash)
                BINARY="$PROJECT_DIR/build/ecotiter.bin"
                META="$PROJECT_DIR/build/.build_meta"
                if [ -f "$META" ]; then
                    source "$META"
                    echo "Firmware built: ${BUILD_DATE:-?} (git: ${GIT_HASH:-?})"
                else
                    echo "⚠️  No build metadata found"
                fi
                if [ ! -f "$BINARY" ]; then
                    echo "❌ Firmware binary not found. Run './scripts/idf.sh build' first."
                    exit 1
                fi
                STALE=$(find "$PROJECT_DIR" \
                    -path "$PROJECT_DIR/build" -prune -o \
                    -path "$PROJECT_DIR/build-tests" -prune -o \
                    -path "$PROJECT_DIR/.git" -prune -o \
                    -path "$PROJECT_DIR/logs" -prune -o \
                    -path "$PROJECT_DIR/legacy" -prune -o \
                    \( -name '*.cpp' -o -name '*.c' -o -name '*.h' -o -name '*.hpp' -o -name 'CMakeLists.txt' -o -name 'sdkconfig.defaults' \) \
                    -newer "$BINARY" -print -quit 2>/dev/null)
                if [ -n "$STALE" ]; then
                    echo "⚠️  Warning: Source file '$STALE' is newer than firmware binary."
                    echo "   Run './scripts/idf.sh build' for a fresh build."
                fi
                idf.py -p "$PORT" flash
                ;;
            monitor)
                timeout 30 python3 "$SCRIPT_DIR/monitor.py" "$PORT" || true
                ;;
            uart)
                timeout 30 python3 scripts/uart_test.py -p "$PORT"
                ;;
        esac
        ;;

    smoke)
        BINARY="$PROJECT_DIR/build/ecotiter.bin"
        if [ -f "$BINARY" ] && [ "${2:-}" != "--force-build" ]; then
            STALE=$(find "$PROJECT_DIR" \
                -path "$PROJECT_DIR/build" -prune -o \
                -path "$PROJECT_DIR/build-tests" -prune -o \
                -path "$PROJECT_DIR/.git" -prune -o \
                -path "$PROJECT_DIR/logs" -prune -o \
                -path "$PROJECT_DIR/legacy" -prune -o \
                \( -name '*.cpp' -o -name '*.c' -o -name '*.h' -o -name '*.hpp' -o -name 'CMakeLists.txt' -o -name 'sdkconfig.defaults' \) \
                -newer "$BINARY" -print -quit 2>/dev/null)
            if [ -z "$STALE" ]; then
                echo "Binary is up-to-date — skipping build"
            else
                do_build
            fi
        else
            do_build
        fi
        PORT=$(python3 "$SCRIPT_DIR/find_port.py") || {
            echo "❌ No ESP32 port found"
            exit 1
        }
        echo "Port: $PORT"
        idf.py -p "$PORT" flash
        sleep 0.3
        python3 "$SCRIPT_DIR/monitor.py" "$PORT" --timeout 30 --log-dir "$PROJECT_DIR/logs" --no-reset
        exit $?
        ;;

    test)
        test_cmd="${2:-run}"
        shift 2 2>/dev/null || true
        if [ ! -f "$PROJECT_DIR/build-tests/CMakeCache.txt" ]; then
            cmake -B "$PROJECT_DIR/build-tests" -S "$PROJECT_DIR/tests"
        fi
        cmake --build "$PROJECT_DIR/build-tests" 2>&1 | tail -1
        case "$test_cmd" in
            --build) ;;
            --list) "$PROJECT_DIR/build-tests/unit_tests" --list-tests ;;
            run)    "$PROJECT_DIR/build-tests/unit_tests" ;;
            --)     "$PROJECT_DIR/build-tests/unit_tests" "$@" ;;
            *)
                echo "Usage: $0 test {run|--build|--list|-- <filter>}"
                exit 1
                ;;
        esac
        ;;

    tidy)
        ./scripts/lint.sh "${@:2}"
        ;;

    full-tidy)
        ./scripts/lint.sh --full "${@:2}"
        ;;

    reconfigure)
        rm -f "$PROJECT_DIR/sdkconfig"
        idf.py reconfigure
        ;;

    clean)
        rm -rf build build-tests
        ;;

    erase-flash)
        PORT=$(resolve_port "${2:-}")
        echo "Port: $PORT"
        do_erase_flash "$PORT"
        ;;

    erase-nvs)
        PORT=$(resolve_port "${2:-}")
        echo "Port: $PORT"
        do_erase_nvs "$PORT"
        ;;

    help|--help|-h)
        echo "ecotiter C++23 firmware toolchain — single entry point"
        echo ""
        echo "PREREQUISITES"
        echo "  ESP-IDF v6.0.1 at \$IDF_PATH (default: ~/.espressif/v6.0.1/esp-idf)"
        echo "  C++23 toolchain (ESP-IDF clang-based)"
        echo "  cmake >= 3.16, Python 3.10+"
        echo ""
        echo "COMMANDS"
        echo "  build                 Clean build — removes build/, injects timestamp + git hash"
        echo "  flash [port]          Flash firmware (auto-detect port, stale source check)"
        echo "  monitor [port]        Serial monitor, 30s timeout"
        echo "  smoke [--force-build] Build (if stale) + semgrep + flash + 30s monitor"
        echo "  test                  Run host unit tests (Catch2)"
        echo "  test --build          Configure + build tests only, don't run"
        echo "  test --list           List test case names"
        echo "  test -- <filter>      Run specific tests (Catch2 wildcard)"
        echo   "  tidy                  clang-tidy static analysis (fast: ~2-5 min)"
  "  full-tidy             clang-tidy full analysis (~30-40 min, code-review only)"
        echo "  uart [port]           UART integration test"
        echo "  reconfigure           Remove sdkconfig + idf.py reconfigure"
        echo "  clean                 Remove build/ and build-tests/"
        echo "  erase-flash [port]    Erase ENTIRE flash chip (all data lost)"
        echo "  erase-nvs [port]      Erase NVS partition only (calibration, WiFi creds)"
        echo ""
        echo "EXAMPLES"
        echo "  ./scripts/idf.sh build                          # clean build"
        echo "  ./scripts/idf.sh flash                          # flash (auto-detect port)"
        echo "  ./scripts/idf.sh flash /dev/ttyUSB0             # flash (specific port)"
        echo "  ./scripts/idf.sh smoke                          # full pipeline test"
        echo "  ./scripts/idf.sh test -- \"dose*\"                # run dose-related tests"
        echo "  ./scripts/idf.sh clean                          # remove artifacts"
        echo "  ./scripts/idf.sh erase-flash                    # erase entire chip"
        echo "  ./scripts/idf.sh erase-nvs                      # erase NVS only"
        echo ""
        echo "SDK CONFIG POLICY"
        echo "  Edit only sdkconfig.defaults — never sdkconfig (auto-generated)"
        echo "  Never run idf.py menuconfig (not reproducible)"
        echo "  After changing defaults: ./scripts/idf.sh reconfigure"
        echo ""
        echo "NOTES"
        echo "  - Port auto-detection: VID/PID matching (CP2102, CH340, FTDI, ESP32-S3)"
        echo "  - Fallback port if auto-detect fails: /dev/ttyUSB0"
        echo "  - Each build embeds BUILD_DATE + GIT_HASH; shown in boot log and by flash"
        echo "  - flash warns if any source file is newer than the binary"
        echo ""
        echo "DOCS"
        echo "  docs/refs/coding_style.md   — C++23 conventions, RAII, error handling"
        echo "  docs/refs/project.md        — HW pinout, threads, network stack"
        echo "  AGENTS.md                    — golden rules (GR-1..GR-11), crash patterns"
        ;;

    *)
        echo "Usage: $0 {build|flash|monitor|smoke|test|tidy|full-tidy|uart|reconfigure|clean|erase-flash|erase-nvs|help}"
        exit 1
        ;;
esac
