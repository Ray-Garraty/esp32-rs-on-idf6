#!/usr/bin/env python3
"""
ESP32 Guru Meditation Crash Analyzer.

Parses Guru Meditation dump from stdin or file, decodes backtrace via
addr2line, classifies the crash, and checks against lessons_learned.yaml.

Usage:
    cat crash.txt | python3 scripts/crash_analyzer.py
    python3 scripts/crash_analyzer.py --dump crash.txt
    python3 scripts/crash_analyzer.py --dump crash.txt --elf target/.../ecotiter

Output: YAML to stdout.
"""

import argparse
import os
import re
import subprocess
import sys
import yaml
from pathlib import Path
from typing import Any

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent
LESSONS_FILE = PROJECT_DIR / "docs" / "lessons_learned.yaml"

# ── Parsing patterns ────────────────────────────────────────────────

RE_GURU_HEADER = re.compile(
    r"Guru Meditation Error:\s*Core\s+\d+\s+panic'ed\s*\((\w+)\)"
)
RE_EXCCAUSE = re.compile(r"EXCCAUSE\s*:\s*0x([0-9a-fA-F]+)")
RE_EXCVADDR = re.compile(r"EXCVADDR\s*:\s*0x([0-9a-fA-F]+)")
RE_EPC1 = re.compile(r"epc1\s*:\s*0x([0-9a-fA-F]+)")
RE_BACKTRACE = re.compile(
    r"Backtrace:\s*((?:0x[0-9a-fA-F]+:\s*0x[0-9a-fA-F]+\s*)+)"
)
RE_HEX_ADDR = re.compile(r"0x([0-9a-fA-F]{8})")
RE_REGISTER = re.compile(
    r"\b([A-Z]+\d?)\s*:\s*0x([0-9a-fA-F]+)"
)
RE_WDT = re.compile(r"rst:0x8\s*\(TG1WDT_SYS_RESET\)")
RE_STACK_OVERFLOW = re.compile(
    r"\*\*\*ERROR\*\*\* A stack overflow in task (\S+) has been detected"
)
RE_ABORT_CALLED = re.compile(
    r"abort\(\) was called at PC (0x[0-9a-fA-F]+)"
)

# ── New CRASH format patterns (diag subsystem) ──
RE_CRASH_HEADER = re.compile(r"=== CRASH ===")
RE_CRASH_FIELDS = re.compile(
    r"exccause=(\d+)\s+name=(\S+)\s+pc=(0x[0-9a-fA-F]+)\s+"
    r"excvaddr=(0x[0-9a-fA-F]+)\s+ps=(0x[0-9a-fA-F]+)\s+sp=(0x[0-9a-fA-F]+)"
)
RE_BT_HEADER = re.compile(r"=== BACKTRACE ===")
RE_BT_LINE = re.compile(r"(0x[0-9a-fA-F]+):(0x[0-9a-fA-F]+)")
RE_BB_HEADER = re.compile(r"=== BLACK BOX")
RE_STACK_HEADER = re.compile(r"=== STACK ===")
RE_STACK_LINE = re.compile(r"t(\d+)\s+(\S+)\s+watermark=(\d+)")

EXCCAUSE_DESCRIPTIONS: dict[int, str] = {
    0x00: "IllegalInstruction — fetch of invalid instruction or misaligned PC",
    0x01: "Syscall — LICT (user) syscall executed",
    0x02: "InstructionFetchError — error during instruction fetch",
    0x03: "LoadStoreError — load/store address error (unaligned or invalid)",
    0x04: "Level1Interrupt — level-1 interrupt as exception",
    0x05: "Alloca — MOVSP caused invalid stack pointer",
    0x06: "IntegerDivideByZero — divide by zero in windowed ABI",
    0x07: "PCValue — PSR has wrong value (PC<->PS mapping)",
    0x08: "Privileged — privileged instruction in user mode",
    0x09: "LoadStoreAlignment — unaligned load/store (no unaligned support)",
    0x1C: "LoadProhibited — load from unmapped address",
    0x1D: "StoreProhibited — store to unmapped address",
    0x20: "Cp0Dis",
    0x21: "Cp1Dis",
    0x22: "Cp2Dis",
    0x23: "Cp3Dis",
    0x24: "Cp4Dis",
    0x25: "Cp5Dis",
    0x26: "Cp6Dis",
    0x27: "Cp7Dis",
}


def parse_crash_dump(text: str) -> dict[str, Any]:
    # Detect which format: new === CRASH === format or old Guru Meditation
    if RE_CRASH_HEADER.search(text):
        return _parse_new_format(text)
    else:
        return _parse_old_format(text)


