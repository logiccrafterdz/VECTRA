#!/usr/bin/env python3
"""Dukascopy Tick Data Downloader for VECTRA Empirical Experiment.

Downloads raw institutional tick data (bid/ask prices with millisecond timestamps and volumes)
from Dukascopy Bank SA via dukascopy-node CLI.

Default configuration targets Q1 2023 (Jan 1, 2023 to Apr 1, 2023 UTC, corresponding to
Jan 1, 2023 00:00:00 UTC through Mar 31, 2023 23:59:59 UTC).
"""

import argparse
import pathlib
import shutil
import subprocess
import sys
from typing import List

DEFAULT_INSTRUMENTS = ["eurusd", "gbpusd", "usdjpy"]
DEFAULT_FROM = "2023-01-01"
DEFAULT_TO = "2023-04-01"
DEFAULT_OUT_DIR = "data/raw"


def download_pair(instrument: str, from_date: str, to_date: str, output_dir: pathlib.Path,
                  batch_size: int = 10, batch_pause_ms: int = 1000) -> pathlib.Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    
    npx_bin = shutil.which("npx") or "npx"
    cmd = [
        npx_bin, "dukascopy-node",
        "-i", instrument.lower(),
        "-from", from_date,
        "-to", to_date,
        "-t", "tick",
        "-f", "csv",
        "-dir", str(output_dir),
        "--volumes",
        "-bs", str(batch_size),
        "-bp", str(batch_pause_ms),
        "--rate-limit-retries", "5",
        "-r", "3"
    ]
    
    print(f"\n[Dukascopy Downloader] Starting download for {instrument.upper()} from {from_date} to {to_date}...")
    print(f"Command: {' '.join(cmd)}")
    
    res = subprocess.run(cmd, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"Download failed for {instrument} with exit code {res.returncode}")
        
    expected_pattern = f"{instrument.lower()}-tick-*.csv"
    matches = list(output_dir.glob(expected_pattern))
    if not matches:
        raise FileNotFoundError(f"Could not locate downloaded file matching pattern {expected_pattern} in {output_dir}")
        
    # Return newest matching file
    newest = max(matches, key=lambda p: p.stat().st_mtime)
    print(f"[Dukascopy Downloader] Successfully downloaded {newest.name} ({newest.stat().st_size / (1024*1024):.2f} MB)")
    return newest


def main() -> int:
    parser = argparse.ArgumentParser(description="Download Dukascopy tick data for VECTRA pre-registered experiment.")
    parser.add_argument("--instruments", nargs="+", default=DEFAULT_INSTRUMENTS, help="List of currency pairs (e.g. eurusd gbpusd usdjpy)")
    parser.add_argument("--from-date", default=DEFAULT_FROM, help="Start date YYYY-MM-DD (UTC)")
    parser.add_argument("--to-date", default=DEFAULT_TO, help="End date YYYY-MM-DD (exclusive, UTC)")
    parser.add_argument("--output-dir", default=DEFAULT_OUT_DIR, help="Destination directory for raw CSV downloads")
    parser.add_argument("--batch-size", type=int, default=10, help="Dukascopy batch size")
    parser.add_argument("--batch-pause", type=int, default=1000, help="Pause between batches in ms")
    
    args = parser.parse_args()
    out_dir = pathlib.Path(args.output_dir)
    
    print(f"==================================================")
    print(f"VECTRA Dukascopy Ingestion Pipeline")
    print(f"Instruments: {', '.join(i.upper() for i in args.instruments)}")
    print(f"Date Range:  {args.from_date} to {args.to_date} (UTC)")
    print(f"Destination: {out_dir.resolve()}")
    print(f"==================================================")
    
    downloaded = []
    for pair in args.instruments:
        try:
            f = download_pair(pair, args.from_date, args.to_date, out_dir, args.batch_size, args.batch_pause)
            downloaded.append(f)
        except Exception as e:
            print(f"ERROR: Failed downloading {pair}: {e}", file=sys.stderr)
            return 1
            
    print(f"\nAll {len(downloaded)} downloads completed successfully.")
    for p in downloaded:
        print(f" - {p}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
