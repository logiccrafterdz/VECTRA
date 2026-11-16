#!/usr/bin/env python3
"""Converts raw Dukascopy tick CSV files into VECTRA CSV schema with pre-registered exclusions.

Target Schema:
    symbol,bid,ask,source_ts_ns,sequence

Pre-Registered Filtering Rules (Protocol v1.7):
All session boundaries are defined in America/New_York local time, because the
FX trading day rolls over at 17:00 New York: 22:00 UTC in winter (EST) and
21:00 UTC in summer (EDT, from 2023-03-12). A fixed UTC window is wrong for part
of Q1 2023. The US DST rule is applied explicitly (no tz database needed) and
mirrors src/market/session.cpp exactly.

1. Daily rollover blackout: discard quotes from 16:45 to 18:30 New York time,
   every day. The 2022-12-15 dry run (outside the locked period) showed the
   winter rollover spread spike from about 21:45 to 23:15 UTC (EUR/USD median
   3.0 pips, GBP/USD 6.5-8.7 pips, USD/JPY up to 6.2 pips).
2. Weekend blackout: discard quotes from Friday 16:45 to Sunday 18:30 New York
   time (market close to 90 minutes after the Sunday reopen).
3. Boundary policy (enforced by the engine, "backtest" config section):
   - no entry is opened within 5 minutes of a blackout start;
   - any open position is flattened 2 minutes before a blackout start;
   - forward returns are scored on the quote prevailing at the target time
     (eval_asof_target), and horizons that touch a blackout are dropped and
     counted in the report.
4. Data hygiene:
   - rejects non-positive prices and crossed quotes (ask < bid);
   - enforces strictly increasing timestamps (ties bumped by +1 microsecond);
   - assigns contiguous 1-based sequence numbers.
"""

import argparse
import csv
import datetime
import pathlib
import sys
from typing import Dict, Any, Tuple


BLACKOUT_NY_START_MIN = 16 * 60 + 45   # 16:45 New York
BLACKOUT_NY_END_MIN = 18 * 60 + 30     # 18:30 New York


def _nth_sunday(year: int, month: int, n: int) -> datetime.date:
    first = datetime.date(year, month, 1)
    offset = (6 - first.weekday()) % 7
    return first + datetime.timedelta(days=offset + 7 * (n - 1))


def new_york_offset_hours(dt_utc: datetime.datetime) -> int:
    """UTC offset of America/New_York under the US rule in force since 2007.

    DST starts 07:00 UTC on the second Sunday of March and ends 06:00 UTC on the
    first Sunday of November: -4 h (EDT) inside, -5 h (EST) outside.
    """
    y = dt_utc.year
    start = datetime.datetime.combine(_nth_sunday(y, 3, 2), datetime.time(7),
                                      tzinfo=datetime.timezone.utc)
    end = datetime.datetime.combine(_nth_sunday(y, 11, 1), datetime.time(6),
                                    tzinfo=datetime.timezone.utc)
    return -4 if start <= dt_utc < end else -5


def is_excluded_time(dt_utc: datetime.datetime) -> Tuple[bool, str]:
    """Checks whether a UTC timestamp falls within the pre-registered New York blackout."""
    local = dt_utc + datetime.timedelta(hours=new_york_offset_hours(dt_utc))
    wd = local.weekday()  # Monday=0 ... Sunday=6
    m = local.hour * 60 + local.minute
    if wd == 5:
        return True, "weekend_saturday"
    if wd == 4 and m >= BLACKOUT_NY_START_MIN:
        return True, "weekend_friday_close"
    if wd == 6 and m < BLACKOUT_NY_END_MIN:
        return True, "weekend_sunday_reopen"
    if BLACKOUT_NY_START_MIN <= m < BLACKOUT_NY_END_MIN:
        return True, "daily_rollover_ny"
    return False, ""


