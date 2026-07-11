#!/usr/bin/env python3
"""
Unit tests for SerialClassifier — pure logic, no I/O, zero external deps.
Run:  python3 -m unittest scripts/utils/test_monitor.py -v
"""

import unittest
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from monitor_classifier import SerialClassifier, ResultCode, DedupTracker


class TestSerialClassifierBase(unittest.TestCase):
    """Helpers and shared assertions."""

    def _classify(self, lines):
        c = SerialClassifier()
        for l in lines:
            c.add_line(l)
        return c

    def assertResult(self, lines, expected, msg=None):
        c = self._classify(lines)
        self.assertEqual(c.result(), expected, msg or f"Expected {expected} for lines={lines!r}")


class TestResultPriority(TestSerialClassifierBase):
    """Highest-priority result is returned regardless of input order."""

    def test_crash_over_boot(self):
        self.assertResult([
            'BOOT_OK_MARKER',
            '{"ts":100,"temp":23.4}',
            '=== CRASH ===',
            'exccause=0 name=IllegalInstruction',
        ], ResultCode.CRASH)

    def test_boot_over_hung(self):
        self.assertResult([
            'I (30) boot: ESP-IDF v5.x',
            'DBG: step 3 - stack_monitor',
            '{"ts":100,"temp":23.4}',
        ], ResultCode.BOOT_OK)

    def test_crash_over_hung(self):
        self.assertResult([
            'I (30) boot: init done',
            '=== CRASH ===',
        ], ResultCode.CRASH)

    def test_boot_flag_set_even_if_crash_follows(self):
        c = SerialClassifier()
        c.add_line('BOOT_OK_MARKER')
        c.add_line('I (50) main: running')
        c.add_line('{"ts":100}')
        c.add_line('=== CRASH ===')
        self.assertEqual(c.result(), ResultCode.CRASH)
        self.assertTrue(c.found_boot)


class TestNoOutput(TestSerialClassifierBase):
    def test_no_lines(self):
        self.assertResult([], ResultCode.NO_OUTPUT)

    def test_only_empty_lines(self):
        c = SerialClassifier()
        for _ in range(10):
            c.add_line("")
        self.assertEqual(c.result(), ResultCode.NO_OUTPUT)


class TestBootOkMarkers(TestSerialClassifierBase):
    def test_boot_ok_marker_exact(self):
        self.assertResult(['BOOT_OK_MARKER'], ResultCode.BOOT_OK)

    def test_boot_ok_marker_in_line(self):
        self.assertResult(['prefix BOOT_OK_MARKER suffix'], ResultCode.BOOT_OK)

    def test_boot_ok_before_logs(self):
        self.assertResult([
            'BOOT_OK_MARKER',
            'DBG: step 1 - nvs_flash_init',
            'I (30) main: Build: 2026-07-11',
            '{"ts":500,"temp":23.4}',
        ], ResultCode.BOOT_OK)


class TestJsonBoot(TestSerialClassifierBase):
    """Any line starting with { means main loop is alive."""

    def test_json_ts_key(self):
        self.assertResult(['{"ts":3000,"temp":null}'], ResultCode.BOOT_OK)

    def test_json_t_key(self):
        self.assertResult(['{"t":3000,"temp":null}'], ResultCode.BOOT_OK)

    def test_json_tmstp_key(self):
        self.assertResult(['{"tmstp":3000}'], ResultCode.BOOT_OK)

    def test_json_tsp_key(self):
        self.assertResult(['{"tsp":3000}'], ResultCode.BOOT_OK)

    def test_json_full_status(self):
        line = '{"ts":3000,"temp":null,"mv":89,"electrode_mv":89,"vlv":"in"}'
        self.assertResult([line], ResultCode.BOOT_OK)

    def test_json_nested(self):
        self.assertResult(['{"brt":{"sts":"working"}}'], ResultCode.BOOT_OK)

    def test_multiple_json_lines(self):
        self.assertResult(
            [f'{{"ts":{t}}}' for t in [100, 200, 300, 400, 500]],
            ResultCode.BOOT_OK,
        )


