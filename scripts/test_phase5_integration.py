#!/usr/bin/env python3
"""
Phase 5 Integration Test — USB-Serial Command/Response & Broadcast Validation.

Connects to ESP32 over USB-Serial, waits for boot, sends 3 commands
(serial.ping, system.getStatus, valve.getState), validates JSON responses,
and collects broadcast messages for format validation.

Usage:
    python3 scripts/test_phase5_integration.py                     # auto-detect port
    python3 scripts/test_phase5_integration.py /dev/ttyUSB0        # specify port

Returns exit code 0 on PASS, 1 on FAIL.
"""

import json
import re
import sys
import threading
import time
from collections import deque
from pathlib import Path

import serial

sys.path.insert(0, str(Path(__file__).parent))
from find_port import find_esp32_port

BAUDRATE = 115200
BOOT_TIMEOUT_S = 15       # max wait for boot message
CMD_TIMEOUT_S = 5         # max wait for response to a command
BROADCAST_WAIT_S = 8      # time to collect broadcast messages
BOOT_MARKER = "EcoTiter firmware"
HOMING_MARKER = "Homing complete"
LINES_BEFORE_BOOT_MAX = 200

PASS = 0
FAIL = 0


def log(msg: str) -> None:
    print(f"[TEST] {msg}", flush=True)


def pass_msg(msg: str) -> None:
    global PASS
    PASS += 1
    print(f"  ✅ PASS: {msg}", flush=True)


def fail_msg(msg: str) -> None:
    global FAIL
    FAIL += 1
    print(f"  ❌ FAIL: {msg}", flush=True)


def reader_thread(ser, buf: deque, stop_event: threading.Event) -> None:
    """Background thread: read all serial bytes into a thread-safe deque."""
    partial = b""
    while not stop_event.is_set():
        try:
            if ser.in_waiting:
                data = ser.read(ser.in_waiting)
                partial += data
                # Split on newlines, keep partial line
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
    """Wait for boot marker in serial output. Returns True on success."""
    log("Waiting for ESP32 boot...")
    deadline = time.time() + BOOT_TIMEOUT_S
    lines_seen = 0
    while time.time() < deadline:
        while buf:
            line = buf.popleft()
            lines_seen += 1
            if BOOT_MARKER in line:
                log(f"Boot detected after {lines_seen} lines: {line.strip()}")
                # Wait a bit more for homing to complete
                homing_deadline = time.time() + 15
                while time.time() < homing_deadline:
                    while buf:
                        hl = buf.popleft()
                        if HOMING_MARKER in hl:
                            log(f"Homing complete: {hl.strip()}")
                            return True
                        if "EcoTiter" in hl or "Heap:" in hl or "ADC" in hl or "Temperature" in hl:
                            pass  # regular boot log
                    time.sleep(0.1)
                # If we didn't see homing marker but boot message was seen, return True anyway
                # (homing may complete silently or the marker format differs)
                log("Boot detected (homing marker not seen — proceeding anyway)")
                return True
        time.sleep(0.1)

    fail_msg(f"Boot marker '{BOOT_MARKER}' not seen within {BOOT_TIMEOUT_S}s")
    return False


def drain_buf(buf: deque) -> None:
    """Drain and discard all current buffer contents."""
    while buf:
        buf.popleft()


def send_and_expect(
    ser, buf: deque, cmd: str, cmd_id: int,
    expect_keys: list | None = None,
    expect_error: bool = False,
    timeout_s: float = CMD_TIMEOUT_S,
) -> dict | None:
    """
    Send a JSON command over serial, wait for the response JSON.

    Returns the parsed response dict, or None on failure.
    """
    payload = json.dumps({"id": cmd_id, "cmd": cmd}) + "\n"
    log(f"Send: id={cmd_id}, cmd={cmd}")
    ser.write(payload.encode())

    deadline = time.time() + timeout_s
    while time.time() < deadline:
        while buf:
            line = buf.popleft()
            # Try to parse as JSON
            if line.startswith("{"):
                try:
                    resp = json.loads(line)
                except json.JSONDecodeError:
                    continue

                # Check if this JSON has our id
                if resp.get("id") == cmd_id:
                    status = resp.get("status", "")
                    if expect_error:
                        if status == "error":
                            pass_msg(f"Command {cmd}: got expected error")
                            return resp
                        else:
                            fail_msg(f"Command {cmd}: expected error, got status='{status}'")
                            return None
                    elif status == "ok":
                        pass_msg(f"Command {cmd}: status=ok")
                        if expect_keys:
                            data = resp.get("data", {})
                            missing = [k for k in expect_keys if k not in data]
                            if missing:
                                fail_msg(f"Command {cmd}: missing keys {missing} in data")
                                return None
                            pass_msg(f"Command {cmd}: contains keys {expect_keys}")
                        return resp
                    else:
                        fail_msg(f"Command {cmd}: unexpected status='{status}'")
                        return None

        time.sleep(0.01)

    fail_msg(f"Command {cmd}: no response within {timeout_s}s (id={cmd_id})")
    return None