def convert_file(raw_csv_path: pathlib.Path, output_csv_path: pathlib.Path, symbol: str) -> Dict[str, Any]:
    output_csv_path.parent.mkdir(parents=True, exist_ok=True)
    
    stats = {
        "raw_rows": 0,
        "valid_rows": 0,
        "excluded_weekend": 0,
        "excluded_rollover": 0,
        "excluded_hygiene": 0,
        "first_ts_ns": None,
        "last_ts_ns": None,
        "first_dt_utc": None,
        "last_dt_utc": None,
    }

    last_ts_ns = 0
    seq = 1

    with raw_csv_path.open("r", encoding="utf-8") as fin, output_csv_path.open("w", newline="", encoding="utf-8") as fout:
        reader = csv.reader(fin)
        writer = csv.writer(fout)

        # Write VECTRA standard header
        writer.writerow(["symbol", "bid", "ask", "source_ts_ns", "sequence"])

        header = next(reader, None)
        if not header:
            raise ValueError(f"Empty raw file: {raw_csv_path}")

        # Map header columns
        # Dukascopy CSV header: timestamp,askPrice,bidPrice,askVolume,bidVolume
        col_map = {col.strip().lower(): idx for idx, col in enumerate(header)}
        ts_col = col_map.get("timestamp", 0)
        ask_col = col_map.get("askprice", 1)
        bid_col = col_map.get("bidprice", 2)

        for row in reader:
            if not row or len(row) < 3:
                continue

            stats["raw_rows"] += 1

            try:
                ts_ms = int(row[ts_col])
                ask = float(row[ask_col])
                bid = float(row[bid_col])
            except (ValueError, IndexError):
                stats["excluded_hygiene"] += 1
                continue

            # Hygiene check: positive prices and uncrossed spread
            if bid <= 0.0 or ask <= 0.0 or ask < bid:
                stats["excluded_hygiene"] += 1
                continue

            # Check exclusion windows
            dt_utc = datetime.datetime.fromtimestamp(ts_ms / 1000.0, tz=datetime.timezone.utc)
            excluded, reason = is_excluded_time(dt_utc)
            if excluded:
                if "weekend" in reason:
                    stats["excluded_weekend"] += 1
                elif "rollover" in reason:
                    stats["excluded_rollover"] += 1
                continue

            # Convert to nanoseconds with strict monotonicity
            ts_ns = ts_ms * 1_000_000
            if ts_ns <= last_ts_ns:
                ts_ns = last_ts_ns + 1_000  # bump by 1 microsecond

            last_ts_ns = ts_ns

            # Determine precision
            decimals = 3 if "jpy" in symbol.lower() else 5
            bid_str = f"{bid:.{decimals}f}"
            ask_str = f"{ask:.{decimals}f}"

            writer.writerow([symbol.upper(), bid_str, ask_str, ts_ns, seq])
            seq += 1
            stats["valid_rows"] += 1

            if stats["first_ts_ns"] is None:
                stats["first_ts_ns"] = ts_ns
                stats["first_dt_utc"] = dt_utc.isoformat()
            stats["last_ts_ns"] = ts_ns
            stats["last_dt_utc"] = dt_utc.isoformat()

    return stats


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert raw Dukascopy tick CSV files to VECTRA schema.")
    parser.add_argument("--input-dir", default="data/raw", help="Directory containing raw Dukascopy tick CSVs")
    parser.add_argument("--output-dir", default="data/converted", help="Directory to save converted VECTRA CSVs")
    parser.add_argument("--instruments", nargs="+", default=["eurusd", "gbpusd", "usdjpy"], help="Instruments to convert")
    
    args = parser.parse_args()
    in_dir = pathlib.Path(args.input_dir)
    out_dir = pathlib.Path(args.output_dir)

    print("==================================================")
    print("VECTRA Dukascopy Data Converter")
    print(f"Source dir: {in_dir.resolve()}")
    print(f"Target dir: {out_dir.resolve()}")
    print("==================================================")

    for inst in args.instruments:
        matches = list(in_dir.glob(f"{inst.lower()}-tick-*.csv"))
        if not matches:
            print(f"WARNING: No matching files found for {inst} in {in_dir}", file=sys.stderr)
            continue
        
        raw_file = max(matches, key=lambda p: p.stat().st_mtime)
        out_file = out_dir / f"{inst.upper()}_ticks.csv"
        
        print(f"\nProcessing {inst.upper()}:")
        print(f"  Input:  {raw_file.name}")
        print(f"  Output: {out_file.name}")
        
        stats = convert_file(raw_file, out_file, inst.upper())
        print(f"  Raw ticks:        {stats['raw_rows']:,}")
        print(f"  Converted ticks:  {stats['valid_rows']:,}")
        print(f"  Weekend filtered: {stats['excluded_weekend']:,}")
        print(f"  Rollover filtered:{stats['excluded_rollover']:,}")
        print(f"  Hygiene filtered: {stats['excluded_hygiene']:,}")
        print(f"  First timestamp:  {stats['first_dt_utc']}")
        print(f"  Last timestamp:   {stats['last_dt_utc']}")
        print(f"  Output size:      {out_file.stat().st_size / (1024*1024):.2f} MB")

    print("\nConversion finished successfully.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
