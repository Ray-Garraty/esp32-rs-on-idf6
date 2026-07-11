"""
Pure-logic serial output classifier for ESP32-S3 firmware.
No I/O, no dependencies beyond stdlib. Reuse and test freely.

Feed filtered (non-garbage) lines one by one via add_line(),
then call result() / result_message().
"""

import re
from enum import IntEnum


class ResultCode(IntEnum):
    BOOT_OK = 0
    CRASH = 2
    HUNG = 3
    NO_OUTPUT = 4


_ESP_LOG_RE = re.compile(r'^[IWED] \(\d+\)')


class SerialClassifier:
    _CRASH_IDLE = 0
    _CRASH_COLLECTING = 1

    def __init__(self, max_last_lines: int = 5):
        self.found_rom_output = False
        self.found_app_output = False
        self.found_boot = False
        self.found_crash = False
        self.found_any_output = False

        self._crash_state = self._CRASH_IDLE
        self.crash_buffer: list[str] = []
        self.last_lines: list[str] = []
        self._max_last_lines = max_last_lines

    def add_line(self, line: str) -> None:
        if not line or not line.strip():
            return

        if len(self.last_lines) >= self._max_last_lines:
            self.last_lines.pop(0)
        self.last_lines.append(line)

        self.found_any_output = True

        if "ESP-ROM" in line:
            self.found_rom_output = True

        if (_ESP_LOG_RE.match(line)
                or line.startswith("DBG:")
                or "entry" in line
                or "BOOT_" in line):
            self.found_app_output = True

        if "=== CRASH ===" in line:
            self._crash_state = self._CRASH_COLLECTING
            self.crash_buffer = [line]
            self.found_crash = True
            return

        if self._crash_state == self._CRASH_COLLECTING:
            self.crash_buffer.append(line)
            if "Rebooting..." in line or "!!! EXCEPTION END !!!" in line:
                self._crash_state = self._CRASH_IDLE
            return

        if "BOOT_OK_MARKER" in line or line.startswith("{"):
            self.found_boot = True

    def result(self) -> ResultCode:
        if self.found_crash:
            return ResultCode.CRASH
        if self.found_boot:
            return ResultCode.BOOT_OK
        if self.found_any_output:
            return ResultCode.HUNG
        return ResultCode.NO_OUTPUT

    def result_message(self) -> str:
        code = self.result()
        if code == ResultCode.CRASH:
            return "RESULT: CRASH DETECTED"
        if code == ResultCode.BOOT_OK:
            return "RESULT: BOOT OK"
        if code == ResultCode.HUNG:
            msg = "RESULT: OUTPUT SEEN BUT NO BOOT MARKER"
            if self.found_app_output:
                msg += " (app code reached)"
            elif self.found_rom_output:
                msg += " (ROM output, hang before app_main)"
            else:
                msg += " (unrecognised output)"
            return msg
        return (
            "RESULT: NO SERIAL OUTPUT AT ALL — possible causes: "
            "wrong port, ESP32 not powered, serial adapter disconnected, "
            "or severe early boot crash before ROM output"
        )


class DedupTracker:
    """Coalesces consecutive duplicate lines for compact serial output.

    Usage:
        tracker = DedupTracker()
        out = tracker.add("line1", "ts1")  # → "line1"
        out = tracker.add("line1", "ts2")  # → None  (deduped)
        out = tracker.add("line2", "ts3")  # → "line1  x2"  +  "line2"
        out = tracker.flush()              # → "line2"       (if count==1, no xN)
    """

    def __init__(self):
        self._line: str | None = None
        self._ts: str = ""
        self._count: int = 0

    def add(self, line: str, ts: str) -> list[str]:
        """Feed a line. Returns list of lines to emit (0-2 entries)."""
        result: list[str] = []
        if line == self._line:
            self._count += 1
            return result
        if self._line is not None:
            result.append(self._format())
        self._line = line
        self._ts = ts
        self._count = 1
        return result

    def flush(self) -> list[str]:
        """Force-emit the last series. Returns [] if nothing pending."""
        if self._line is None:
            return []
        result = [self._format()]
        self._line = None
        self._count = 0
        return result

    def _format(self) -> str:
        text = f"[{self._ts}] {self._line}"
        if self._count > 1:
            text += f"  x{self._count}"
        return text