class TestAppOutputDetection(TestSerialClassifierBase):
    """ESP-IDF log prefix, DBG:, entry, BOOT_ — app code reached."""

    def test_esp_idf_info(self):
        self.assertResult(['I (30) boot: ESP-IDF v5.x'], ResultCode.HUNG)

    def test_esp_idf_warn(self):
        self.assertResult(['W (123) stack_monitor: LOW STACK'], ResultCode.HUNG)

    def test_esp_idf_error(self):
        self.assertResult(['E (456) onewire: timeout'], ResultCode.HUNG)

    def test_esp_idf_debug(self):
        self.assertResult(['D (789) temp: read=234'], ResultCode.HUNG)

    def test_esp_idf_varied_numbers(self):
        for n in [0, 1, 99999, 123456789]:
            with self.subTest(n=n):
                self.assertResult([f'I ({n}) main: test'], ResultCode.HUNG)

    def test_dbg_init_step(self):
        self.assertResult(['DBG: step 1 - nvs_flash_init'], ResultCode.HUNG)

    def test_dbg_with_detail(self):
        self.assertResult(['DBG: step 3 - stack_monitor'], ResultCode.HUNG)

    def test_entry_line(self):
        """ROM bootloader jump to app entry point."""
        self.assertResult(['entry 0x403ce000'], ResultCode.HUNG)

    def test_boot_log_line(self):
        """Line containing BOOT_ (e.g. bootloader output)."""
        self.assertResult(['BOOT: app started'], ResultCode.HUNG)

    def test_rom_output(self):
        """ROM bootloader banner without any app output."""
        self.assertResult(['ESP-ROM:esp32s3-20210327'], ResultCode.HUNG)


class TestCrashDetection(TestSerialClassifierBase):
    def test_crash_header_only(self):
        self.assertResult(['=== CRASH ==='], ResultCode.CRASH)

    def test_full_crash_dump(self):
        lines = [
            '=== CRASH ===',
            'exccause=0 name=IllegalInstruction pc=0x40091100',
            '=== REGISTERS ===',
            'a0=0x800910c8',
            '=== BACKTRACE ===',
            '0x400910fd:0x3fffcee0',
            '=== BLACK BOX (64 events, newest first) ===',
            '[822us] t4 FfiExit { boundary: 20, result: 0 }',
            '=== STACK ===',
            't0 main watermark=0  t1 motor watermark=0',
            '!!! EXCEPTION END !!!',
        ]
        self.assertResult(lines, ResultCode.CRASH)

    def test_crash_with_reboot(self):
        self.assertResult([
            '=== CRASH ===',
            'exccause=0',
            'Rebooting...',
        ], ResultCode.CRASH)

    def test_crash_collects_complete_buffer(self):
        c = SerialClassifier()
        c.add_line('=== CRASH ===')
        c.add_line('line 1')
        c.add_line('line 2')
        c.add_line('Rebooting...')
        self.assertEqual(c.crash_buffer, [
            '=== CRASH ===',
            'line 1',
            'line 2',
            'Rebooting...',
        ])

    def test_crash_state_returns_to_idle(self):
        c = SerialClassifier()
        c.add_line('=== CRASH ===')
        c.add_line('info')
        c.add_line('!!! EXCEPTION END !!!')
        c.add_line('I (999) main: post-crash line')
        # After EXCEPTION END, back to normal — next line sets app_output
        self.assertEqual(c.result(), ResultCode.CRASH)
        self.assertTrue(c.found_crash)
        self.assertTrue(c.found_app_output)


class TestLastLinesTracking(TestSerialClassifierBase):
    def test_keeps_default_count(self):
        c = SerialClassifier()
        for i in range(10):
            c.add_line(f"line {i}")
        self.assertEqual(len(c.last_lines), 5)
        self.assertEqual(c.last_lines[-1], "line 9")

    def test_custom_max_lines(self):
        c = SerialClassifier(max_last_lines=3)
        for i in range(5):
            c.add_line(f"line {i}")
        self.assertEqual(c.last_lines, ["line 2", "line 3", "line 4"])

    def test_single_line(self):
        c = SerialClassifier()
        c.add_line("only line")
        self.assertEqual(c.last_lines, ["only line"])


class TestResultMessages(TestSerialClassifierBase):
    def test_message_crash(self):
        c = self._classify(['=== CRASH ==='])
        self.assertIn("CRASH DETECTED", c.result_message())
        self.assertEqual(c.result(), ResultCode.CRASH)

    def test_message_boot_ok(self):
        c = self._classify(['BOOT_OK_MARKER'])
        self.assertIn("BOOT OK", c.result_message())
        self.assertEqual(c.result(), ResultCode.BOOT_OK)

    def test_message_hung_app(self):
        c = self._classify(['I (30) main: running'])
        msg = c.result_message()
        self.assertIn("OUTPUT SEEN", msg)
        self.assertIn("app code reached", msg)
        self.assertEqual(c.result(), ResultCode.HUNG)

    def test_message_hung_rom(self):
        c = self._classify(['ESP-ROM:esp32s3'])
        msg = c.result_message()
        self.assertIn("OUTPUT SEEN", msg)
        self.assertIn("ROM output", msg)
        self.assertEqual(c.result(), ResultCode.HUNG)

    def test_message_hung_unrecognised(self):
        c = self._classify(["Some gibberish here and there"])
        msg = c.result_message()
        self.assertIn("OUTPUT SEEN", msg)
        self.assertIn("unrecognised output", msg)
        self.assertEqual(c.result(), ResultCode.HUNG)

    def test_message_no_output(self):
        c = self._classify([])
        msg = c.result_message()
        self.assertIn("NO SERIAL OUTPUT AT ALL", msg)
        self.assertEqual(c.result(), ResultCode.NO_OUTPUT)


