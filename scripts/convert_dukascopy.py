#!/usr/bin/env python3
"""Converts raw Dukascopy tick CSV files into VECTRA CSV schema with pre-registered exclusions.

Target Schema:
    symbol,bid,ask,source_ts_ns,sequence

Pre-Registered Filtering Rules (Protocol v1.6):
1. Weekend Closure:
   - Discards quotes between Friday 21:00:00 UTC and Sunday 21:00:00 UTC.
2. Daily Rollover Window:
   - Discards quotes between 21:00:00 UTC and 21:59:59.999 UTC (Monday through Thursday)
     to prevent artificial rollover spread spikes from entering signal calculations.
3. Boundary Horizon Policy:
   - Filtering the 21:00-22:00 window leaves a 1-hour time gap.
   - The engine causal lifecycle automatically triggers horizon exit at the reopening
     tick (22:00:00 UTC) if a trade was held past its 180s horizon.
   - The MarketValidator detects a gap > 15s and sets `stale_gap = true`, safely blocking
     new order entries until the feed stabilizes.
4. Data Hygiene:
   - Rejects non-positive prices and crossed spreads (ask < bid).
   - Enforces strictly monotonic timestamps (adjusts duplicates by +1 microsecond).
   - Assigns contiguous 1-based sequence numbers.
"""

import argparse
import csv
import datetime
import pathlib
import sys
from typing import Dict, Any, Tuple


def is_excluded_time(dt_utc: datetime.datetime) -> Tuple[bool, str]:
    """Checks whether a UTC timestamp falls within pre-registered exclusion periods."""
    weekday = dt_utc.weekday()  # Monday=0 ... Sunday=6
    hour = dt_utc.hour

    # 1. Weekend filter: Friday 21:00 UTC to Sunday 21:00 UTC
    if weekday == 4 and hour >= 21:
        return True, "weekend_friday_post_21"
    if weekday == 5:
        return True, "weekend_saturday"
    if weekday == 6 and hour < 21:
        return True, "weekend_sunday_pre_21"

    # 2. Daily rollover window: 21:00 to 22:00 UTC
    if hour == 21:
        return True, "daily_rollover_21_to_22"

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
