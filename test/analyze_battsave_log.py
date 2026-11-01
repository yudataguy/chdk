#!/usr/bin/env python3
"""Summarize A/BATTSAVE.LOG written by CHDK Battery Saver ("Log to card").

Each line is `tick_ms,state,vbatt_mv`, written on every state change and every
10 minutes while asleep. Prints time spent and battery drain (mV per hour) in
each state, and how the last session ended.

Usage: python3 test/analyze_battsave_log.py BATTSAVE.LOG [--low-mv 3300]
"""
import argparse
import sys


def parse(lines):
    """Return [(tick_ms, state, mv)], skipping lines that do not parse."""
    rows = []
    for line in lines:
        parts = line.strip().split(",")
        if len(parts) != 3:
            continue
        try:
            rows.append((int(parts[0]), parts[1], int(parts[2])))
        except ValueError:
            continue
    return rows


def analyze(rows):
    """Attribute each gap between two lines to the state of the earlier line.

    The tick counter restarts at power on, so a tick that goes backwards (or a
    line after power_off) starts a new session and the gap is not counted.
    """
    states = {}
    runtime_ms = 0
    sessions = 1 if rows else 0
    for (t0, s0, v0), (t1, _s1, v1) in zip(rows, rows[1:]):
        if t1 < t0 or s0 == "power_off":
            sessions += 1
            continue
        st = states.setdefault(s0, {"ms": 0, "mv": 0})
        st["ms"] += t1 - t0
        st["mv"] += v0 - v1
        runtime_ms += t1 - t0
    out = {}
    for name, st in states.items():
        hours = st["ms"] / 3_600_000
        out[name] = {"hours": hours, "mv_per_h": st["mv"] / hours if hours else 0.0}
    return {
        "states": out,
        "runtime_h": runtime_ms / 3_600_000,
        "sessions": sessions,
        "last": rows[-1][1] if rows else None,
        "last_mv": rows[-1][2] if rows else None,
    }


def report(result, low_mv=None):
    lines = [f"sessions: {result['sessions']}, logged runtime: {result['runtime_h']:.2f} h"]
    for name in ("awake", "light", "deep", "waking", "wake_failed"):
        st = result["states"].get(name)
        if st and st["hours"] > 0:
            lines.append(f"  {name:12s} {st['hours']:7.2f} h  {st['mv_per_h']:7.1f} mV/h")
    if result["last"] == "power_off":
        why = "low battery" if low_mv and result["last_mv"] < low_mv else "timer (Power off after)"
        lines.append(f"ended: power_off at {result['last_mv']} mV ({why})")
    elif result["last"]:
        lines.append(f"ended: last line was '{result['last']}' (battery ran out, or camera was switched off by hand)")
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("log")
    ap.add_argument("--low-mv", type=int, default=None,
                    help="CHDK battery 'min voltage' setting, to tell a low-battery power off from the timer")
    args = ap.parse_args(argv)
    with open(args.log, encoding="ascii", errors="replace") as f:
        rows = parse(f)
    if not rows:
        print("no log lines found", file=sys.stderr)
        return 1
    print(report(analyze(rows), args.low_mv))
    return 0


if __name__ == "__main__":
    sys.exit(main())
