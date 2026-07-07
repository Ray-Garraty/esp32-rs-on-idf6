#!/usr/bin/env python3
"""
UART Command Test for ecotiter firmware on ESP32-S3.

Sends JSON commands over UART and validates responses.

Usage:
    python scripts/uart_test.py                          # auto-detect port
    python scripts/uart_test.py -p /dev/ttyACM0           # specify port
    python scripts/uart_test.py -p /dev/ttyUSB0 --baud 115200
"""

import argparse
import datetime
import json
import sys
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent
sys.path.insert(0, str(SCRIPT_DIR))
from find_port import find_esp32_port

try:
    import serial
except ImportError:
    print("ERROR: pyserial not installed. Run: pip install pyserial")
    sys.exit(1)

BAUDRATE = 115200
TIMEOUT = 2
LINE_END = b"\n"

tests_passed = 0
tests_failed = 0
log_file = None


def log(msg: str):
    """Write to both stdout and the raw log file."""
    print(msg)
    global log_file
    if log_file:
        ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        log_file.write(f"[{ts}] {msg}\n")
        log_file.flush()


def send_and_recv(ser, payload: str) -> str | None:
    """Send a JSON string + newline and read one line response."""
    raw_send = payload.encode("utf-8") + LINE_END
    log(f"  >>> {raw_send!r}")
    ser.write(raw_send)
    buf = b""
    deadline = time.time() + TIMEOUT
    while time.time() < deadline:
        if ser.in_waiting:
            chunk = ser.read(ser.in_waiting)
            log(f"  <<< {chunk!r}")
            buf += chunk
            while LINE_END in buf:
                line, buf = buf.split(LINE_END, 1)
                line = line.strip(b"\r")
                if line:
                    dec = line.decode("utf-8", errors="replace")
                    log(f"  ... {dec}")
                    return dec
        else:
            time.sleep(0.01)
    return None


def expect_json_field(response_str: str | None, field: str, expected_val=None) -> bool:
    if response_str is None:
        print("  FAIL: no response (timeout)")
        return False
    try:
        data = json.loads(response_str)
    except json.JSONDecodeError as e:
        print(f"  FAIL: invalid JSON: {e}")
        print(f"  raw: {response_str}")
        return False
    if field not in data:
        print(f"  FAIL: missing field '{field}' in {data}")
        return False
    if expected_val is not None and data[field] != expected_val:
        print(f"  FAIL: field '{field}' = {data[field]!r}, expected {expected_val!r}")
        return False
    return True


def run_test(ser, name: str, cmd: str, expect_fn, drain_first=False):
    global tests_passed, tests_failed

    if drain_first:
        time.sleep(0.2)
        ser.reset_input_buffer()

    log(f"\n=== {name} ===")
    log(f"  SEND: {cmd}")
    resp = send_and_recv(ser, cmd)
    if resp:
        log(f"  RECV: {resp}")
    else:
        log(f"  RECV: (timeout)")

    if expect_fn(resp):
        log("  ==> PASS")
        tests_passed += 1
    else:
        log("  ==> FAIL")
        tests_failed += 1


def main():
    parser = argparse.ArgumentParser(description="UART command test for ecotiter")
    parser.add_argument("-p", "--port", default=None, help="Serial port")
    parser.add_argument("--baud", type=int, default=BAUDRATE, help="Baud rate")
    args = parser.parse_args()

    port = args.port or find_esp32_port()
    if not port:
        print("ERROR: ESP32-S3 not found. Specify port with -p /dev/ttyACM0")
        sys.exit(1)

    print(f"Opening {port} @ {args.baud} baud...")
    ser = serial.Serial(
        port=port,
        baudrate=args.baud,
        timeout=1,
    )
    time.sleep(0.5)
    ser.reset_input_buffer()

    global tests_passed, tests_failed, log_file
    log_dir = PROJECT_DIR / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    ts = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    log_path = log_dir / f"uart_test_{ts}.log"
    log_file = open(log_path, "w", encoding="utf-8")
    log_file.write(f"UART test log — {ts}\n")
    log_file.write(f"Port: {port} @ {args.baud} baud\n")
    log_file.write("=" * 60 + "\n")
    log_file.flush()
    log(f"Log: {log_path}")

    # Test 1: serial.ping
    run_test(
        ser,
        "serial.ping",
        '{"cmd":"serial.ping"}',
        lambda r: expect_json_field(r, "cmd", "serial.ping")
        and expect_json_field(r, "result", "pong"),
        drain_first=True,
    )

    # Test 2: system.firmwareVersion
    run_test(
        ser,
        "system.firmwareVersion",
        '{"cmd":"system.firmwareVersion"}',
        lambda r: expect_json_field(r, "cmd", "system.firmwareVersion")
        and expect_json_field(r, "version"),
    )

    # Test 3: getStatus
    run_test(
        ser,
        "getStatus",
        '{"cmd":"getStatus"}',
        lambda r: expect_json_field(r, "state"),
    )

    # Test 4: invalid JSON
    run_test(
        ser,
        "invalid JSON",
        "not-json",
        lambda r: expect_json_field(r, "error"),
    )

    # Test 5: unknown command
    run_test(
        ser,
        "unknown command",
        '{"cmd":"nonexistent"}',
        lambda r: expect_json_field(r, "error"),
    )

    ser.close()
    log(f"\nResults: {tests_passed} passed, {tests_failed} failed")
    if log_file:
        log_file.write("=" * 60 + "\n")
        log_file.write(f"Results: {tests_passed} passed, {tests_failed} failed\n")
        log_file.close()
    return 0 if tests_failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
