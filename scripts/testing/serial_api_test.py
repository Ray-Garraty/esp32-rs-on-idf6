#!/usr/bin/env python3
"""
Serial API Format Validation Test.

Validates serial command/response format against SERIAL_API.md,
collects broadcast messages, and diagnoses timing issues via
two methods (timestamp delta and arrival delta).

Usage:
    python3 scripts/testing/serial_api_test.py                    # auto-detect port
    python3 scripts/testing/serial_api_test.py -p /dev/ttyUSB0   # specify port

Exit code: 0 = PASS, 1 = FAIL.
"""

import argparse
import datetime
import json
import sys
import threading
import time
from collections import deque
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent
sys.path.insert(0, str(SCRIPT_DIR))
sys.path.insert(0, str(PROJECT_DIR))
sys.path.insert(0, str(PROJECT_DIR / "utils"))

try:
    import serial
except ImportError:
    print("ERROR: pyserial not installed. Run: pip install pyserial")
    sys.exit(1)

from find_port import find_esp32_port
from utils.monitor_classifier import DedupTracker
from utils.log_utils import sanitize_line
from boot_detect import BootDetector, BOOT_OK_MARKER, wait_for_boot as shared_wait_for_boot
from broadcast_validator import validate_broadcast_format, diagnose_broadcast_intervals

BAUDRATE = 115200
BOOT_TIMEOUT_S = 15
CMD_TIMEOUT_S = 5
BROADCAST_COLLECT_S = 30
PASS = 0
FAIL = 0
log_file = None
boot_detector = None


def status(msg: str) -> None:
    global log_file
    print(msg, flush=True)
    if log_file:
        ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        log_file.write(f"[{ts}] {sanitize_line(msg)}\n")
        log_file.flush()


def log(msg: str) -> None:
    global log_file
    if log_file:
        ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        log_file.write(f"[{ts}] {sanitize_line(msg)}\n")
        log_file.flush()


def pass_msg(msg: str) -> None:
    global PASS
    PASS += 1
    status(f"  ==> PASS: {msg}")


def fail_msg(msg: str) -> None:
    global FAIL
    FAIL += 1
    status(f"  ==> FAIL: {msg}")


def reader_thread(ser, buf: deque, stop_event: threading.Event) -> None:
    partial = b""
    while not stop_event.is_set():
        try:
            if ser.in_waiting:
                data = ser.read(ser.in_waiting)
                partial += data
                while b"\n" in partial:
                    line, partial = partial.split(b"\n", 1)
                    line_str = line.decode("utf-8", errors="replace").strip("\r")
                    if line_str:
                        buf.append(line_str)
            else:
                time.sleep(0.005)
        except serial.SerialException:
            break
        except Exception:
            break


def wait_for_boot(ser, buf: deque) -> bool:
    global boot_detector
    status("Waiting for ESP32 boot...")
    deadline = time.time() + BOOT_TIMEOUT_S
    lines_seen = 0
    dedup = DedupTracker()
    ts = lambda: datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
    while time.time() < deadline:
        while buf:
            line = buf.popleft()
            lines_seen += 1
            boot_detector.add_line(line)
            for out in dedup.add(line.strip(), ts()):
                log(f"  boot line: {out}")
            if BOOT_OK_MARKER in line:
                for out in dedup.flush():
                    log(f"  boot line: {out}")
                status(f"Boot detected after {lines_seen} lines")
                return True
        time.sleep(0.1)

    for out in dedup.flush():
        log(f"  boot line: {out}")
    fail_msg(f"Boot marker not seen within {BOOT_TIMEOUT_S}s")
    return False


def drain_buf(buf: deque) -> None:
    while buf:
        buf.popleft()


