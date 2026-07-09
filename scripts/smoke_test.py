#!/usr/bin/env python3
"""Build -> flash (auto port) -> 30s monitor with log.

Linux-only. Uses scripts/build.sh for IDF operations.

Usage:
    python scripts/smoke_test.py
"""

import subprocess
import sys
import time
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent
SCRIPTS_DIR = PROJECT_DIR / "scripts"

sys.path.insert(0, str(SCRIPTS_DIR))
from monitor import monitor_port
from find_port import find_esp32_port


def build():
    return subprocess.run(
        [str(SCRIPTS_DIR / "build.sh"), "build"],
        cwd=str(PROJECT_DIR),
    ).returncode


def flash(port: str):
    return subprocess.run(
        [str(SCRIPTS_DIR / "build.sh"), "flash", port],
        cwd=str(PROJECT_DIR),
    ).returncode


def log(msg):
    print(f"  {msg}", flush=True)


def main():
    log("Build...")
    if build():
        sys.exit(1)

    port = find_esp32_port()
    if not port:
        log("No ESP32 port found")
        sys.exit(1)
    log(f"Port: {port}")

    log("Flash...")
    if flash(port):
        sys.exit(1)

    time.sleep(0.3)
    sys.exit(monitor_port(port, timeout=30, log_dir=PROJECT_DIR / "logs", no_reset=True))


if __name__ == "__main__":
    main()
