#!/usr/bin/env python3
"""
Serial monitor for ESP32-S3 firmware. Auto-detects port, resets chip via DTR,
saves serial output to log file.

Default (quiet): only status messages to stdout, serial lines to log file only.
Use --verbose to echo serial output to terminal.

Usage:
    python scripts/monitor.py                        # quiet, 30s, auto-detect
    python scripts/monitor.py --verbose              # echo serial to terminal
    python scripts/monitor.py /dev/ttyACM0            # specify port
    python scripts/monitor.py --timeout 60           # longer
    python scripts/monitor.py --no-reset             # skip DTR reset
    python scripts/monitor.py --no-log               # terminal only, no file
    python scripts/monitor.py --log-dir /tmp/logs
"""

import serial
import sys
import time
import argparse
from pathlib import Path
from datetime import datetime

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))
from find_port import find_esp32_port
from monitor_classifier import SerialClassifier, ResultCode

BAUDRATE = 115200
PROJECT_DIR = SCRIPT_DIR.parent
DEFAULT_LOG_DIR = str(PROJECT_DIR / "logs")


def timestamp():
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


def make_log_filename(log_dir: str) -> Path:
    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    return Path(log_dir) / f"serial_{ts}.log"


def monitor_port(port, timeout=30, log_dir=DEFAULT_LOG_DIR, no_reset=False,
                 no_log=False, log_path=None, verbose=False):
    classifier = SerialClassifier(max_last_lines=5)
    log_file = None
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
        ser.dtr = False
        ser.rts = False

        def writeline(line: str, always_visible: bool = False, end: str = "\n"):
            if always_visible or verbose:
                try:
                    print(line, end=end, flush=True)
                except UnicodeEncodeError:
                    safe = line.encode("utf-8", errors="replace").decode("utf-8", errors="replace")
                    print(safe, end=end, flush=True)
            if log_file is not None:
                try:
                    with open(log_file, "a", encoding="utf-8") as f:
                        f.write(line + "\n")
                except OSError:
                    pass

        writeline(f"=== Connected to ESP32-S3 on {port} @ {BAUDRATE} baud ===", always_visible=True)

        if not no_reset:
            writeline("=== Resetting ESP32-S3 (DTR pulse) ===", always_visible=True)
            ser.dtr = False
            ser.rts = False
            time.sleep(0.1)
            ser.dtr = True
            ser.rts = True
            time.sleep(0.1)
            ser.dtr = False
            ser.rts = False
            time.sleep(1.0)

        # Flush ROM bootloader binary garbage before reading.
        time.sleep(0.3)
        ser.reset_input_buffer()

        if log_file:
            writeline(f"=== Logging to {log_file} ===", always_visible=True)

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
                        if not line:
                            continue

                        # Filter out binary garbage from ROM bootloader preamble.
                        if line[0].isdigit():
                            continue
                        n_alpha = sum(c.isalpha() for c in line)
                        if n_alpha < len(line) * 0.3:
                            continue

                        classifier.add_line(line)
                        writeline(f"[{timestamp()}] {line}")
                else:
                    time.sleep(0.01)
            except serial.SerialException:
                writeline("=== Connection lost ===", always_visible=True)
                break
            except KeyboardInterrupt:
                writeline("\n=== Exiting ===", always_visible=True)
                break

        ser.close()
        writeline("=== Port closed ===", always_visible=True)

        if log_file and log_file.exists() and log_file.stat().st_size == 0:
            log_file.unlink()

    except serial.SerialException as e:
        msg = f"[{timestamp()}] Error: Cannot open {port}: {e}"
        safe = msg.encode("utf-8", errors="replace").decode("utf-8", errors="ignore")
        sys.stdout.buffer.write((safe + "\n").encode("utf-8", errors="replace"))
        sys.stdout.buffer.flush()
        return 1

    if log_path and log_path.exists() and log_path.stat().st_size > 0:
        print(f"Log: {log_path}", flush=True)

    print(classifier.result_message(), flush=True)
    return classifier.result()


def main():
    parser = argparse.ArgumentParser(description="ESP32-S3 serial monitor")
    parser.add_argument("port", nargs="?", default=None, help="Serial port (auto-detect if omitted)")
    parser.add_argument("--timeout", type=int, default=30, help="Monitor duration in seconds")
    parser.add_argument("--no-reset", action="store_true", help="Skip DTR reset on connect")
    parser.add_argument("--log-dir", default=DEFAULT_LOG_DIR, help="Directory for log files (default: project_root/logs/)")
    parser.add_argument("--no-log", action="store_true", help="Disable log file saving")
    parser.add_argument("--verbose", action="store_true", help="Echo serial output to terminal (default: quiet, log only)")
    args = parser.parse_args()

    port = args.port or find_esp32_port()
    if not port:
        print("ERROR: ESP32-S3 not found. Specify port: python scripts/monitor.py /dev/ttyACM0", flush=True)
        return 1

    return monitor_port(port=port, timeout=args.timeout, log_dir=args.log_dir,
                        no_reset=args.no_reset, no_log=args.no_log,
                        verbose=args.verbose)


if __name__ == "__main__":
    sys.exit(main())