def send_and_expect(
    ser, buf: deque, cmd: str, cmd_id: int,
    expect_keys: list | None = None,
    expect_error: bool = False,
    params: dict | None = None,
    timeout_s: float = CMD_TIMEOUT_S,
) -> dict | None:
    payload_dict = {"id": cmd_id, "cmd": cmd}
    if params:
        payload_dict.update(params)
    payload = json.dumps(payload_dict) + "\n"
    log(f"  >>> {payload.strip()}")
    ser.write(payload.encode())

    deadline = time.time() + timeout_s
    while time.time() < deadline:
        while buf:
            line = buf.popleft()
            boot_detector.add_line(line)
            log(f"  <<< {line}")
            if line.startswith("{"):
                try:
                    resp = json.loads(line)
                except json.JSONDecodeError:
                    continue

                # Match by id (spec format) or cmd field (current firmware)
                matched = resp.get("id") == cmd_id or resp.get("cmd") == cmd
                if not matched:
                    continue

                # Prefer spec format, fallback to current firmware format
                st = resp.get("status", "")
                if expect_error:
                    if st == "error" or "error" in resp.get("result", ""):
                        pass_msg(f"{cmd}: got expected error")
                        return resp
                    fail_msg(f"{cmd}: expected error, got status='{st}'")
                    return None
                elif st == "ok":
                    data = resp.get("data", {})
                    if expect_keys:
                        missing = [k for k in expect_keys if k not in data]
                        if missing:
                            fail_msg(f"{cmd}: missing keys {missing} in data")
                            return None
                    pass_msg(f"{cmd}: status=ok")
                    return resp
                elif resp.get("result") == "pong":
                    pass_msg(f"{cmd}: result=pong")
                    return resp
                else:
                    fail_msg(f"{cmd}: unexpected status='{st}', result='{resp.get('result','')}'")
                    return None

    fail_msg(f"{cmd}: no response within {timeout_s}s")
    return None


def collect_broadcasts(buf: deque, duration_s: float) -> list[tuple[dict, float]]:
    results: list[tuple[dict, float]] = []
    deadline = time.time() + duration_s
    while time.time() < deadline:
        while buf:
            line = buf.popleft()
            boot_detector.add_line(line)
            log(f"  broadcast raw: {line}")
            if line.startswith("{"):
                try:
                    obj = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if "id" not in obj and "cmd" not in obj:
                    results.append((obj, time.monotonic()))
        time.sleep(0.01)

    status(f"Collected {len(results)} broadcast messages in {duration_s}s")
    return results





# ── Valve helpers ─────────────────────────────────────────────────────

def read_valve_position_from_broadcast(
    buf: deque, timeout_s: float = 3
) -> str | None:
    """Read broadcast messages from buf, return last seen vlv value."""
    deadline = time.time() + timeout_s
    last_vlv: str | None = None
    while time.time() < deadline:
        while buf:
            line = buf.popleft()
            boot_detector.add_line(line)
            if not line.startswith("{"):
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue
            if "id" in obj or "cmd" in obj:
                continue
            vlv = obj.get("vlv")
            if vlv in ("in", "out"):
                last_vlv = vlv
                log(f"  broadcast vlv={vlv}")
        time.sleep(0.01)
    return last_vlv


def wait_for_valve_position(
    buf: deque, target_vlv: str, timeout_s: float = 5
) -> bool:
    """Wait for a broadcast with vlv matching target_vlv."""
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        while buf:
            line = buf.popleft()
            boot_detector.add_line(line)
            if not line.startswith("{"):
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue
            if "id" in obj or "cmd" in obj:
                continue
            vlv = obj.get("vlv")
            if vlv == target_vlv:
                return True
        time.sleep(0.01)
    return False


def validate_spec_response(
    resp: dict | None, cmd_id: int, expect_data_keys: list[str] | None = None
) -> bool:
    """Validate command response follows COMMS_PROTOCOL.md single-phase format.
    
    Single-phase spec pattern:
      {"id": <uint64>, "status": "ok", "data": {...}}
      {"id": <uint64>, "status": "error", "message": "<code>"}
    """
    if resp is None:
        return False
    if resp.get("id") != cmd_id:
        log(f"  spec validation: id mismatch — expected {cmd_id}, got {resp.get('id')}")
        return False
    st = resp.get("status")
    if st not in ("ok", "error"):
        log(f"  spec validation: status expected ok|error, got {st!r}")
        return False
    if st == "ok" and expect_data_keys:
        data = resp.get("data", {})
        missing = [k for k in expect_data_keys if k not in data]
        if missing:
            log(f"  spec validation: data missing keys {missing}")
            return False
    if st == "error" and "message" not in resp:
        log(f"  spec validation: error response missing 'message' field")
        return False
    return True


def collect_broadcast_vlv_values(buf: deque, duration_s: float) -> list[str]:
    """Collect vlv field values from broadcast messages over a period."""
    vlv_values: list[str] = []
    deadline = time.time() + duration_s
    while time.time() < deadline:
        while buf:
            line = buf.popleft()
            boot_detector.add_line(line)
            if not line.startswith("{"):
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue
            if "id" in obj or "cmd" in obj:
                continue
            vlv = obj.get("vlv")
            if vlv in ("in", "out", "unk"):
                vlv_values.append(vlv)
        time.sleep(0.01)
    return vlv_values


