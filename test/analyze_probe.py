#!/usr/bin/env python3
"""Analyze A/battprobe.csv from test/battsave_probe.lua.

Reports battery drain (mV/hour) per mode and a go/no-go verdict for
the Battery Saver deep-sleep tier (playback + screen off).

Usage: python3 test/analyze_probe.py battprobe.csv
"""
import csv
import statistics
import sys
from collections import defaultdict

SETTLE_MS = 60_000      # ignore each block's first minute (mode-switch settling)
GO_RATIO = 0.75         # play_off must drain <= 75% of rec_off to be "clearly better"
MODE_ORDER = ["rec_on", "rec_off", "play_off"]


def _slope_mv_per_hour(points):
    """Least-squares slope of (hours, mV); returns drain as a positive number."""
    n = len(points)
    mx = sum(p[0] for p in points) / n
    my = sum(p[1] for p in points) / n
    sxx = sum((p[0] - mx) ** 2 for p in points)
    if sxx == 0:
        return None
    sxy = sum((p[0] - mx) * (p[1] - my) for p in points)
    return -sxy / sxx


def analyze(rows):
    """rows: dicts with tick_ms, mode, block, vbatt_mv. Returns {mode: mV/hour}."""
    blocks = defaultdict(list)
    for r in rows:
        blocks[(r["mode"], int(r["block"]))].append((int(r["tick_ms"]), int(r["vbatt_mv"])))

    per_mode = defaultdict(list)
    for (mode, _), samples in blocks.items():
        start = min(t for t, _ in samples)
        pts = [(t / 3_600_000, mv) for t, mv in samples if t - start >= SETTLE_MS]
        if len(pts) < 3:
            continue
        s = _slope_mv_per_hour(pts)
        if s is not None:
            per_mode[mode].append(s)

    return {mode: statistics.median(slopes) for mode, slopes in per_mode.items()}


def verdict(rates):
    rec_off = rates.get("rec_off")
    play_off = rates.get("play_off")
    if not rec_off or play_off is None:
        return "NO-GO: missing rec_off or play_off data"
    ratio = play_off / rec_off
    if ratio <= GO_RATIO:
        return f"GO: playback drains {ratio:.0%} of rec+screen-off"
    return f"NO-GO: playback drains {ratio:.0%} of rec+screen-off (need <= {GO_RATIO:.0%})"


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip())
        return 2
    with open(argv[1], newline="") as f:
        rows = list(csv.DictReader(f))
    rates = analyze(rows)
    print(f"{'mode':<10} {'mV/hour':>8}")
    for mode in MODE_ORDER:
        if mode in rates:
            print(f"{mode:<10} {rates[mode]:>8.1f}")
    print(verdict(rates))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
