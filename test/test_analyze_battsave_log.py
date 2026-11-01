"""Tests for analyze_battsave_log.py (A/BATTSAVE.LOG analysis)."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import analyze_battsave_log as a  # noqa: E402

H = 3_600_000


def test_parse_skips_junk():
    rows = a.parse(["0,awake,4100", "", "garbage", "1000,light,4090", "5,deep"])
    assert rows == [(0, "awake", 4100), (1000, "light", 4090)]


def test_time_and_drain_per_state():
    # 1 h awake losing 60 mV, then 2 h deep losing 20 mV (logged every 10 min)
    lines = ["0,awake,4100", f"{H},deep,4040"]
    lines += [f"{H + i * 600_000},deep,{4040 - i * 20 // 12}" for i in range(1, 13)]
    lines += [f"{3 * H},awake,4020"]
    r = a.analyze(a.parse(lines))
    assert abs(r["states"]["awake"]["hours"] - 1.0) < 1e-9
    assert abs(r["states"]["awake"]["mv_per_h"] - 60) < 1e-9
    assert abs(r["states"]["deep"]["hours"] - 2.0) < 1e-9
    assert abs(r["states"]["deep"]["mv_per_h"] - 10) < 1e-9
    assert abs(r["runtime_h"] - 3.0) < 1e-9
    assert r["sessions"] == 1


def test_reboot_starts_new_session():
    # tick counter restarts at power on: never treat that gap as a segment
    lines = ["0,awake,4100", f"{H},deep,4050", f"{2 * H},power_off,4000",
             "500,awake,4010", f"{500 + H},light,3990"]
    r = a.analyze(a.parse(lines))
    assert r["sessions"] == 2
    assert abs(r["runtime_h"] - 3.0) < 1e-9
    assert r["states"].get("light", {"hours": 0})["hours"] == 0
    assert abs(r["states"]["awake"]["hours"] - 2.0) < 1e-9
    assert r["last"] == "light"


def test_report_mentions_power_off_reason():
    lines = ["0,awake,4100", f"{H},deep,4050", f"{2 * H},power_off,3290"]
    text = a.report(a.analyze(a.parse(lines)), low_mv=3300)
    assert "deep" in text and "power_off" in text
    assert "low battery" in text


def test_report_power_off_by_timer():
    lines = ["0,awake,4100", f"{H},deep,4050", f"{2 * H},power_off,4000"]
    text = a.report(a.analyze(a.parse(lines)), low_mv=3300)
    assert "low battery" not in text
    assert "timer" in text
