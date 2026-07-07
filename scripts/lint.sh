#!/usr/bin/env bash
set -euo pipefail

# Lint wrapper — find clang-tidy, run against IDF build + host tests.
# Usage:
#   ./scripts/lint.sh [--fix] [--all] [file.cpp ...]

FIX=false
if [[ "${1:-}" == "--fix" ]]; then
  FIX=true
  shift
fi

SHOW_ALL=false
if [[ "${1:-}" == "--all" ]]; then
  SHOW_ALL=true
  shift
fi

# Locate clang-tidy (esp-clang preferred for target code)
CLANG_TIDY=""
for candidate in \
  ~/.espressif/tools/esp-clang/*/esp-clang/bin/clang-tidy \
  /usr/bin/clang-tidy-{18,17,19} \
  /usr/bin/clang-tidy; do
  cand_exp=$(eval echo "$candidate")
  if [[ -x "$cand_exp" ]]; then
    CLANG_TIDY="$cand_exp"
    break
  fi
done

if [[ -z "$CLANG_TIDY" ]]; then
  echo "ERROR: clang-tidy not found. Install it or source ESP-IDF."
  exit 1
fi

echo "=== Linter: $CLANG_TIDY ==="

# --- Check build directories ---
if [[ ! -f build/compile_commands.json ]]; then
  echo "ERROR: build/compile_commands.json not found. Run 'idf.py build' first."
  echo "  (or: ./scripts/build.sh)"
  exit 1
fi

# Generate test build compile_commands if missing
if [[ ! -f build-tests/compile_commands.json ]]; then
  echo ">> Generating build-tests/compile_commands.json..."
  cmake -B build-tests -S tests -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /dev/null 2>&1
fi

# --- Determine files to lint ---
if [[ $# -gt 0 ]]; then
  FILES=("$@")
else
  # Default: all application, interface, domain, infrastructure src files
  FILES=()
  while IFS= read -r -d '' f; do
    FILES+=("$f")
  done < <(
    find components/application/src \
         components/interface/src \
         components/domain/src \
         components/infrastructure/src \
         components/diag/src \
         main \
         -name '*.cpp' -type f -print0 2>/dev/null
  )
fi

if [[ ${#FILES[@]} -eq 0 ]]; then
  echo "No source files found to lint."
  exit 0
fi

ARGS="-p build/"
if $FIX; then
  ARGS="$ARGS --fix-errors"
fi

HAS_ERROR=false
HAS_WARNING=false

run_tidy() {
  local compile_db="$1"
  shift
  local files=("$@")

  # Filter: only files that exist in the compile_commands.json
  local valid_files=()
  for f in "${files[@]}"; do
    if grep -q "\"$f\"" "$compile_db/compile_commands.json" 2>/dev/null; then
      valid_files+=("$f")
    fi
  done

  if [[ ${#valid_files[@]} -eq 0 ]]; then
    return 0
  fi

  echo ">> Linting ${#valid_files[@]} file(s) with $compile_db ..."

  # Run clang-tidy
  # Suppress do-while-bugprone from catch2 macros via env
  local output
  output=$("$CLANG_TIDY" -p "$compile_db" "${valid_files[@]}" 2>&1) || true

  # Filter output
  local filtered
  filtered=$(echo "$output" | grep -v "^Suppressed " \
    | grep -v "^Use -header-filter" \
    | grep -v "^Found compiler error" \
    | grep -v "unknown argument:" \
    || true)

  # Show only non-NOLINT warnings, skip third-party headers
  local warnings
  warnings=$(echo "$filtered" \
    | grep -E "^(/.*)?warning:" \
    | grep -v "NOLINT" \
    | grep -v "catch2/" \
    | grep -v "nlohmann/" \
    || true)

  local errors
  errors=$(echo "$filtered" \
    | grep -E "^.*error:" \
    | grep -v "unknown argument:" \
    || true)

  # Count unique warning types
  local warn_count=0
  if [[ -n "$warnings" ]]; then
    warn_count=$(echo "$warnings" | wc -l)
  fi

  local err_count=0
  if [[ -n "$errors" ]]; then
    err_count=$(echo "$errors" | wc -l)
  fi

  if [[ "$warn_count" -gt 0 ]]; then
    HAS_WARNING=true
    echo ""
    echo "--- Warnings ($warn_count) ---"
    echo "$warnings" | sort -u
  fi

  if [[ "$err_count" -gt 0 ]]; then
    HAS_ERROR=true
    echo ""
    echo "--- Errors ($err_count) ---"
    echo "$errors"
  fi

  if [[ "$warn_count" -eq 0 && "$err_count" -eq 0 ]]; then
    echo "   ✅ Clean"
  fi

  echo ""
}

# Separate test files vs production files
TEST_FILES=()
PROD_FILES=()

for f in "${FILES[@]}"; do
  if [[ "$f" == tests/* ]]; then
    TEST_FILES+=("$f")
  else
    PROD_FILES+=("$f")
  fi
done

# Lint production files against IDF build
if [[ ${#PROD_FILES[@]} -gt 0 ]]; then
  run_tidy "build" "${PROD_FILES[@]}"
fi

# Lint test files against test build
if [[ ${#TEST_FILES[@]} -gt 0 ]]; then
  if [[ ! -f build-tests/compile_commands.json ]]; then
    echo ">> Skipping test files — build-tests/ not available"
  else
    run_tidy "build-tests" "${TEST_FILES[@]}"
  fi
fi

# Summary
echo "=== Summary ==="
if $HAS_ERROR; then
  echo "  ❌ Errors found"
  exit 1
elif $HAS_WARNING; then
  if $SHOW_ALL; then
    echo "  ⚠️  Warnings found (non-blocking)"
  else
    echo "  ⚠️  Warnings found (run with --all to see details)"
  fi
  exit 0
else
  echo "  ✅ Clean"
  exit 0
fi
