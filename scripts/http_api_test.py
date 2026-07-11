#!/usr/bin/env python3
"""
HTTP API Test for ecotiter firmware on ESP32-S3.

Tests all HTTP API endpoints via curl against the device IP.
Supports auto-detection of device IP from serial monitor or manual specification.

Usage:
    python scripts/http_api_test.py                          # auto-detect IP from serial
    python scripts/http_api_test.py --ip 192.168.1.103       # specify IP
    python scripts/http_api_test.py --ip 192.168.4.1         # AP mode
    python scripts/http_api_test.py --wait 15                 # wait N seconds for boot
"""

import argparse
import datetime
import json
import re
import subprocess
import sys
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent
sys.path.insert(0, str(SCRIPT_DIR))
from find_port import find_esp32_port

TIMEOUT = 5
tests_passed = 0
tests_failed = 0
log_file = None
BASE_URL = ""


def log(msg: str):
    print(msg)
    global log_file
    if log_file:
        ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        log_file.write(f"[{ts}] {msg}\n")
        log_file.flush()


def curl_get(path: str, timeout: int = TIMEOUT) -> tuple[int, str]:
    url = f"http://{BASE_URL}{path}"
    try:
        r = subprocess.run(
            ["curl", "-s", "-o", "-", "-w", "\n%{http_code}", url],
            capture_output=True, text=True, timeout=timeout
        )
        parts = r.stdout.strip().rsplit("\n", 1)
        if len(parts) == 2:
            return int(parts[1]), parts[0].strip()
        return 0, r.stdout.strip()
    except subprocess.TimeoutExpired:
        return 0, "TIMEOUT"
    except Exception as e:
        return 0, f"ERROR: {e}"


def curl_post(path: str, data: dict, timeout: int = TIMEOUT) -> tuple[int, str]:
    url = f"http://{BASE_URL}{path}"
    payload = json.dumps(data)
    try:
        r = subprocess.run(
            ["curl", "-s", "-o", "-", "-w", "\n%{http_code}", "-X", "POST",
             url, "-H", "Content-Type: application/json", "-d", payload],
            capture_output=True, text=True, timeout=timeout
        )
        parts = r.stdout.strip().rsplit("\n", 1)
        if len(parts) == 2:
            return int(parts[1]), parts[0].strip()
        return 0, r.stdout.strip()
    except subprocess.TimeoutExpired:
        return 0, "TIMEOUT"
    except Exception as e:
        return 0, f"ERROR: {e}"


def expect_json(response_str: str) -> dict | None:
    try:
        return json.loads(response_str)
    except (json.JSONDecodeError, TypeError):
        return None


def expect_json_field(response_str: str | None, field: str, expected_val=None) -> bool:
    data = expect_json(response_str)
    if data is None:
        print(f"    FAIL: no JSON response")
        return False
    if field not in data:
        print(f"    FAIL: missing field '{field}' in {data}")
        return False
    if expected_val is not None and data[field] != expected_val:
        print(f"    FAIL: field '{field}' = {data[field]!r}, expected {expected_val!r}")
        return False
    return True


def run_test(name: str, method: str, path: str, body: dict | None = None,
             expect_code: int = 200, expect_fn=None) -> bool:
    global tests_passed, tests_failed
    log(f"\n=== {name} ===")
    log(f"  {method} {path}")

    if method == "GET":
        code, resp = curl_get(path)
    else:
        code, resp = curl_post(path, body or {})

    log(f"  HTTP {code}")
    if resp and len(resp) > 200:
        log(f"  Body: {resp[:200]}...")
    elif resp:
        log(f"  Body: {resp}")

    if code != expect_code:
        log(f"  FAIL: expected HTTP {expect_code}, got {code}")
        tests_failed += 1
        return False

    if expect_fn and not expect_fn(resp):
        tests_failed += 1
        return False

    log("  ==> PASS")
    tests_passed += 1
    return True


def find_ip_from_serial(port: str | None = None) -> str | None:
    """Scan serial output for 'sta ip: <IP>' pattern."""
    if not port:
        port = find_esp32_port()
    if not port:
        return None
    try:
        import serial as pyserial
    except ImportError:
        return None

    try:
        ser = pyserial.Serial(port=port, baudrate=115200, timeout=1)
    except Exception:
        return None

    deadline = time.time() + 15
    ip_pattern = re.compile(r"sta ip:\s*(\d+\.\d+\.\d+\.\d+)")
    while time.time() < deadline:
        try:
            line = ser.readline().decode("utf-8", errors="replace")
            m = ip_pattern.search(line)
            if m:
                ser.close()
                return m.group(1)
        except Exception:
            break
    ser.close()
    return None


