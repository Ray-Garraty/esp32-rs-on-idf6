#!/usr/bin/env bash
# Recommended timeout: 600s (full mode includes build + tidy + serial test)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
export PATH="$HOME/.local/bin:$PATH"

fast_mode=false
[ "${1:-}" = "--fast" ] && fast_mode=true

# --- Locate clang-format (esp-clang preferred) ---
CLANG_FORMAT=""
for candidate in \
  ~/.espressif/tools/esp-clang/*/esp-clang/bin/clang-format \
  /usr/bin/clang-format-{18,17,19} \
  /usr/bin/clang-format; do
  cand_exp=$(eval echo "$candidate")
  if [[ -x "$cand_exp" ]]; then
    CLANG_FORMAT="$cand_exp"
    break
  fi
done

# --- Staged files ---
STAGED=$(git diff --cached --name-only --diff-filter=ACMR 2>/dev/null || true)

cpp_files() {
  echo "$STAGED" | grep -E '\.(cpp|h|hpp)$' || true
}

# ============================================================
echo "=== 1. Staged files: merge conflict check ==="
conflicts=$(cpp_files | xargs -r grep -ln '<<<<<<< \|=======\|>>>>>>> ' 2>/dev/null || true)
if [[ -n "$conflicts" ]]; then
  echo "  ❌ FAIL: Merge conflict markers in:"
  echo "$conflicts" | sed 's/^/    /'
  echo ""
  echo "  Fix and re-stage before committing."
  exit 1
fi
echo "  ✅ No merge conflict markers"

# ============================================================
echo "=== 2. Staged files: trailing whitespace ==="
STAGED_ALL=$(echo "$STAGED" | grep -v '^$' || true)
ws_files=$(echo "$STAGED_ALL" | xargs -r grep -l '[[:space:]]$' 2>/dev/null || true)
if [[ -n "$ws_files" ]]; then
  echo "  ❌ FAIL: Trailing whitespace in:"
  echo "$ws_files" | sed 's/^/    /'
  echo ""
  echo "  Fix with:"
  echo "    echo \"$ws_files\" | xargs sed -i 's/[[:space:]]*$//'"
  echo "    git add $(echo "$ws_files" | tr '\n' ' ')"
  exit 1
fi
echo "  ✅ No trailing whitespace"

# ============================================================
CPP_STAGED=$(cpp_files)

if [[ -z "$CPP_STAGED" ]]; then
  echo "=== 3. Format check ==="
  echo "  No staged C++ files — skipping"
else
  if [[ -z "$CLANG_FORMAT" ]]; then
    echo "=== 3. Format check ==="
    echo "  ❌ FAIL: clang-format not found"
    echo "  Install esp-clang or system clang-format."
    exit 1
  fi
  echo "=== 3. Format check ($CLANG_FORMAT) ==="
  echo "$CPP_STAGED" | xargs "$CLANG_FORMAT" --dry-run --Werror
  echo "  ✅ All staged C++ files are formatted"
fi

# ============================================================
echo "=== 4. Semgrep (main loop blocking) ==="
if ! command -v semgrep &>/dev/null; then
  echo "  ❌ FAIL: semgrep not installed"
  echo "  Install: pip install --break-system-packages semgrep"
  exit 1
fi
semgrep --config "$PROJECT_DIR/.semgrep/main_loop_blocking.yaml" --error "$PROJECT_DIR/main/main.cpp"
echo "  ✅ No main loop blocking violations"

# ============================================================
echo "=== 5. Host unit tests ==="
"$SCRIPT_DIR/idf.sh" test
echo "  ✅ All unit tests pass"

# ============================================================
echo "=== 6. Docs OKF validation ==="
python "$PROJECT_DIR/docs/validate_okf.py"
echo "  ✅ All docs valid"

# ============================================================
echo "=== 7. sdkconfig constraint ==="
SDKCONFIG="$PROJECT_DIR/sdkconfig.defaults"
if [[ ! -f "$SDKCONFIG" ]]; then
  echo "  ❌ FAIL: $SDKCONFIG not found"
  exit 1
fi
rx_ba_win=$(grep -E '^CONFIG_ESP_WIFI_RX_BA_WIN=' "$SDKCONFIG" | cut -d= -f2)
dyn_rx_buf=$(grep -E '^CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=' "$SDKCONFIG" | cut -d= -f2)
if [[ -z "$rx_ba_win" || -z "$dyn_rx_buf" ]]; then
  echo "  ❌ FAIL: Cannot read CONFIG_ESP_WIFI_RX_BA_WIN or CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM"
  echo "  Check that both are defined in $SDKCONFIG"
  exit 1
fi
if [[ "$rx_ba_win" -gt "$dyn_rx_buf" ]]; then
  echo "  ❌ FAIL: CONFIG_ESP_WIFI_RX_BA_WIN ($rx_ba_win) > CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM ($dyn_rx_buf)"
  echo "  RX_BA_WIN must be ≤ DYNAMIC_RX_BUFFER_NUM to prevent wifi_init compile error."
  exit 1
fi
echo "  ✅ CONFIG_ESP_WIFI_RX_BA_WIN ($rx_ba_win) ≤ CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM ($dyn_rx_buf)"

# ============================================================
# Full mode
if [ "$fast_mode" = false ]; then
  echo "=== 8. Smoke test (build + flash + 70s monitor) ==="
  "$SCRIPT_DIR/idf.sh" smoke
  echo "  ✅ Smoke test passed"

  echo "=== 9. Stack watermark check ==="
  if ls "$PROJECT_DIR"/logs/serial_*.log >/dev/null 2>&1; then
      python3 "$SCRIPT_DIR/check_watermarks.py"
  else
      echo "  ⏭️  No serial log found — skipping watermark check"
  fi

  echo "=== 10. clang-tidy ==="
  if [[ ! -f "$PROJECT_DIR/build/compile_commands.json" ]]; then
    echo "  ❌ FAIL: build/compile_commands.json not found"
    echo "  Run build first or check build output."
    exit 1
  fi
  "$SCRIPT_DIR/idf.sh" tidy
  echo "  ✅ clang-tidy clean"

  echo "=== 11. Serial API hardware test ==="
  if [[ -x "$PROJECT_DIR/scripts/testing/serial_api_test.py" ]]; then
    timeout 60 python3 "$PROJECT_DIR/scripts/testing/serial_api_test.py" || true
  fi
fi

# ============================================================
echo ""
echo "=== All checks passed ==="