def _parse_new_format(text: str) -> dict[str, Any]:
    """Parse the new === CRASH === format."""
    info: dict[str, Any] = {
        "type": "unknown",
        "excvaddr": None,
        "excause": None,
        "pc": None,
        "epc1": None,
        "backtrace_raw": [],
        "registers": {},
        "excause_description": None,
        "wdt_reset": False,
        "stack_overflow_task": None,
        "stack_watermarks": [],
        "bb_events": [],
    }

    # Parse CRASH fields: exccause=N name=XXX pc=0x... ...
    m = RE_CRASH_FIELDS.search(text)
    if m:
        info["excause"] = int(m.group(1))
        info["type"] = m.group(2)  # e.g. "IllegalInstruction"
        info["pc"] = int(m.group(3), 16)
        info["excvaddr"] = int(m.group(4), 16)
        info["excause_description"] = EXCCAUSE_DESCRIPTIONS.get(
            int(m.group(1)), f"Unknown EXCCAUSE (0x{int(m.group(1)):02X})"
        )

    # Parse backtrace — only PC:SP pairs within === BACKTRACE === section
    bt_section = RE_BT_HEADER.split(text)
    if len(bt_section) > 1:
        bt_text = bt_section[1]
        # Stop at the next === section header or !!! marker
        end_section = re.search(r"\n===", bt_text)
        if end_section:
            bt_text = bt_text[:end_section.start()]
        end_marker = bt_text.find("!!!")
        if end_marker >= 0:
            bt_text = bt_text[:end_marker]
        for m in RE_BT_LINE.finditer(bt_text):
            info["backtrace_raw"].append({
                "pc": int(m.group(1), 16),
                "sp": int(m.group(2), 16),
            })

    # Parse stack watermarks
    st_section = RE_STACK_HEADER.split(text)
    if len(st_section) > 1:
        st_text = st_section[1]
        for m in RE_STACK_LINE.finditer(st_text):
            info["stack_watermarks"].append({
                "task_id": int(m.group(1)),
                "name": m.group(2),
                "watermark": int(m.group(3)),
            })

    # Parse black box events
    info["bb_events"] = parse_bb_events(text)

    return info


def _parse_old_format(text: str) -> dict[str, Any]:
    """Parse the old Guru Meditation format (backward compatible)."""
    info: dict[str, Any] = {
        "type": "unknown",
        "excvaddr": None,
        "excause": None,
        "pc": None,
        "epc1": None,
        "backtrace_raw": [],
        "registers": {},
        "excause_description": None,
        "wdt_reset": False,
        "stack_overflow_task": None,
    }

    # WDT reset
    m = RE_WDT.search(text)
    if m:
        info["wdt_reset"] = True

    # Stack overflow detection
    m = RE_STACK_OVERFLOW.search(text)
    if m:
        info["stack_overflow_task"] = m.group(1)

    # ESP-IDF abort
    m = RE_ABORT_CALLED.search(text)
    if m:
        info["abort_pc"] = int(m.group(1), 16)
        info["pc"] = info["abort_pc"]

    # Guru Meditation type
    m = RE_GURU_HEADER.search(text)
    if m:
        info["type"] = m.group(1)

    # EXCCAUSE
    m = RE_EXCCAUSE.search(text)
    if m:
        val = int(m.group(1), 16)
        info["excause"] = val
        info["excause_description"] = EXCCAUSE_DESCRIPTIONS.get(
            val, f"Unknown EXCCAUSE (0x{val:02X})"
        )

    # EXCVADDR
    m = RE_EXCVADDR.search(text)
    if m:
        info["excvaddr"] = int(m.group(1), 16)

    # PC
    m = RE_EPC1.search(text)
    if m:
        info["epc1"] = int(m.group(1), 16)
        info["pc"] = info["epc1"]

    # Registers
    for m in RE_REGISTER.finditer(text):
        reg = m.group(1)
        val = int(m.group(2), 16)
        info["registers"][reg] = val
    if "A2" in info["registers"]:
        info["a2"] = info["registers"]["A2"]

    # Backtrace (raw hex addresses)
    m = RE_BACKTRACE.search(text)
    if m:
        pairs = m.group(1).strip().split()
        for pair in pairs:
            if ":" in pair:
                pc_str, sp_str = pair.split(":", 1)
                info["backtrace_raw"].append({
                    "pc": int(pc_str, 16),
                    "sp": int(sp_str, 16),
                })

    return info


