"""Tests for analyze_probe.py (battery probe CSV analysis)."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import analyze_probe  # noqa: E402

MODES = ["rec_on", "rec_off", "play_off"]
BLOCK_MS = 20 * 60 * 1000
SAMPLE_MS = 30 * 1000


def synth(rates, cycles=6, spike_mv=0):
    """Interleaved blocks; each mode drains at rates[mode] mV/hour.

    Voltage is continuous across blocks, as on a real battery. An optional
    spike is added during each block's first 60 s (mode-switch settling).
    """
    rows = []
    v = 4100.0
    t = 0
    block = 0
    for _ in range(cycles):
        for mode in MODES:
            start = t
            while t < start + BLOCK_MS:
                in_settle = (t - start) < 60_000
                mv = v + (spike_mv if in_settle else 0)
                rows.append({"tick_ms": t, "mode": mode, "block": block, "vbatt_mv": round(mv)})
                t += SAMPLE_MS
                v -= rates[mode] * SAMPLE_MS / 3_600_000
            block += 1
    return rows


def test_slope_per_mode():
    rates = {"rec_on": 60, "rec_off": 40, "play_off": 20}
    got = analyze_probe.analyze(synth(rates))
    for mode, want in rates.items():
        assert abs(got[mode] - want) <= 1, (mode, got[mode])


def test_verdict_go():
    assert analyze_probe.verdict({"rec_off": 40, "play_off": 20}).startswith("GO")


def test_verdict_nogo():
    assert analyze_probe.verdict({"rec_off": 40, "play_off": 35}).startswith("NO-GO")


def test_skips_first_minute_of_block():
    rates = {"rec_on": 60, "rec_off": 40, "play_off": 20}
    clean = analyze_probe.analyze(synth(rates))
    spiked = analyze_probe.analyze(synth(rates, spike_mv=30))
    for mode in rates:
        assert abs(clean[mode] - spiked[mode]) <= 1, (mode, clean[mode], spiked[mode])
