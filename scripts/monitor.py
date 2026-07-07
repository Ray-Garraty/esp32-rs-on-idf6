#!/usr/bin/env python3
"""
Serial monitor for ESP32-S3 firmware. Auto-detects port, resets chip via DTR,
prints serial output with timestamps, saves to log file.

Usage:
    python scripts/monitor.py                  # auto-detect port, 30s timeout
    python scripts/monitor.py COM5             # specify port manually
    python scripts/monitor.py --timeout 60     # longer
    python scripts/monitor.py --no-reset       # skip DTR reset
    python scripts/monitor.py --no-log         # terminal only, no file
    python scripts/monitor.py --log-dir /tmp/logs
"""

import serial
import sys
import os
import time
import argparse
import threading
from pathlib import Path
from datetime import datetime

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))
from find_port import find_esp32_port

BAUDRATE = 115200
PROJECT_DIR = SCRIPT_DIR.parent
DEFAULT_LOG_DIR = str(PROJECT_DIR / "logs")


def timestamp():
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


def make_log_filename(log_dir: str) -> Path:
    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    return Path(log_dir) / f"serial_{ts}.log"


def monitor_port(port, timeout=30, log_dir=DEFAULT_LOG_DIR, no_reset=False, no_log=False, log_path=None):
    CRASH_STATE_IDLE = 0
    CRASH_STATE_COLLECTING = 1
    crash_state = CRASH_STATE_IDLE
    crash_buffer: list[str] = []
    log_path = None
    if not no_log:
        log_dir = Path(log_dir)
        log_dir.mkdir(parents=True, exist_ok=True)
        log_file = make_log_filename(str(log_dir))
        counter = 1
        while log_file.exists():
            stem = f"serial_{datetime.now().strftime('%Y-%m-%d_%H-%M-%S')}_{counter}"
            log_file = log_dir / f"{stem}.log"
            counter += 1
        log_path = log_file

    try:
        ser = serial.Serial(
            port=port,
            baudrate=BAUDRATE,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=1,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
        )
        ser.rts = True
        ser.dtr = True

        def writeline(line: str, end: str = "\n"):
            try:
                print(line, end=end, flush=True)
            except UnicodeEncodeError:
                safe = line.encode("utf-8", errors="replace").decode("cp1252", errors="replace")
                print(safe, end=end, flush=True)
            if log_file is not None:
                try:
                    with open(log_file, "a", encoding="utf-8") as f:
                        f.write(line + "\n")
                except OSError:
                    pass

        writeline(f"=== Connected to ESP32-S3 on {port} @ {BAUDRATE} baud ===")

        if not no_reset:
            writeline("=== Resetting ESP32-S3 (DTR pulse) ===")
            ser.dtr = False; ser.rts = False; time.sleep(0.1)
            ser.dtr = True;  ser.rts = True;  time.sleep(1.2)

        if log_file:
            writeline(f"=== Logging to {log_file} ===")

        found_crash = False
        found_boot = False
        deadline = time.time() + timeout
        buf = ""

        while time.time() < deadline:
            try:
                if ser.in_waiting:
                    data = ser.read(ser.in_waiting).decode("utf-8", errors="replace")
                    buf += data
                    while "\n" in buf:
                        line, buf = buf.split("\n", 1)
                        line = line.strip("\r")
                        if line:
                            ts = timestamp()

                            if "=== CRASH ===" in line:
                                crash_state = CRASH_STATE_COLLECTING
                                crash_buffer = [line]
                                found_crash = True
                                writeline(f"[{ts}] {line}")
                            elif crash_state == CRASH_STATE_COLLECTING:
                                crash_buffer.append(line)
                                writeline(f"[{ts}] {line}")
                                if "Rebooting..." in line or "!!! EXCEPTION END !!!" in line:
                                    crash_state = CRASH_STATE_IDLE
                            else:
                                if "BOOT_OK_MARKER" in line:
                                    found_boot = True
                                writeline(f"[{ts}] {line}")
                else:
                    time.sleep(0.01)
            except serial.SerialException:
                writeline("=== Connection lost ===")
                break
            except KeyboardInterrupt:
                writeline("\n=== Exiting ===")
                break

        ser.close()
        writeline("=== Port closed ===")

        if log_file and log_file.exists() and log_file.stat().st_size == 0:
            log_file.unlink()

    except serial.SerialException as e:
        msg = f"[{timestamp()}] Error: Cannot open {port}: {e}"
        safe = msg.encode("utf-8", errors="replace").decode("cp1252", errors="ignore")
        sys.stdout.buffer.write((safe + "\n").encode("cp1252", errors="replace"))
        sys.stdout.buffer.flush()
        return 1

    if log_path and log_path.exists() and log_path.stat().st_size > 0:
        print(f"Log: {log_path}", flush=True)
    if found_crash:
        print("RESULT: CRASH DETECTED", flush=True)
        return 2
    elif found_boot:
        print("RESULT: BOOT OK", flush=True)
        return 0
    else:
        print("RESULT: No boot marker (possibly OK)", flush=True)
        return 0


def main():
    parser = argparse.ArgumentParser(description="ESP32-S3 serial monitor")
    parser.add_argument("port", nargs="?", default=None, help="COM port (auto-detect if omitted)")
    parser.add_argument("--timeout", type=int, default=30, help="Monitor duration in seconds")
    parser.add_argument("--no-reset", action="store_true", help="Skip DTR reset on connect")
    parser.add_argument("--log-dir", default=DEFAULT_LOG_DIR, help="Directory for log files (default: project_root/logs/)")
    parser.add_argument("--no-log", action="store_true", help="Disable log file saving")
    args = parser.parse_args()

    port = args.port or find_esp32_port()
    if not port:
        print("ERROR: ESP32-S3 not found. Specify port: python scripts/monitor.py COM5", flush=True)
        return 1

    return monitor_port(port=port, timeout=args.timeout, log_dir=args.log_dir,
                        no_reset=args.no_reset, no_log=args.no_log)


if __name__ == "__main__":
    sys.exit(main())