def main():
    parser = argparse.ArgumentParser(description="HTTP API test for ecotiter")
    parser.add_argument("--ip", default=None, help="Device IP address")
    parser.add_argument("--wait", type=int, default=0, help="Seconds to wait for boot")
    parser.add_argument("--port", default=None, help="Serial port for IP detection")
    args = parser.parse_args()

    global BASE_URL
    if args.ip:
        BASE_URL = args.ip
    else:
        ip = find_ip_from_serial(args.port)
        if not ip:
            print("ERROR: Could not determine device IP. Specify with --ip 192.168.x.x")
            sys.exit(1)
        BASE_URL = ip

    if args.wait:
        print(f"Waiting {args.wait}s for device boot...")
        time.sleep(args.wait)

    global tests_passed, tests_failed, log_file
    log_dir = PROJECT_DIR / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    ts = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    log_path = log_dir / f"http_api_test_{ts}.log"
    log_file = open(log_path, "w", encoding="utf-8")
    log_file.write(f"HTTP API test log — {ts}\n")
    log_file.write(f"Target: http://{BASE_URL}\n")
    log_file.write("=" * 60 + "\n")
    log_file.flush()
    log(f"Log: {log_path}")

    # ============================================================
    # TEST SUITE — 18 tests covering all endpoints
    # ============================================================

    # --- 1. GET /api/ping ---
    run_test("GET /api/ping", "GET", "/api/ping",
             expect_code=200,
             expect_fn=lambda r: expect_json_field(r, "status", "ok"))

    # --- 2. GET /api/status ---
    run_test("GET /api/status", "GET", "/api/status",
             expect_code=200,
             expect_fn=lambda r: (
                 expect_json_field(r, "state") and
                 expect_json_field(r, "temperature") and
                 expect_json_field(r, "valve") and
                 expect_json_field(r, "mv")
             ))

    # --- 3. GET /api/valve ---
    run_test("GET /api/valve", "GET", "/api/valve",
             expect_code=200,
             expect_fn=lambda r: expect_json_field(r, "valve"))

    # --- 4. POST /api/valve (toggle to output) ---
    run_test("POST /api/valve (output)", "POST", "/api/valve",
             expect_code=200, body={"position": "output"},
             expect_fn=lambda r: expect_json_field(r, "valve", "output"))

    # --- 5. POST /api/valve (back to input) ---
    run_test("POST /api/valve (input)", "POST", "/api/valve",
             expect_code=200, body={"position": "input"},
             expect_fn=lambda r: expect_json_field(r, "valve", "input"))

    # --- 6. GET /api/logs ---
    run_test("GET /api/logs", "GET", "/api/logs",
             expect_code=200,
             expect_fn=lambda r: expect_json_field(r, "entries"))

    # --- 7. GET /api/logs?limit=3 ---
    run_test("GET /api/logs?limit=3", "GET", "/api/logs?limit=3",
             expect_code=200,
             expect_fn=lambda r: (
                 expect_json_field(r, "entries") and
                 expect_json_field(r, "count", 3)
             ))

    # --- 8. GET /api/logs?level=WARN ---
    run_test("GET /api/logs?level=WARN", "GET", "/api/logs?level=WARN",
             expect_code=200,
             expect_fn=lambda r: expect_json_field(r, "entries"))

    # --- 9. GET /api/logs/download ---
    run_test("GET /api/logs/download", "GET", "/api/logs/download",
             expect_code=200,
             expect_fn=lambda r: r is not None and len(r) > 0)

    # --- 10. GET /api/nvs/status ---
    run_test("GET /api/nvs/status", "GET", "/api/nvs/status",
             expect_code=200,
             expect_fn=lambda r: (
                 expect_json_field(r, "burette_cal") and
                 expect_json_field(r, "adc_cal")
             ))

    # --- 11. POST /api/command (ping) ---
    run_test("POST /api/command (ping)", "POST", "/api/command",
             expect_code=200, body={"cmd": "ping"},
             expect_fn=lambda r: (
                 expect_json_field(r, "cmd", "serial.ping") and
                 expect_json_field(r, "result", "pong")
             ))

    # --- 12. POST /api/command (status) ---
    run_test("POST /api/command (status)", "POST", "/api/command",
             expect_code=200, body={"cmd": "status"},
             expect_fn=lambda r: expect_json_field(r, "state"))

    # --- 13. GET /wifi/status ---
    run_test("GET /wifi/status", "GET", "/wifi/status",
             expect_code=200,
             expect_fn=lambda r: expect_json_field(r, "ap"))

    # --- 14. GET / (dashboard) ---
    run_test("GET / (dashboard)", "GET", "/",
             expect_code=200,
             expect_fn=lambda r: r is not None and "EcoTiter" in r)

    # --- 15. GET /wifi (captive portal) ---
    run_test("GET /wifi", "GET", "/wifi",
             expect_code=200,
             expect_fn=lambda r: r is not None and "WiFi" in r)

    # --- 16. GET /js/init.js ---
    run_test("GET /js/init.js", "GET", "/js/init.js",
             expect_code=200,
             expect_fn=lambda r: r is not None and "initApp" in r)

    # --- 17. GET /js/calibration.js ---
    run_test("GET /js/calibration.js", "GET", "/js/calibration.js",
             expect_code=200,
             expect_fn=lambda r: r is not None and "loadCalibrationStatus" in r)

    # --- 18. 404 redirect ---
    run_test("GET /nonexistent (404 redirect)", "GET", "/nonexistent",
             expect_code=303)

    log(f"\nResults: {tests_passed} passed, {tests_failed} failed")
    if log_file:
        log_file.write("=" * 60 + "\n")
        log_file.write(f"Results: {tests_passed} passed, {tests_failed} failed\n")
        log_file.close()
    return 0 if tests_failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