def decode_backtrace(info: dict[str, Any], elf_path: str) -> list[dict[str, Any]]:
    """Run addr2line on backtrace PCs."""
    if not info["backtrace_raw"]:
        return []

    # Collect unique PCs
    pcs = set(bt["pc"] for bt in info["backtrace_raw"])

    # Find addr2line tool
    ad2_path = _find_addr2line()
    if not ad2_path:
        return _make_placeholder(info)

    if not os.path.isfile(elf_path):
        return _make_placeholder(info, error=f"ELF not found at {elf_path}")

    try:
        hex_addrs = [f"0x{pc:08x}" for pc in sorted(pcs)]
        result = subprocess.run(
            [ad2_path, "-pfiaC", "-e", elf_path] + hex_addrs,
            capture_output=True, text=True, timeout=30,
        )
        decoded = {}
        if result.returncode == 0:
            for line in result.stdout.splitlines():
                m = re.match(
                    r'^0x([0-9a-fA-F]+):\s+(.+?)\s+at\s+(.+?):(\d+)',
                    line.strip()
                )
                if m:
                    addr = int(m.group(1), 16)
                    func = m.group(2).strip()
                    file = m.group(3).strip()
                    line_no = int(m.group(4))
                    decoded[addr] = {
                        "function": func,
                        "file": file,
                        "line": line_no,
                    }
                else:
                    # Try simpler format: "function at file:line"
                    m2 = re.match(
                        r'^\s*(.+?)\s+at\s+(.+?):(\d+)', line.strip()
                    )
                    if m2:
                        continue  # skip, no address prefix
                    # For lines starting with "0x...:" but different format
                    m3 = re.match(
                        r'^0x([0-9a-fA-F]+):\s+(.+)', line.strip()
                    )
                    if m3:
                        addr = int(m3.group(1), 16)
                        decoded[addr] = {
                            "function": m3.group(2).strip(),
                            "file": "?",
                            "line": 0,
                        }

        backtrace_decoded = []
        for bt in info["backtrace_raw"]:
            pc = bt["pc"]
            entry: dict[str, Any] = {
                "address": pc,
                "function": decoded.get(pc, {}).get("function", "??"),
                "file": decoded.get(pc, {}).get("file", "??"),
                "line": decoded.get(pc, {}).get("line", 0),
            }
            backtrace_decoded.append(entry)
        return backtrace_decoded

    except (subprocess.TimeoutExpired, OSError) as e:
        return _make_placeholder(info, error=str(e))


def _find_addr2line() -> str | None:
    """Find xtensa-esp32-elf-addr2line or xtensa-esp32s3-elf-addr2line."""
    candidates = ["xtensa-esp32-elf-addr2line", "xtensa-esp32s3-elf-addr2line"]
    # Check ESP-IDF tools directory
    tools_root = Path.home() / ".espressif" / "tools"
    if tools_root.exists():
        for name in candidates:
            for d in sorted(tools_root.rglob(name)):
                if d.is_file():
                    return str(d)
    # Check PATH
    for p in os.environ.get("PATH", "").split(os.pathsep):
        for name in candidates:
            exe = os.path.join(p, name)
            if os.path.isfile(exe):
                return exe
    return None


def _make_placeholder(
    info: dict[str, Any], error: str | None = None,
) -> list[dict[str, Any]]:
    """Return backtrace entries with raw addresses when addr2line unavailable."""
    result = []
    for bt in info["backtrace_raw"]:
        entry: dict[str, Any] = {
            "address": bt["pc"],
            "function": "??",
            "file": "??",
            "line": 0,
        }
        if error:
            entry["decode_error"] = error
        result.append(entry)
    return result


