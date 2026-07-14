"""
Broadcast message format validation and interval diagnostics.
Shared between serial and BLE integration tests.

Compact broadcast format spec:
  {"ts": uint32, "temp": float|null, "mv": number, "vlv": "in"|"out"|"unk",
   "brt": {"sts": "idle"|"working"|"error", "vl": float|null, "spd": number}}
"""

import math

SPEC_TOP_KEYS = {"ts", "temp", "mv", "vlv", "brt"}
BRT_SUB_KEYS = {"sts", "vl", "spd"}
VLV_VALID = {"in", "out", "unk"}
BRT_STS_VALID = {"idle", "working", "error"}

SPEC_INTERVAL_MS = 300
SPEC_INTERVAL_TOLERANCE_MS = 10
OUTLIER_THRESHOLD_MS = 400


def validate_broadcast_format(broadcasts: list[tuple[dict, float]], log_fn=print) -> tuple[int, int]:
    """Validate all broadcasts against spec. Returns (pass_count, total_count)."""
    if not broadcasts:
        return (0, 0)
    spec_ok = 0
    for obj, _arrival in broadcasts:
        issues = _check_spec(obj)
        extra_keys = set(obj.keys()) - SPEC_TOP_KEYS
        if extra_keys:
            log_fn(f"  broadcast extra keys: {sorted(extra_keys)}")
        if not issues:
            spec_ok += 1
        else:
            for e in issues:
                log_fn(f"  broadcast spec issue: {e}")
    return (spec_ok, len(broadcasts))


def _check_spec(obj: dict) -> list[str]:
    """Return list of spec violations for a single broadcast object."""
    issues: list[str] = []
    missing_top = SPEC_TOP_KEYS - set(obj.keys())
    if missing_top:
        issues.append(f"missing spec top-level keys: {missing_top}")
        return issues
    if not isinstance(obj["ts"], int) or obj["ts"] < 0:
        issues.append(f"ts: expected uint32, got {obj['ts']!r}")
    if obj["temp"] is not None and not isinstance(obj["temp"], (int, float)):
        issues.append(f"temp: expected float|null, got {type(obj['temp']).__name__}")
    if not isinstance(obj["mv"], (int, float)):
        issues.append(f"mv: expected number, got {type(obj['mv']).__name__}")
    if obj["vlv"] not in VLV_VALID:
        issues.append(f"vlv: expected in/out/unk, got {obj['vlv']!r}")
    brt = obj["brt"]
    if not isinstance(brt, dict):
        issues.append("brt: expected object")
        return issues
    missing_brt = BRT_SUB_KEYS - set(brt.keys())
    if missing_brt:
        issues.append(f"brt missing keys: {missing_brt}")
    else:
        if brt["sts"] not in BRT_STS_VALID:
            issues.append(f"brt.sts: expected idle/working/error, got {brt['sts']!r}")
        if brt["vl"] is not None and not isinstance(brt["vl"], (int, float)):
            issues.append(f"brt.vl: expected float|null, got {type(brt['vl']).__name__}")
        if not isinstance(brt["spd"], (int, float)):
            issues.append(f"brt.spd: expected number, got {type(brt['spd']).__name__}")
    return issues


def diagnose_broadcast_intervals(broadcasts: list[tuple[dict, float]], log_fn=print) -> None:
    """Analyze broadcast timing using ts ticks and arrival timestamps."""
    if len(broadcasts) < 2:
        log_fn("  Broadcast interval: too few messages (<2) for analysis")
        return
    ts_deltas_ms: list[float] = []
    arrival_deltas_ms: list[float] = []
    for i in range(1, len(broadcasts)):
        ts_prev = broadcasts[i - 1][0]["ts"]
        ts_curr = broadcasts[i][0]["ts"]
        # gTick increments every ~10 ms (see scheduler.hpp BROADCAST_INTERVAL
        # comment). The *10 factor converts ts ticks to milliseconds.
        delta_ts = (ts_curr - ts_prev) * 10
        if delta_ts > 0:
            ts_deltas_ms.append(delta_ts)
        arrival_prev = broadcasts[i - 1][1]
        arrival_curr = broadcasts[i][1]
        delta_arrival = (arrival_curr - arrival_prev) * 1000
        if delta_arrival > 0:
            arrival_deltas_ms.append(delta_arrival)
    log_fn(f"\n=== Broadcast interval diagnostics ({len(broadcasts)} frames) ===")
    _report_deltas("Method A (ts * 10ms)", ts_deltas_ms, "device-side blocking", log_fn)
    _report_deltas("Method B (arrival)", arrival_deltas_ms, "connection/read issue", log_fn)


def _report_deltas(label: str, deltas: list[float], issue_hint: str, log_fn) -> None:
    """Report statistics for a single delta series."""
    if not deltas:
        log_fn(f"  {label}: no valid deltas")
        return
    mean = sum(deltas) / len(deltas)
    variance = sum((d - mean) ** 2 for d in deltas) / len(deltas)
    stddev = math.sqrt(variance)
    max_d = max(deltas)
    min_d = min(deltas)
    lower = SPEC_INTERVAL_MS - SPEC_INTERVAL_TOLERANCE_MS
    upper = SPEC_INTERVAL_MS + SPEC_INTERVAL_TOLERANCE_MS
    n_out = sum(1 for d in deltas if d < lower or d > upper)
    log_fn(f"  {label}:")
    log_fn(f"    Target: {SPEC_INTERVAL_MS} ±{SPEC_INTERVAL_TOLERANCE_MS} ms  "
           f"Actual: min={min_d:.0f}ms  max={max_d:.0f}ms  "
           f"mean={mean:.0f}ms  stddev={stddev:.0f}ms")
    log_fn(f"    Out of tolerance (±{SPEC_INTERVAL_TOLERANCE_MS}ms): {n_out}/{len(deltas)}")
    if n_out > 0:
        log_fn(f"    -> WARN: {n_out} intervals exceed tolerance — possible {issue_hint}")
    else:
        log_fn(f"    -> ALL intervals within tolerance")
