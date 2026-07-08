#!/usr/bin/env python3
"""
OKF documentation validator.

Checks all .md files in docs/ for compliance with OKF v0.1 standards.
Exit code 0 = all good, 1 = errors found.
"""

import os
import re
import sys
import yaml
from pathlib import Path

DOCS_DIR = Path(__file__).parent.resolve()

ALLOWED_TYPES = {
    "Architecture Decision",
    "Architecture Reference",
    "Algorithm Reference",
    "User Journey",
    "Known Issue",
    "Plan",
    "Testing Guide",
    "Metric",
    "UI Rule",
    "ESP32 Reference",
    "Build Guide",
    "Code Review",
    "Docs Rule",
    "CrashReport",
}

SKIP_DIRS = frozenset({"esp_idf_v6"})

REQUIRED_FIELDS = {"type", "title", "description", "tags", "timestamp"}
ISO8601_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")
HEADING_RE = re.compile(r"^#{1,6}\s+")
EMOJI_RE = re.compile(
    "[\U0001F000-\U0001FFFF\U00002600-\U000027BF\U00002B50-\U00002B55\U0000FE00-\U0000FE0F]"
)
CYRILLIC_RE = re.compile("[\u0400-\u04FF\u0500-\u052F]")


def find_md_files(root: Path):
    for dirpath, _dirnames, filenames in os.walk(root):
        # skip hidden dirs and configured skip dirs
        parts = Path(dirpath).parts
        if any(part.startswith(".") for part in parts):
            continue
        if SKIP_DIRS & set(parts):
            continue
        for f in filenames:
            if not f.endswith(".md"):
                continue
            # skip reserved filenames
            if f in ("index.md", "log.md"):
                continue
            yield Path(dirpath) / f


def parse_frontmatter(content: str):
    """Return (frontmatter_dict, body_start_line) or (None, 0)."""
    lines = content.split("\n")
    if not lines or lines[0].strip() != "---":
        return None, 0
    end = None
    for i in range(1, len(lines)):
        if lines[i].strip() == "---":
            end = i
            break
    if end is None:
        return None, 0
    yaml_block = "\n".join(lines[1:end])
    try:
        data = yaml.safe_load(yaml_block)
    except yaml.YAMLError:
        return None, 0
    if not isinstance(data, dict):
        return None, 0
    return data, end + 1


def check_first_heading(content: str, body_start: int, filepath: Path):
    lines = content.split("\n")
    for i in range(body_start, len(lines)):
        line = lines[i].strip()
        if not line:
            continue
        if HEADING_RE.match(line):
            if not line.startswith("# "):
                print(
                    f"  ERROR: First heading must be '# Title', got: {line}",
                    file=sys.stderr,
                )
                return False
            if EMOJI_RE.search(line):
                print(
                    f"  ERROR: Heading contains emoji: {line}",
                    file=sys.stderr,
                )
                return False
            return True
    print("  ERROR: No heading found after frontmatter", file=sys.stderr)
    return False


def validate_file(filepath: Path) -> bool:
    ok = True
    content = filepath.read_text(encoding="utf-8")

    fm, body_start = parse_frontmatter(content)
    if fm is None:
        print(f"FAIL: {filepath.relative_to(DOCS_DIR)} — missing or invalid frontmatter")
        return False

    rel = filepath.relative_to(DOCS_DIR)

    for field in REQUIRED_FIELDS:
        if field not in fm:
            print(f"FAIL: {rel} — missing required field '{field}'")
            ok = False

    if "type" in fm and fm["type"] not in ALLOWED_TYPES:
        print(
            f"FAIL: {rel} — invalid type '{fm['type']}'. Allowed: {', '.join(sorted(ALLOWED_TYPES))}"
        )
        ok = False

    if "timestamp" in fm:
        ts = str(fm["timestamp"])
        if not ISO8601_RE.match(ts):
            print(f"FAIL: {rel} — timestamp '{ts}' is not ISO 8601 (expected YYYY-MM-DD)")
            ok = False

    if not check_first_heading(content, body_start, filepath):
        ok = False

    cyrillic_matches = list(CYRILLIC_RE.finditer(content))
    if cyrillic_matches:
        for m in cyrillic_matches[:10]:
            line_num = content[: m.start()].count("\n") + 1
            snippet = content[max(0, m.start() - 10) : m.end() + 10].replace("\n", " ")
            print(f"FAIL: {rel} — Cyrillic character '{m.group()}' at line {line_num}: ...{snippet}...")
        if len(cyrillic_matches) > 10:
            print(f"  ({len(cyrillic_matches) - 10} more Cyrillic character(s))")
        ok = False

    if ok:
        print(f"  OK: {rel}")
    return ok


def main():
    files = list(find_md_files(DOCS_DIR))
    if not files:
        print("No .md files found (excluding index.md, log.md)")
        return 0

    print(f"Checking {len(files)} file(s)...\n")
    errors = 0
    for f in sorted(files):
        if not validate_file(f):
            errors += 1

    print(f"\n{'=' * 40}")
    if errors:
        print(f"FAILED: {errors} file(s) with errors")
        return 1
    else:
        print("All files pass OKF validation")
        return 0


if __name__ == "__main__":
    sys.exit(main())
