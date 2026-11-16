#!/usr/bin/env python3
"""Generates cryptographic SHA-256 data manifest for VECTRA empirical evaluation.

Computes SHA-256 checksums, byte sizes, line counts, tick counts, and provider timestamps
for both raw downloaded tick archives and preprocessed/converted VECTRA CSV feeds.
The generated manifest is committed to version control BEFORE running any evaluation.
"""

import argparse
import csv
import datetime
import hashlib
import json
import pathlib
import sys
from typing import Dict, Any, List


def compute_sha256(file_path: pathlib.Path) -> str:
    """Computes SHA-256 hash of a file efficiently using 64KB blocks."""
    sha = hashlib.sha256()
    with file_path.open("rb") as f:
        while chunk := f.read(65536):
            sha.update(chunk)
    return sha.hexdigest()


def inspect_csv(file_path: pathlib.Path, is_raw: bool) -> Dict[str, Any]:
    """Extracts line count, tick count, byte size, SHA-256, and boundary timestamps."""
    size_bytes = file_path.stat().st_size
    sha256_hash = compute_sha256(file_path)

    total_lines = 0
    tick_count = 0
    first_dt_utc = None
    last_dt_utc = None

    with file_path.open("r", encoding="utf-8") as f:
        reader = csv.reader(f)
        header = next(reader, None)
        if header:
            total_lines += 1

        for row in reader:
            if not row:
                continue
            total_lines += 1
            tick_count += 1

            if is_raw:
                # Raw Dukascopy CSV: timestamp is in column 0 (ms)
                try:
                    ts_ms = int(row[0])
                    dt = datetime.datetime.fromtimestamp(ts_ms / 1000.0, tz=datetime.timezone.utc)
                except Exception:
                    continue
            else:
                # Converted VECTRA CSV: source_ts_ns is in column 3 (ns)
                try:
                    ts_ns = int(row[3])
                    dt = datetime.datetime.fromtimestamp(ts_ns / 1_000_000_000.0, tz=datetime.timezone.utc)
                except Exception:
                    continue

            if first_dt_utc is None:
                first_dt_utc = dt.isoformat()
            last_dt_utc = dt.isoformat()

    return {
        "file_name": file_path.name,
        "relative_path": file_path.as_posix(),
        "sha256": sha256_hash,
        "byte_size": size_bytes,
        "total_lines": total_lines,
        "tick_count": tick_count,
        "first_timestamp_utc": first_dt_utc,
        "last_timestamp_utc": last_dt_utc
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate cryptographic data manifest for VECTRA evaluation.")
    parser.add_argument("--raw-dir", default="data/raw", help="Directory of raw downloaded CSVs")
    parser.add_argument("--converted-dir", default="data/converted", help="Directory of converted VECTRA CSVs")
    parser.add_argument("--output", default="reports/data_manifest_q1_2023.json", help="Path to manifest JSON output")
    parser.add_argument("--instruments", nargs="+", default=["eurusd", "gbpusd", "usdjpy"], help="Instruments list")
    
    args = parser.parse_args()
    raw_dir = pathlib.Path(args.raw_dir)
    conv_dir = pathlib.Path(args.converted_dir)
    out_file = pathlib.Path(args.output)
    out_file.parent.mkdir(parents=True, exist_ok=True)

    manifest: Dict[str, Any] = {
        "protocol_version": "v1.6",
        "generated_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "provider": "Dukascopy Bank SA",
        "download_tool": "dukascopy-node@1.51.1",
        "raw_datasets": {},
        "converted_datasets": {}
    }

    print("==================================================")
    print("VECTRA Cryptographic Manifest Generator")
    print(f"Raw directory:       {raw_dir.resolve()}")
    print(f"Converted directory: {conv_dir.resolve()}")
    print(f"Output manifest:     {out_file.resolve()}")
    print("==================================================")

    for inst in args.instruments:
        # 1. Raw file
        raw_matches = list(raw_dir.glob(f"{inst.lower()}-tick-*.csv"))
        if raw_matches:
            raw_file = max(raw_matches, key=lambda p: p.stat().st_mtime)
            print(f"Hashing raw {inst.upper()}: {raw_file.name}...")
            manifest["raw_datasets"][inst.upper()] = inspect_csv(raw_file, is_raw=True)
        else:
            print(f"WARNING: No raw file found for {inst} in {raw_dir}", file=sys.stderr)

        # 2. Converted file
        conv_file = conv_dir / f"{inst.upper()}_ticks.csv"
        if conv_file.exists():
            print(f"Hashing converted {inst.upper()}: {conv_file.name}...")
            manifest["converted_datasets"][inst.upper()] = inspect_csv(conv_file, is_raw=False)
        else:
            print(f"WARNING: Converted file not found: {conv_file}", file=sys.stderr)

    out_file.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"\nManifest successfully generated and written to {out_file}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
