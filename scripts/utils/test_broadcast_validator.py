#!/usr/bin/env python3
"""Unit tests for broadcast_validator module."""

import unittest
import time
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from broadcast_validator import (
    validate_broadcast_format, _check_spec, diagnose_broadcast_intervals,
    SPEC_TOP_KEYS, BRT_SUB_KEYS, VLV_VALID, BRT_STS_VALID,
)


class TestCheckSpec(unittest.TestCase):
    def _make_broadcast(self, **overrides):
        obj = {
            "ts": 1000,
            "temp": 23.5,
            "mv": 89,
            "vlv": "in",
            "brt": {"sts": "idle", "vl": None, "spd": 0.0},
        }
        obj.update(overrides)
        return obj

    def test_valid_broadcast(self):
        issues = _check_spec(self._make_broadcast())
        self.assertEqual(issues, [])

    def test_valid_broadcast_out_valve(self):
        issues = _check_spec(self._make_broadcast(vlv="out"))
        self.assertEqual(issues, [])

    def test_valid_broadcast_unk_valve(self):
        issues = _check_spec(self._make_broadcast(vlv="unk"))
        self.assertEqual(issues, [])

    def test_valid_broadcast_working(self):
        issues = _check_spec(self._make_broadcast(brt={"sts": "working", "vl": 5.0, "spd": 10.0}))
        self.assertEqual(issues, [])

    def test_valid_broadcast_error(self):
        issues = _check_spec(self._make_broadcast(brt={"sts": "error", "vl": 5.0, "spd": 10.0}))
        self.assertEqual(issues, [])

    def test_missing_top_key(self):
        issues = _check_spec({"ts": 1000})
        self.assertTrue(any("missing spec top-level keys" in i for i in issues))

    def test_ts_not_int(self):
        issues = _check_spec(self._make_broadcast(ts="abc"))
        self.assertTrue(any("ts" in i for i in issues))

    def test_ts_negative(self):
        issues = _check_spec(self._make_broadcast(ts=-1))
        self.assertTrue(any("ts" in i for i in issues))

    def test_vlv_invalid(self):
        issues = _check_spec(self._make_broadcast(vlv="invalid"))
        self.assertTrue(any("vlv" in i for i in issues))

    def test_brt_sts_invalid(self):
        issues = _check_spec(self._make_broadcast(brt={"sts": "bogus", "vl": 0, "spd": 0}))
        self.assertTrue(any("brt.sts" in i for i in issues))

    def test_temp_null_allowed(self):
        issues = _check_spec(self._make_broadcast(temp=None))
        self.assertEqual(issues, [])

    def test_brt_vl_null_allowed(self):
        issues = _check_spec(self._make_broadcast(brt={"sts": "idle", "vl": None, "spd": 0.0}))
        self.assertEqual(issues, [])


class TestValidateBroadcastFormat(unittest.TestCase):
    def _make_broadcasts(self, count=3):
        obj = {"ts": 1000, "temp": 23.5, "mv": 89, "vlv": "in",
               "brt": {"sts": "idle", "vl": None, "spd": 0.0}}
        base_ts = 1000
        result = []
        for i in range(count):
            o = dict(obj)
            o["ts"] = base_ts + i * 200
            result.append((o, time.monotonic()))
        return result

    def test_all_valid(self):
        logged = []
        p, a = validate_broadcast_format(self._make_broadcasts(5), log_fn=lambda m: logged.append(m))
        self.assertEqual(p, 5)
        self.assertEqual(a, 5)

    def test_empty_list(self):
        p, a = validate_broadcast_format([])
        self.assertEqual(p, 0)
        self.assertEqual(a, 0)

    def test_all_invalid(self):
        broadcasts = [({"ts": "bad", "temp": None, "mv": 0, "vlv": "?", "brt": {}}, time.monotonic())]
        logged = []
        p, a = validate_broadcast_format(broadcasts, log_fn=lambda m: logged.append(m))
        self.assertEqual(p, 0)
        self.assertEqual(a, 1)
        self.assertTrue(len(logged) > 0)


class TestDiagnoseBroadcastIntervals(unittest.TestCase):
    def test_too_few_messages(self):
        logged = []
        diagnose_broadcast_intervals([({"ts": 100}, 0.0)], log_fn=lambda m: logged.append(m))
        self.assertTrue(any("too few" in m for m in logged))

    def test_normal_intervals(self):
        logged = []
        base = time.monotonic()
        broadcasts = []
        for i in range(5):
            # 30 ts ticks × 10 = 300 ms — matches SPEC_INTERVAL_MS
            broadcasts.append(({"ts": 100 + i * 30}, base + i * 0.3))
        diagnose_broadcast_intervals(broadcasts, log_fn=lambda m: logged.append(m))
        # Should not warn about anomalies
        self.assertFalse(any("WARN" in m for m in logged))

    def test_empty(self):
        diagnose_broadcast_intervals([])  # should not crash


if __name__ == "__main__":
    unittest.main(verbosity=2)