def classify_crash(
    info: dict[str, Any],
    backtrace_decoded: list[dict[str, Any]],
) -> dict[str, Any]:
    """Classify the crash into a high-level category."""
    classification: dict[str, Any] = {
        "category": "unknown",
        "pattern": "",
        "confidence": "low",
    }

    # Stack overflow (explicit FreeRTOS detection)
    if info.get("stack_overflow_task"):
        classification["category"] = "stack_overflow"
        classification["pattern"] = (
            f"FreeRTOS stack overflow detected in task '{info['stack_overflow_task']}'"
        )
        classification["confidence"] = "high"
        return classification

    # WDT reset
    if info.get("wdt_reset"):
        classification["category"] = "wdt_timeout"
        classification["pattern"] = "TG1WDT_SYS_RESET — blocking call in main loop"
        classification["confidence"] = "high"
        return classification

    crash_type = info.get("type", "")
    excvaddr = info.get("excvaddr")
    a2 = info.get("a2")

    # LoadProhibited / StoreProhibited
    if crash_type in ("LoadProhibited", "StoreProhibited"):
        # TLSF corruption (A2=0xFFFFFFFC) — check FIRST, this trumps NULL deref
        # because the TLSF block_next pointer was corrupted by stack overflow,
        # causing the allocator to read from address 0 (corrupted next->next).
        if a2 is not None and a2 in (0xFFFFFFFC, 0xFFFFFFF8, 0xFFFFFFF0):
            classification["category"] = "heap_corruption"
            classification["pattern"] = (
                f"A2=0x{a2:08x} — TLSF free-list pointer corrupted. "
                f"Likely main task stack overflow (see LL-001)."
            )
            classification["confidence"] = "high"
        # NULL dereference
        elif excvaddr == 0:
            classification["category"] = "null_deref"
            classification["pattern"] = (
                f"{crash_type} at EXCVADDR=0x00000000 — NULL pointer dereference"
            )
            classification["confidence"] = "high" if a2 is not None else "medium"
        # NULL + small offset (struct field access on NULL)
        elif excvaddr and excvaddr < 0x1000:
            classification["category"] = "null_deref"
            classification["pattern"] = (
                f"{crash_type} at EXCVADDR=0x{excvaddr:08x} "
                f"— struct field access on NULL pointer"
            )
            classification["confidence"] = "medium"
        # DRAM range
        elif excvaddr and 0x3FFB0000 <= excvaddr <= 0x3FFE0000:
            classification["category"] = "heap_corruption"
            classification["pattern"] = (
                f"{crash_type} at EXCVADDR=0x{excvaddr:08x} "
                f"— access within DRAM/heap region"
            )
            classification["confidence"] = "medium"
        else:
            classification["category"] = "memory_access"
            classification["pattern"] = (
                f"{crash_type} at EXCVADDR=0x{excvaddr:08x}"
            )
            classification["confidence"] = "low"

        # Check if backtrace suggests heap allocator
        if backtrace_decoded:
            bt_functions = [
                b.get("function", "") for b in backtrace_decoded
            ]
            bt_text = " ".join(bt_functions)
            if any(fn in bt_text for fn in (
                "tlsf", "multi_heap", "heap_caps", "pvPortMalloc",
                "free", "malloc",
            )):
                classification["category"] = "heap_corruption"
                classification["pattern"] += " [backtrace confirms allocator crash]"
                classification["confidence"] = "high"

        return classification

    # Integer errors
    if crash_type in ("IntegerDivideByZero", "IntegerOverflow", "IntegerInvalid"):
        classification["category"] = "arithmetic_error"
        classification["pattern"] = crash_type
        classification["confidence"] = "high"
        return classification

    return classification


def check_lessons_learned(
    info: dict[str, Any],
    classification: dict[str, Any],
    raw_text: str = "",
) -> list[dict[str, Any]]:
    """Check classified crash against lessons_learned.yaml."""
    if not LESSONS_FILE.exists():
        return []

    try:
        with open(LESSONS_FILE) as f:
            data = yaml.safe_load(f)
    except (yaml.YAMLError, OSError):
        return []

    if not data or "lessons" not in data:
        return []

    # Build searchable text from info + raw crash dump
    searchable = raw_text + " " + yaml.dump(info)

    matches = []
    for lesson in data["lessons"]:
        # Check trigger patterns against searchable text
        for pattern in lesson.get("trigger_patterns", []):
            try:
                if re.search(pattern, searchable, re.IGNORECASE):
                    matches.append({
                        "id": lesson.get("id"),
                        "match": True,
                        "lesson": lesson.get("lesson", ""),
                        "diagnostic": lesson.get("diagnostic", ""),
                        "fix": lesson.get("fix", ""),
                    })
                    break
            except re.error:
                pass

        # Check category match (if not already matched by pattern)
        if lesson.get("category") == classification.get("category"):
            if not any(m["id"] == lesson.get("id") for m in matches):
                matches.append({
                    "id": lesson.get("id"),
                    "match": True,
                    "lesson": lesson.get("lesson", ""),
                    "diagnostic": lesson.get("diagnostic", ""),
                    "fix": lesson.get("fix", ""),
                })

    return matches


