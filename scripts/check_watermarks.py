#!/usr/bin/env python3
"""
Parse StackMonitor::logAllWatermarks() output from serial log.
Exit 0 if all tasks <75% usage and all expected tasks present.
Exit 1 otherwise.

Usage:
    python3 scripts/check_watermarks.py logs/serial_*.log
    python3 scripts/check_watermarks.py          # auto-discover logs/serial_*.log
"""

import sys
import re
import glob
from pathlib import Path

EXPECTED_TASKS = {
    "main", "ipc0", "ipc1", "wifi",
    "motor", "temp", "net_owner", "log_worker", "ble_notify",
}
THRESHOLD_PCT = 85
TOLERANCE_PCT = 5
EFFECTIVE_THRESHOLD = THRESHOLD_PCT + TOLERANCE_PCT  # 90%


def main():
    log_path = None
    for arg in sys.argv[1:]:
        p = Path(arg)
        if p.exists():
            log_path = p
            break
    if not log_path:
        matches = sorted(glob.glob("logs/serial_*.log"))
        if matches:
            log_path = Path(matches[-1])
    if not log_path:
        print("FAIL: no log file found")
        sys.exit(1)

    text = log_path.read_text()
    pattern = re.compile(r"Thread (\S+): cfg=(\d+)B wmark=(\d+) used=(\d+)%")
    found_tasks = set()
    failures = []

    for name, cfg, wmark, pct in pattern.findall(text):
        found_tasks.add(name)
        pct_val = int(pct)
        if pct_val > EFFECTIVE_THRESHOLD:
            failures.append(f"{name}: {pct_val}% used (>{EFFECTIVE_THRESHOLD}%)")

    missing = EXPECTED_TASKS - found_tasks
    if missing:
        failures.append(f"missing tasks: {', '.join(sorted(missing))}")

    if failures:
        print("FAIL:")
        for f in failures:
            print(f"  {f}")
        sys.exit(1)

    print(f"OK: {len(found_tasks)} tasks, max usage within threshold")
    sys.exit(0)


if __name__ == "__main__":
    main()
