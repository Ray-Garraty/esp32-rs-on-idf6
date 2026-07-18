#!/usr/bin/env python3
"""
OKF documentation validator.

Checks .md files in docs/ for compliance with OKF v0.1 standards.
Also checks .yaml lessons-learned files.
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
    "Hardware Reference",
    "Build Guide",
    "Code Review",
    "CrashReport",
    "Docs Rule",
}

SKIP_DIRS = frozenset({"esp_idf_v6"})
SKIP_FILES = frozenset({"refs/esp32-s3_datasheet_en.md"})

REQUIRED_FIELDS = {"type", "title", "description", "tags", "timestamp"}
LL_REQUIRED_FIELDS = {"id", "date", "category", "title", "lesson"}
LL_ID_RE = re.compile(r"^LL-\d{3}$")
ISO8601_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")
HEADING_RE = re.compile(r"^#{1,6}\s+")
EMOJI_RE = re.compile(
    "[\U0001F000-\U0001FFFF\U00002600-\U000027BF\U00002B50-\U00002B55\U0000FE00-\U0000FE0F]"
)
CYRILLIC_RE = re.compile("[\u0400-\u04FF\u0500-\u052F]")

LESSONS_DIR = DOCS_DIR / "lessons_learned"


def find_doc_files(root: Path):
    """Yield (filepath, kind) where kind is 'md' or 'll'."""
    for dirpath, _dirnames, filenames in os.walk(root):
        parts = Path(dirpath).parts
        if any(part.startswith(".") for part in parts):
            continue
        if SKIP_DIRS & set(parts):
            continue
        for f in filenames:
            if f.endswith(".md"):
                if f in ("index.md", "log.md"):
                    continue
                filepath = Path(dirpath) / f
                rel = filepath.relative_to(root)
                if str(rel) in SKIP_FILES:
                    continue
                yield filepath, "md"
            elif f.endswith(".yaml") and Path(dirpath) == LESSONS_DIR:
                yield Path(dirpath) / f, "ll"


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


def check_cyrillic(content: str, rel: Path) -> list[str]:
    """Return error lines for any Cyrillic characters in content."""
    errors = []
    for m in CYRILLIC_RE.finditer(content):
        line_num = content[: m.start()].count("\n") + 1
        snippet = content[max(0, m.start() - 10) : m.end() + 10].replace("\n", " ")
        errors.append(
            f"FAIL: {rel} — Cyrillic character '{m.group()}' at line {line_num}: ...{snippet}..."
        )
    return errors


def _check_iso8601(value: str, field: str, rel: Path) -> bool:
    """Print error and return True if value is not ISO 8601."""
    if not ISO8601_RE.match(value):
        print(f"FAIL: {rel} — {field} '{value}' is not ISO 8601 (expected YYYY-MM-DD)")
        return True
    return False


def validate_md(filepath: Path) -> bool:
    ok = True
    rel = filepath.relative_to(DOCS_DIR)
    content = filepath.read_text(encoding="utf-8")

    fm, body_start = parse_frontmatter(content)
    if fm is None:
        print(f"FAIL: {rel} — missing or invalid frontmatter")
        return False

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
        if _check_iso8601(str(fm["timestamp"]), "timestamp", rel):
            ok = False

    if not check_first_heading(content, body_start, filepath):
        ok = False

    for err in check_cyrillic(content, rel):
        print(err)
        ok = False

    return ok


def validate_ll(filepath: Path) -> bool:
    ok = True
    rel = filepath.relative_to(DOCS_DIR)
    content = filepath.read_text(encoding="utf-8")

    try:
        data = yaml.safe_load(content)
    except yaml.YAMLError as e:
        print(f"FAIL: {rel} — YAML parse error: {e}")
        return False

    if not isinstance(data, dict):
        print(f"FAIL: {rel} — not a YAML mapping")
        return False

    for field in LL_REQUIRED_FIELDS:
        if field not in data:
            print(f"FAIL: {rel} — missing required field '{field}'")
            ok = False

    if "id" in data:
        lid = str(data["id"])
        if not LL_ID_RE.match(lid):
            print(f"FAIL: {rel} — id '{lid}' does not match LL-NNN pattern")
            ok = False

    if "date" in data:
        if _check_iso8601(str(data["date"]), "date", rel):
            ok = False

    for err in check_cyrillic(content, rel):
        print(err)
        ok = False

    return ok


def main():
    files = list(find_doc_files(DOCS_DIR))
    if not files:
        print("No files found to check")
        return 0

    print(f"Checking {len(files)} file(s)...\n")
    errors = 0
    for fpath, kind in sorted(files, key=lambda x: x[0]):
        if kind == "md":
            if not validate_md(fpath):
                errors += 1
        elif kind == "ll":
            if not validate_ll(fpath):
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
