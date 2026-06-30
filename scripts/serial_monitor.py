#!/usr/bin/env python3
"""
Serial monitor for Autosampler firmware.
Auto-detects ESP32 port, resets the chip via DTR, prints serial output with timestamps.
Usage:
    python3 scripts/serial_monitor.py           # auto-detect port
    python3 scripts/serial_monitor.py /dev/ttyUSB0  # specify port manually
    python3 scripts/serial_monitor.py /dev/ttyUSB0 --no-reset  # connect without reset
"""

import serial
import sys
import time
import argparse
from pathlib import Path
from datetime import datetime

sys.path.insert(0, str(Path(__file__).parent))
from find_port import find_esp32_port

BAUDRATE = 115200


def timestamp():
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


def main():
    parser = argparse.ArgumentParser(description="Autosampler serial monitor")
    parser.add_argument("port", nargs="?", default=None, help="COM port (auto-detect if omitted)")
    parser.add_argument("--no-reset", action="store_true", help="Skip DTR reset on connect")
    args = parser.parse_args()

    port = args.port or find_esp32_port()
    if not port:
        print("ERROR: ESP32 not found. Specify port manually: python3 serial_monitor.py /dev/ttyUSB0", flush=True)
        return 1

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

        print(f"=== Connected to {port} @ {BAUDRATE} baud ===", flush=True)

        if not args.no_reset:
            print("=== Resetting ESP32 (DTR pulse) ===", flush=True)
            ser.dtr = False
            ser.rts = False
            time.sleep(0.1)
            ser.dtr = True
            ser.rts = True
            time.sleep(0.1)
            ser.dtr = False
            ser.rts = False
            time.sleep(0.5)
            ser.reset_input_buffer()

        print("=== Monitoring (Ctrl+C to exit) ===\n", flush=True)

        buf = ""
        while True:
            try:
                if ser.in_waiting:
                    data = ser.read(ser.in_waiting).decode("utf-8", errors="replace")
                    buf += data
                    while "\n" in buf:
                        line, buf = buf.split("\n", 1)
                        line = line.strip("\r")
                        if line:
                            print(f"[{timestamp()}] {line}", flush=True)
                else:
                    time.sleep(0.01)
            except serial.SerialException:
                print("=== Connection lost ===", flush=True)
                break
            except KeyboardInterrupt:
                print("\n=== Exiting ===", flush=True)
                break

        ser.close()
        print("Port closed.", flush=True)

    except serial.SerialException as e:
        print(f"Error: {e}", flush=True)
        return 1

    return 0


if __name__ == "__main__":
    main()