def collect_broadcasts(buf: deque, duration_s: float) -> list[dict]:
    """Collect JSON broadcast messages for the given duration."""
    BROADCAST_KEYS = {"ts", "temp", "mv", "vlv", "brt"}
    broadcasts = []
    deadline = time.time() + duration_s
    while time.time() < deadline:
        while buf:
            line = buf.popleft()
            if line.startswith("{"):
                try:
                    obj = json.loads(line)
                except json.JSONDecodeError:
                    continue
                # Broadcasts have NO "id" field
                if "id" not in obj:
                    broadcasts.append(obj)
        time.sleep(0.01)

    log(f"Collected {len(broadcasts)} broadcast messages in {duration_s}s")
    return broadcasts


def validate_broadcasts(broadcasts: list[dict]) -> None:
    """Validate that broadcast messages match the expected format."""
    if not broadcasts:
        fail_msg("No broadcast messages received")
        return

    BROADCAST_KEYS = {"ts", "temp", "mv", "vlv", "brt"}
    BRT_KEYS = {"sts", "vl", "spd"}

    # Check each broadcast
    valid_count = 0
    for b in broadcasts:
        # Must have all top-level keys
        missing_top = BROADCAST_KEYS - set(b.keys())
        if missing_top:
            fail_msg(f"Broadcast missing top-level keys: {missing_top}")
            continue

        # ts must be non-negative integer
        if not isinstance(b["ts"], int) or b["ts"] < 0:
            fail_msg(f"Broadcast ts invalid: {b['ts']}")
            continue

        # temp can be null or float
        if b["temp"] is not None and not isinstance(b["temp"], (int, float)):
            fail_msg(f"Broadcast temp invalid type: {type(b['temp']).__name__}")
            continue

        # mv must be int
        if not isinstance(b["mv"], (int, float)):
            fail_msg(f"Broadcast mv invalid type: {type(b['mv']).__name__}")
            continue

        # vlv must be "in", "out", or "unk"
        if b["vlv"] not in ("in", "out", "unk"):
            fail_msg(f"Broadcast vlv invalid: {b['vlv']}")
            continue

        # brt must be object with sts, vl, spd
        brt = b["brt"]
        if not isinstance(brt, dict):
            fail_msg("Broadcast brt is not an object")
            continue
        missing_brt = BRT_KEYS - set(brt.keys())
        if missing_brt:
            fail_msg(f"Broadcast brt missing keys: {missing_brt}")
            continue
        if brt["sts"] not in ("idle", "working", "error"):
            fail_msg(f"Broadcast brt.sts invalid: {brt['sts']}")
            continue
        if not isinstance(brt["vl"], (int, float)):
            fail_msg(f"Broadcast brt.vl invalid type: {type(brt['vl']).__name__}")
            continue
        if not isinstance(brt["spd"], (int, float)):
            fail_msg(f"Broadcast brt.spd invalid type: {type(brt['spd']).__name__}")
            continue

        valid_count += 1

    if valid_count == len(broadcasts):
        pass_msg(f"All {len(broadcasts)} broadcasts have valid format")
    else:
        fail_msg(f"{valid_count}/{len(broadcasts)} broadcasts have valid format")


def main():
    global PASS, FAIL

    port = sys.argv[1] if len(sys.argv) > 1 else find_esp32_port()
    if not port:
        print("ERROR: ESP32 not found. Specify port: python3 test_phase5_integration.py /dev/ttyUSB0",
              flush=True)
        sys.exit(1)

    log(f"Connecting to {port} @ {BAUDRATE} baud")
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
        print(f"ERROR: Cannot open {port}: {e}", flush=True)
        sys.exit(1)

    # DTR reset to trigger reboot
    log("Resetting ESP32 (DTR pulse)")
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

    # Shared buffer between reader thread and main
    buf: deque = deque()
    stop_event = threading.Event()

    reader = threading.Thread(target=reader_thread, args=(ser, buf, stop_event), daemon=True)
    reader.start()

    # ── Test sequence ──────────────────────────────────────────────

    # Step 1: Wait for boot
    if not wait_for_boot(ser, buf):
        stop_event.set()
        ser.close()
        sys.exit(1)

    time.sleep(1)  # let boot settle
    drain_buf(buf)

    # Step 2: serial.ping (single-phase, no id needed)
    send_and_expect(ser, buf, "serial.ping", cmd_id=1)

    # Small delay between commands
    time.sleep(0.5)
    drain_buf(buf)

    # Step 3: system.getStatus (single-phase)
    resp = send_and_expect(
        ser, buf, "system.getStatus", cmd_id=2,
        expect_keys=["brt", "vlv"],
    )

    # Step 4: valve.getState (single-phase)
    time.sleep(0.5)
    drain_buf(buf)
    send_and_expect(
        ser, buf, "valve.getState", cmd_id=3,
        expect_keys=["position"],
    )

    # Step 5: Collect and validate broadcast messages
    time.sleep(1)
    drain_buf(buf)
    broadcasts = collect_broadcasts(buf, BROADCAST_WAIT_S)
    validate_broadcasts(broadcasts)

    # ── Summary ────────────────────────────────────────────────────
    stop_event.set()
    ser.close()

    total = PASS + FAIL
    log(f"\n{'='*50}")
    log(f"RESULTS: {PASS}/{total} passed, {FAIL}/{total} failed")

    if FAIL == 0:
        log("ALL CHECKS PASSED ✅")
        sys.exit(0)
    else:
        log("SOME CHECKS FAILED ❌")
        sys.exit(1)


if __name__ == "__main__":
    main()