# ── Main ─────────────────────────────────────────────────────────────

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Serial API format validation test")
    p.add_argument("-p", "--port", default=None, help="Serial port")
    return p.parse_args()


def main() -> int:
    global PASS, FAIL, log_file, boot_detector

    boot_detector = BootDetector()
    args = parse_args()
    port = args.port or find_esp32_port()
    if not port:
        status("ERROR: ESP32 not found. Specify port with -p /dev/ttyUSB0")
        return 1

    log_dir = SCRIPT_DIR / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    ts = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    log_path = log_dir / f"serial_api_test_{ts}.log"
    log_file = open(log_path, "w", encoding="utf-8")
    log_file.write(f"Serial API test log — {ts}\n")
    log_file.write(f"Port: {port} @ {BAUDRATE}\n")
    log_file.write("=" * 60 + "\n")
    log_file.flush()
    status(f"Log: {log_path}")

    status(f"Connecting to {port} @ {BAUDRATE} baud")
    try:
        ser = serial.Serial(
            port=port,
            baudrate=BAUDRATE,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.1,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
        )
    except serial.SerialException as e:
        status(f"ERROR: Cannot open {port}: {e}")
        if log_file:
            log_file.close()
        return 1

    status("Resetting ESP32 (DTR pulse)")
    try:
        ser.dtr = False
        ser.rts = False
        time.sleep(0.1)
        ser.dtr = True
        ser.rts = True
        time.sleep(0.1)
        ser.dtr = False
        ser.rts = False
        time.sleep(0.3)
        ser.reset_input_buffer()
    except Exception:
        pass

    buf: deque = deque()
    stop_event = threading.Event()
    reader = threading.Thread(target=reader_thread, args=(ser, buf, stop_event), daemon=True)
    reader.start()

    # ── Step 1: Wait for boot ─────────────────────────────────────
    if not wait_for_boot(ser, buf):
        stop_event.set()
        ser.close()
        if log_file:
            log_file.close()
        return 1

    time.sleep(1)
    drain_buf(buf)

    # ── Step 2: serial.ping ───────────────────────────────────────
    status("\n=== serial.ping ===")
    send_and_expect(ser, buf, "serial.ping", cmd_id=1,
                    expect_keys=["status"])

    time.sleep(0.5)
    drain_buf(buf)

    # ── Step 3: burette.getStatus ─────────────────────────────────
    status("\n=== burette.getStatus ===")
    send_and_expect(ser, buf, "burette.getStatus", cmd_id=2,
                    expect_keys=["status", "volume_ml", "speed_ml_min"])

    time.sleep(0.5)
    drain_buf(buf)

    # ── Step 4: burette.cal.get ───────────────────────────────────
    status("\n=== burette.cal.get ===")
    send_and_expect(ser, buf, "burette.cal.get", cmd_id=3,
                    expect_keys=["steps_per_ml", "nominal_vol", "speed_coeff",
                                 "min_freq", "max_freq", "is_default"])

    time.sleep(0.5)
    drain_buf(buf)

    # ── Step 5: valve.getState ────────────────────────────────────
    status("\n=== valve.getState ===")
    send_and_expect(ser, buf, "valve.getState", cmd_id=4,
                    expect_keys=["position"])

    time.sleep(0.5)
    drain_buf(buf)

    # ── Step 6: nonexistent command (error case) ──────────────────
    status("\n=== nonexistent command (expect error) ===")
    send_and_expect(ser, buf, "nonexistent", cmd_id=5,
                    expect_error=True)

    time.sleep(0.5)
    drain_buf(buf)

    # ── Step 7: Valve toggle + broadcast confirmation ──────────────
    status("\n=== Valve toggle test ===")
    cur_vlv = read_valve_position_from_broadcast(buf, timeout_s=3)
    if cur_vlv is None:
        fail_msg("Could not determine current valve position from broadcasts")
    else:
        target_vlv = "out" if cur_vlv == "in" else "in"
        target_pos = "output" if target_vlv == "out" else "input"

        # ── 7a: Toggle to opposite position ──
        status(f"  (7a) Current vlv={cur_vlv}, toggling to {target_pos}")
        rsp6 = send_and_expect(ser, buf, "valve.setPosition", cmd_id=6,
                               params={"position": target_pos},
                               expect_keys=["position"])
        if rsp6 and validate_spec_response(rsp6, 6, ["position"]):
            pass_msg(f"valve.setPosition({target_pos}): response conforms to spec format")
        elif rsp6:
            fail_msg(f"valve.setPosition({target_pos}): response spec format violation")

        time.sleep(0.3)
        if wait_for_valve_position(buf, target_vlv, timeout_s=5):
            pass_msg(f"valve.setPosition({target_pos}): broadcast confirmed vlv={target_vlv}")
            # Log vlv transition sequence observed during the wait
            vlv_seq = collect_broadcast_vlv_values(buf, 0.5)
            if vlv_seq:
                log(f"  vlv values after toggle: original={cur_vlv}, transition={vlv_seq}")
        else:
            fail_msg(f"valve.setPosition({target_pos}): broadcast did not show vlv={target_vlv} in 5s")

        time.sleep(0.3)
        drain_buf(buf)

        # ── 7b: Idempotency — set to same position (should return ok) ──
        status(f"  (7b) Idempotency: setting to same position ({target_pos})")
        rsp_idem = send_and_expect(ser, buf, "valve.setPosition", cmd_id=7,
                                   params={"position": target_pos},
                                   expect_keys=["position"])
        if rsp_idem and validate_spec_response(rsp_idem, 7, ["position"]):
            pass_msg(f"valve.setPosition({target_pos}): idempotent set returns ok")
        time.sleep(0.3)
        drain_buf(buf)

        # ── 7c: Invalid parameter error ──
        status("  (7c) Error case: invalid position 'garbage'")
        rsp_err = send_and_expect(ser, buf, "valve.setPosition", cmd_id=8,
                                  params={"position": "garbage"},
                                  expect_error=True)
        if rsp_err and validate_spec_response(rsp_err, 8):
            msg = rsp_err.get("message")
            if msg == "invalid_params":
                pass_msg("valve.setPosition('garbage'): returns invalid_params per spec")
            else:
                fail_msg(f"valve.setPosition('garbage'): expected message='invalid_params', got {msg!r}")
        time.sleep(0.3)
        drain_buf(buf)

        # ── 7d: Restore original position ──
        restore_pos = "input" if cur_vlv == "in" else "output"
        status(f"  (7d) Restoring to {cur_vlv}")
        rsp_restore = send_and_expect(ser, buf, "valve.setPosition", cmd_id=9,
                                      params={"position": restore_pos},
                                      expect_keys=["position"])
        if rsp_restore and validate_spec_response(rsp_restore, 9, ["position"]):
            pass_msg(f"valve.setPosition({cur_vlv}): response conforms to spec format")
        elif rsp_restore:
            fail_msg(f"valve.setPosition({cur_vlv}): response spec format violation")

        time.sleep(0.3)
        if wait_for_valve_position(buf, cur_vlv, timeout_s=5):
            pass_msg(f"valve.setPosition({cur_vlv}): broadcast confirmed vlv={cur_vlv}")
        else:
            fail_msg(f"valve.setPosition({cur_vlv}): broadcast did not show vlv={cur_vlv} in 5s")

        time.sleep(0.3)
        drain_buf(buf)

    # ── Step 8: Collect + validate broadcast messages ─────────────
    status(f"\n=== Broadcast collection ({BROADCAST_COLLECT_S}s) ===")
    time.sleep(1)
    drain_buf(buf)
    broadcasts = collect_broadcasts(buf, BROADCAST_COLLECT_S)

    status("\n=== Broadcast format validation ===")
    passed, total_b = validate_broadcast_format(broadcasts, log_fn=log)
    if total_b == 0:
        fail_msg("No broadcast messages received for format validation")
    elif passed == total_b:
        pass_msg(f"All {total_b} broadcasts conform to spec format")
    else:
        fail_msg(f"{passed}/{total_b} broadcasts conform to spec format")

    status("\n=== Broadcast interval diagnostics ===")
    diagnose_broadcast_intervals(broadcasts, log_fn=status)

    # ── Reboot check ──────────────────────────────────────────────
    if boot_detector.reboot_detected:
        fail_msg(f"ESP32-S3 reboot detected (BOOT OK: seen {boot_detector.count} times)")

    # ── Summary ───────────────────────────────────────────────────
    stop_event.set()
    ser.close()

    total = PASS + FAIL
    status("\n" + "=" * 50)
    status(f"RESULTS: {PASS}/{total} passed, {FAIL}/{total} failed")

    if FAIL == 0:
        status("ALL CHECKS PASSED")
    else:
        status("SOME CHECKS FAILED")

    if log_file:
        log_file.write("=" * 60 + "\n")
        log_file.write(f"Results: {PASS}/{total} passed, {FAIL}/{total} failed\n")
        log_file.close()

    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
