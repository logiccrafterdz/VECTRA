#!/usr/bin/env python3
"""Tests for the pre-registered New York session blackout used by convert_dukascopy.py.

1. Fixed boundary cases (same instants as tests/test_session.cpp), so the
   Python converter and the C++ engine are pinned to the same calendar.
2. When the IANA tz database is available (zoneinfo + tzdata), the explicit
   US DST rule is checked against America/New_York for every minute of 2023.
"""

import datetime
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import convert_dukascopy as conv  # noqa: E402

UTC = datetime.timezone.utc


def utc(s: str) -> datetime.datetime:
    return datetime.datetime.fromisoformat(s).replace(tzinfo=UTC)


BOUNDARY_CASES = [
    # (UTC instant, expected excluded)
    ("2023-01-10 21:44:59", False),  # Tue 16:44:59 EST
    ("2023-01-10 21:45:00", True),   # Tue 16:45 EST: rollover blackout starts
    ("2023-01-10 23:29:59", True),
    ("2023-01-10 23:30:00", False),  # 18:30 EST: blackout ends
    ("2023-03-14 20:44:59", False),  # Tue 16:44:59 EDT (after the 2023-03-12 switch)
    ("2023-03-14 20:45:00", True),
    ("2023-03-14 22:29:59", True),
    ("2023-03-14 22:30:00", False),
    ("2023-01-13 21:44:59", False),  # Fri before the close
    ("2023-01-13 21:45:00", True),   # Fri 16:45 EST: weekend starts
    ("2023-01-14 12:00:00", True),   # Saturday
    ("2023-01-15 23:29:59", True),   # Sun 18:29:59 EST
    ("2023-01-15 23:30:00", False),  # Sun 18:30 EST: trading resumes
]

OFFSET_CASES = [
    ("2023-03-12 06:59:59", -5),
    ("2023-03-12 07:00:00", -4),
    ("2023-11-05 05:59:59", -4),
    ("2023-11-05 06:00:00", -5),
]


def run() -> int:
    failures = 0
    for s, expected in BOUNDARY_CASES:
        got, reason = conv.is_excluded_time(utc(s))
        if got != expected:
            print(f"FAIL blackout {s}: expected {expected}, got {got} ({reason})")
            failures += 1
    for s, expected in OFFSET_CASES:
        got = conv.new_york_offset_hours(utc(s))
        if got != expected:
            print(f"FAIL offset {s}: expected {expected}, got {got}")
            failures += 1

    try:
        from zoneinfo import ZoneInfo
        ny = ZoneInfo("America/New_York")
    except Exception:  # tz database not installed: boundary cases still ran
        print("SKIP zoneinfo cross-check (tzdata not available)")
        ny = None

    if ny is not None:
        t = utc("2023-01-01 00:00:00")
        end = utc("2024-01-01 00:00:00")
        step = datetime.timedelta(minutes=1)
        checked = 0
        while t < end:
            expected = int(t.astimezone(ny).utcoffset().total_seconds() // 3600)
            if conv.new_york_offset_hours(t) != expected:
                print(f"FAIL zoneinfo mismatch at {t.isoformat()}")
                failures += 1
                break
            t += step
            checked += 1
        print(f"zoneinfo cross-check: {checked:,} minutes of 2023 compared")

    if failures:
        print(f"{failures} session calendar test(s) failed")
        return 1
    print("All session calendar tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(run())