class TestEdgeCases(TestSerialClassifierBase):
    def test_empty_string_added(self):
        c = SerialClassifier()
        c.add_line("")
        self.assertEqual(c.result(), ResultCode.NO_OUTPUT)
        self.assertFalse(c.found_any_output)

    def test_whitespace_only(self):
        c = SerialClassifier()
        c.add_line("   ")
        self.assertEqual(c.result(), ResultCode.NO_OUTPUT)

    def test_very_long_line(self):
        c = SerialClassifier()
        c.add_line("A" * 10000)
        self.assertEqual(c.result(), ResultCode.HUNG)
        self.assertTrue(c.found_any_output)

    def test_case_sensitivity_matters(self):
        """Patterns are case-sensitive."""
        self.assertResult(['esp-rom:esp32s3'], ResultCode.HUNG)
        self.assertResult(['boot_ok_marker'], ResultCode.HUNG)
        c = self._classify(['i (30) main: test'])
        self.assertEqual(c.result(), ResultCode.HUNG)
        self.assertFalse(c.found_app_output)

    def test_brace_in_mid_line_does_not_set_boot(self):
        self.assertResult(['not json { but brace mid'], ResultCode.HUNG)

    def test_idempotent_result(self):
        c = self._classify(['BOOT_OK_MARKER'])
        for _ in range(5):
            self.assertEqual(c.result(), ResultCode.BOOT_OK)
            self.assertEqual(c.result_message(), "RESULT: BOOT OK")


class TestIntegrationRealWorld(TestSerialClassifierBase):
    """Realistic sequences from actual firmware sessions."""

    def test_clean_boot_sequence(self):
        lines = [
            'ESP-ROM:esp32s3-20210327',
            'Build Apr 27 2021',
            'entry 0x403ce000',
            'BOOT_OK_MARKER',
            'DBG: step 1 - nvs_flash_init',
            'DBG: step 2 - blackbox',
            'DBG: step 3 - stack_monitor',
            'I (30) main: Build: 2026-07-11 (git: abc1234)',
            'DBG: step 4 - serial',
            'DBG: step 5 - RWDT init',
            'I (45) main: Creating temp_task',
            'DBG: step 9 - RUNNING',
            '{"ts":100,"temp":23.4}',
        ]
        self.assertResult(lines, ResultCode.BOOT_OK)

    def test_hang_before_app_main(self):
        """ROM output but app never starts."""
        lines = [
            'ESP-ROM:esp32s3-20210327',
            'Build Apr 27 2021',
        ]
        c = self._classify(lines)
        self.assertEqual(c.result(), ResultCode.HUNG)
        self.assertIn("ROM output", c.result_message())

    def test_hang_after_entry_no_boot_marker(self):
        """App entry reached but BOOT_OK_MARKER never printed."""
        lines = [
            'ESP-ROM:esp32s3-20210327',
            'entry 0x403ce000',
        ]
        c = self._classify(lines)
        self.assertEqual(c.result(), ResultCode.HUNG)
        self.assertIn("app code reached", c.result_message())

    def test_the_actual_bug_20260711(self):
        """Exact lines from the user's bug report."""
        lines = [
            'I (30144) stack_monitor: Thread ble_notify: cfg=8192B wmark=20976 used=2096995%',
            'W (30155) stack_monitor: Thread ble_notify: LOW STACK! 2096995% used',
            'W (31095) onewire: DS18B20 not detected (no presence pulse)',
            'W (31096) temp_thread: Temperature read failed',
            'I (31118) stack_monitor: Thread main: cfg=32768B wmark=104304 used=524069%',
            'I (31132) stack_monitor: Thread motor: cfg=16384B wmark=7040 used=57%',
            '{"ts":3000,"temp":null,"mv":89,"electrode_mv":89,"vlv":"in"}',
        ]
        c = self._classify(lines)
        self.assertEqual(c.result(), ResultCode.BOOT_OK)
        self.assertNotIn("NO SERIAL OUTPUT AT ALL", c.result_message())
        self.assertIn("BOOT OK", c.result_message())

    def test_no_serial_output_msg_only_when_truly_empty(self):
        """NO OUTPUT only when classifier truly saw nothing."""
        c = SerialClassifier()
        self.assertEqual(c.result_message(),
                         "RESULT: NO SERIAL OUTPUT AT ALL — possible causes: "
                         "wrong port, ESP32 not powered, serial adapter disconnected, "
                         "or severe early boot crash before ROM output")


