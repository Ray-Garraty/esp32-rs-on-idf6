#!/usr/bin/env python3
"""
Unit tests for _clean() from scripts/monitor.py.
Run:  python3 -m unittest scripts/utils/test_monitor_clean.py -v
"""

import unittest
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from monitor import _clean


class TestClean(unittest.TestCase):
    def test_keeps_printable_ascii(self):
        self.assertEqual(_clean("hello world"), "hello world")

    def test_keeps_printable_unicode(self):
        self.assertEqual(_clean("тест привет"), "тест привет")

    def test_keeps_newline_tab_cr(self):
        self.assertEqual(_clean("a\nb\tc\rd"), "a\nb\tc\rd")

    def test_removes_null_bytes(self):
        self.assertEqual(_clean("BOOT\x00 OK:"), "BOOT OK:")

    def test_removes_bell(self):
        self.assertEqual(_clean("a\x07b"), "ab")

    def test_removes_escape(self):
        self.assertEqual(_clean("a\x1bb"), "ab")

    def test_removes_various_control_chars(self):
        raw = "a\x00\x01\x02\x03b"
        self.assertEqual(_clean(raw), "ab")

    def test_empty_string(self):
        self.assertEqual(_clean(""), "")

    def test_only_control_chars(self):
        self.assertEqual(_clean("\x00\x01\x02"), "")

    def test_realistic_rom_output(self):
        raw = "ESP-ROM\x00:esp32s3\x00-20210327\x00"
        expected = "ESP-ROM:esp32s3-20210327"
        self.assertEqual(_clean(raw), expected)

    def test_realistic_log_line(self):
        raw = "I (476) cpu_start\x00: Single core mode\n"
        expected = "I (476) cpu_start: Single core mode\n"
        self.assertEqual(_clean(raw), expected)


if __name__ == "__main__":
    unittest.main(verbosity=2)
