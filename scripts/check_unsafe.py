#!/usr/bin/env python3
"""Check unsafe blocks in src/: documented + count against baseline.

Scans src/**/*.rs and:
1. Reports any `unsafe {` or `unsafe impl` without a `// SAFETY:` comment.
2. Counts total unsafe blocks and fails if count exceeds KNOWN_BASELINE.

Returns:
  Exit 0 — all checks pass.
  Exit 1 — undocumented unsafe blocks or count exceeds baseline.
"""

import re
import sys
from pathlib import Path


SRC_DIR = Path(__file__).resolve().parent.parent / "src"

# Known baseline: update when unsafe blocks are deliberately removed
KNOWN_BASELINE = 32

# Match // SAFETY:, // Safety:, /// # Safety (doc comment), // CHECKED_SAFE:
SAFETY_RE = re.compile(r"/[/*]\s*(#\s*)?(SAFETY|Safety|CHECKED_SAFE)[:\s]?")

# Match `unsafe {` or `unsafe impl` (not `unsafe fn`)
UNSAFE_RE = re.compile(r"\bunsafe\s*(\{|impl\b)")

# Lookback window in lines to find a SAFETY comment
LOOKBACK = 10


def check_file(path: Path) -> list[tuple[int, str]]:
    """Return list of (line_number, snippet) for undocumented unsafe blocks."""
    text = path.read_text()
    lines = text.split("\n")
    undocumented: list[tuple[int, str]] = []

    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith(("//", "/*", "*")):
            continue
        if not UNSAFE_RE.search(stripped):
            continue

        window = range(max(0, i - LOOKBACK), min(len(lines), i + 2))
        has_safety = any(SAFETY_RE.search(lines[j]) for j in window)

        if not has_safety:
            undocumented.append((i + 1, stripped[:80]))

    return undocumented


def count_unsafe(path: Path) -> int:
    """Count unsafe blocks in a file, ignoring comment lines."""
    text = path.read_text()
    real_lines = [
        l for l in text.splitlines()
        if not l.strip().startswith(("//", "/*", "*"))
    ]
    return len(UNSAFE_RE.findall("\n".join(real_lines)))


def main() -> int:
    undocumented_total = 0
    all_undocumented: list[tuple[Path, int, str]] = []

    count_total = 0
    file_counts: dict[Path, int] = {}

    for rs_file in sorted(SRC_DIR.rglob("*.rs")):
        rel = rs_file.relative_to(SRC_DIR.parent)

        # Check 1: undocumented blocks
        undocumented = check_file(rs_file)
        undocumented_total += len(undocumented)
        for line_num, snippet in undocumented:
            all_undocumented.append((rel, line_num, snippet))

        # Check 2: count
        cnt = count_unsafe(rs_file)
        if cnt > 0:
            file_counts[rel] = cnt
        count_total += cnt

    # ── Report ─────────────────────────────────────────────────
    exit_code = 0

    print(f"Unsafe blocks: {count_total} (baseline {KNOWN_BASELINE})")

    for f, c in sorted(file_counts.items(), key=lambda x: -x[1]):
        print(f"  {f}: {c}")

    # Check baseline
    if count_total > KNOWN_BASELINE:
        print(
            f"\nERROR: Unsafe block count {count_total} exceeds baseline "
            f"{KNOWN_BASELINE} (increase of {count_total - KNOWN_BASELINE})."
        )
        print("New unsafe blocks must be justified in the commit message.")
        print(f"Update KNOWN_BASELINE in {__file__} if intentional.")
        exit_code = 1

    # Check documentation
    if undocumented_total > 0:
        print(f"\nUndocumented unsafe blocks found ({undocumented_total}):")
        for rel, line_num, snippet in all_undocumented:
            print(f"  {rel}:{line_num}  {snippet}")
        print("\nEach unsafe block must have a `// SAFETY:` or `// Safety:`")
        print("comment within 10 lines above explaining why it is safe.")
        exit_code = 1

    if exit_code == 0:
        print(
            f"OK: {count_total} unsafe blocks (within baseline "
            f"{KNOWN_BASELINE}), all documented."
        )

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