def generate_report(
    info: dict[str, Any],
    backtrace_decoded: list[dict[str, Any]],
    classification: dict[str, Any],
    known_lessons: list[dict[str, Any]],
    raw_text: str = "",
) -> str:
    """Generate YAML report string."""
    report: dict[str, Any] = {
        "crash": {
            "type": info.get("type", "unknown"),
            "excvaddr": f"0x{info['excvaddr']:08x}" if info.get("excvaddr") is not None else None,
            "excause": info.get("excause"),
            "excause_description": info.get("excause_description"),
            "wdt_reset": info.get("wdt_reset", False),
            "stack_overflow_task": info.get("stack_overflow_task"),
            "pc": f"0x{info['pc']:08x}" if info.get("pc") else None,
            "a2": f"0x{info['a2']:08x}" if info.get("a2") is not None else None,
            "registers": {k: f"0x{v:08x}" for k, v in info.get("registers", {}).items()},
        },
        "backtrace_decoded": backtrace_decoded,
        "classification": classification,
        "known_lessons": known_lessons if known_lessons else None,
    }

    # Add stack watermarks if available (new format)
    if info.get("stack_watermarks"):
        report["stack_watermarks"] = info["stack_watermarks"]

    # Add BB events summary (last 5)
    if info.get("bb_events"):
        report["crash"]["last_bb_events"] = info["bb_events"][:5]

    return yaml.dump(
        report, default_flow_style=False, allow_unicode=True,
        sort_keys=False,
    )


def parse_bb_events(text: str) -> list[str]:
    """Extract black box events from the crash dump text."""
    events: list[str] = []
    parts = RE_BB_HEADER.split(text)
    if len(parts) < 2:
        return events
    bb_text = parts[1]
    for line in bb_text.splitlines():
        line = line.strip()
        if not line or line.startswith("==="):
            break  # Stop at next === section header
        if "FfiEnter" in line or "FfiExit" in line or "HeapSnapshot" in line or "StackLow" in line:
            events.append(line)
    return events


def extract_crash_section_from_log(text: str) -> str | None:
    """Extract the crash dump section from a serial log.
    Returns the crash section text (from === CRASH === / Guru Meditation to
    !!! EXCEPTION END !!! / Reboot) or None."""
    start = text.find("=== CRASH ===")
    if start < 0:
        start = text.find("Guru Meditation Error:")
        if start < 0:
            return None
    # Find end: prefer !!! EXCEPTION END !!! then Rebooting... then EOF
    end = text.find("!!! EXCEPTION END !!!", start)
    if end >= 0:
        return text[start:end + len("!!! EXCEPTION END !!!")]
    end = text.find("Rebooting...", start)
    if end >= 0:
        return text[start:end + len("Rebooting...")]
    return text[start:]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="ESP32 Guru Meditation Crash Analyzer",
    )
    parser.add_argument(
        "--dump", "-d", type=str, default=None,
        help="Path to crash dump file (reads stdin if omitted)",
    )
    parser.add_argument(
        "--elf", "-e", type=str, default=None,
        help="Path to ELF binary (auto-detects build/ecotiter.elf)",
    )
    parser.add_argument(
        "--no-decode", action="store_true",
        help="Skip addr2line backtrace decoding",
    )
    parser.add_argument(
        "--log", "-l", type=str, default=None,
        help="Serial log file path (extracts crash section automatically)",
    )
    args = parser.parse_args()

    # Read crash dump
    if args.log:
        with open(args.log) as f:
            full_text = f.read()
        text = extract_crash_section_from_log(full_text)
        if text is None:
            print("ERROR: No crash section found in log file", file=sys.stderr)
            return 1
    elif args.dump:
        with open(args.dump) as f:
            text = f.read()
    elif not sys.stdin.isatty():
        text = sys.stdin.read()
    else:
        print("ERROR: provide --dump file or pipe crash dump to stdin", file=sys.stderr)
        parser.print_help()
        return 1

    # Find ELF
    elf_path = args.elf
    if not elf_path:
        candidates = sorted(Path("build").rglob("ecotiter.elf"))
        if candidates:
            elf_path = str(candidates[0])
    if not elf_path:
        elf_path = "build/ecotiter.elf"

    # Parse
    info = parse_crash_dump(text)

    # Decode backtrace
    if args.no_decode:
        backtrace_decoded = [
            {"address": bt["pc"], "function": "??", "file": "??", "line": 0}
            for bt in info.get("backtrace_raw", [])
        ]
    else:
        backtrace_decoded = decode_backtrace(info, elf_path)

    # Classify
    classification = classify_crash(info, backtrace_decoded)

    # Check lessons
    known_lessons = check_lessons_learned(info, classification, raw_text=text)

    # Generate output
    report = generate_report(info, backtrace_decoded, classification, known_lessons, raw_text=text)
    print(report, end="")

    return 0


if __name__ == "__main__":
    sys.exit(main())