class TestRegression(TestSerialClassifierBase):
    """Past failures that MUST NOT regress."""

    def test_regression_json_ts_not_t(self):
        """2026-07-11: Boot not detected due to '{"t":' vs '{"ts":'."""
        self.assertResult([
            'I (30144) stack_monitor: Thread cfg=8192B wmark=20976',
            '{"ts":3000,"temp":null,"mv":89}',
        ], ResultCode.BOOT_OK)

    def test_regression_not_no_output(self):
        """2026-07-11: Falsely reported NO SERIAL OUTPUT AT ALL."""
        lines = [
            'I (30144) stack_monitor: Thread ble_notify',
            'W (31095) onewire: DS18B20 not detected',
            '{"ts":3000,"temp":null}',
        ]
        c = self._classify(lines)
        self.assertNotIn("NO SERIAL OUTPUT AT ALL", c.result_message())

    def test_regression_hung_reports_truthfully(self):
        """Even unrecognized output should not lie."""
        c = self._classify(["unexpected but valid line with text"])
        self.assertNotIn("NO SERIAL OUTPUT AT ALL", c.result_message())
        self.assertIn("OUTPUT SEEN", c.result_message())

    def test_regression_crash_during_json_emission(self):
        """Crash after JSON output — CRASH takes priority."""
        lines = [
            'BOOT_OK_MARKER',
            '{"ts":500,"temp":23.4}',
            '=== CRASH ===',
        ]
        self.assertResult(lines, ResultCode.CRASH)


class TestDedupTracker(unittest.TestCase):
    def test_unique_lines(self):
        t = DedupTracker()
        self.assertEqual(t.add("line A", "t1"), [])     # first, queued
        self.assertEqual(t.add("line B", "t2"), ["[t1] line A"])  # previous emitted
        self.assertEqual(t.add("line C", "t3"), ["[t2] line B"])
        self.assertEqual(t.flush(), ["[t3] line C"])

    def test_dedup_two(self):
        t = DedupTracker()
        self.assertEqual(t.add("hello", "t1"), [])     # first
        self.assertEqual(t.add("hello", "t2"), [])     # deduped
        self.assertEqual(t.flush(), ["[t1] hello  x2"])

    def test_dedup_many(self):
        t = DedupTracker()
        t.add("beacon", "t1")
        for _ in range(106):
            t.add("beacon", "tX")
        out = t.flush()
        self.assertEqual(len(out), 1)
        self.assertEqual(out[0], "[t1] beacon  x107")

    def test_dedup_then_new_line(self):
        t = DedupTracker()
        t.add("same", "t1")
        t.add("same", "t2")
        out = t.add("new", "t3")
        self.assertEqual(out, ["[t1] same  x2"])       # previous series flushed
        self.assertEqual(t.flush(), ["[t3] new"])

    def test_single_line_flush(self):
        t = DedupTracker()
        t.add("only", "t1")
        self.assertEqual(t.flush(), ["[t1] only"])

    def test_empty_flush(self):
        t = DedupTracker()
        self.assertEqual(t.flush(), [])

    def test_dedup_then_flush_then_new(self):
        t = DedupTracker()
        t.add("a", "t1")
        t.add("a", "t2")
        self.assertEqual(t.flush(), ["[t1] a  x2"])
        self.assertEqual(t.add("b", "t3"), [])         # first after flush
        self.assertEqual(t.flush(), ["[t3] b"])

    def test_timestamp_preserves_first(self):
        """The timestamp in xN output is always the FIRST occurrence."""
        t = DedupTracker()
        t.add("msg", "08:00:00.000")
        t.add("msg", "08:00:01.000")
        t.add("msg", "08:00:02.000")
        self.assertEqual(t.flush(), ["[08:00:00.000] msg  x3"])

    def test_no_dedup_across_different_lines(self):
        t = DedupTracker()
        t.add("a", "t1")
        t.add("b", "t2")
        t.add("a", "t3")
        # "a" at t1 and "a" at t3 are NOT consecutive — no dedup
        self.assertEqual(t.flush(), ["[t3] a"])

    def test_interleaved_integration(self):
        """Realistic scenario: beacon line repeated 107 times between unique lines."""
        t = DedupTracker()
        self.assertEqual(t.add("ESP-ROM:esp32s3", "t1"), [])
        self.assertEqual(t.add("entry 0x403ce000", "t2"), ["[t1] ESP-ROM:esp32s3"])

        self.assertEqual(t.add("I (1280) wifi:Init max length of beacon: 752/752", "t3"), ["[t2] entry 0x403ce000"])
        for _ in range(106):
            t.add("I (1280) wifi:Init max length of beacon: 752/752", "tX")
        out = t.add("I (1290) wifi:mode : sta + softAP", "t4")

        self.assertEqual(out, ["[t3] I (1280) wifi:Init max length of beacon: 752/752  x107"])
        self.assertEqual(t.flush(), ["[t4] I (1290) wifi:mode : sta + softAP"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
