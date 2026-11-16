#!/usr/bin/env python3
"""End-to-end CLI scale-invariance test.

Verifies that executing `vectra.exe` via CLI with `--config config/experiment_eurusd.json`
and `--config config/experiment_usdjpy.json` on a 100x price-scaled tick file produces
strictly identical signal counts, fill counts, and a Net P&L scaled by exactly 100x.
"""

import csv
import math
import pathlib
import re
import subprocess
import sys

def main() -> int:
    build_dir = pathlib.Path("build")
    cli_path = build_dir / "Release" / "vectra.exe"
    if not cli_path.exists():
        cli_path = build_dir / "vectra.exe"
    if not cli_path.exists():
        print(f"Error: vectra executable not found at {cli_path}", file=sys.stderr)
        return 1

    base_csv = pathlib.Path("examples/real_eurusd_dev.csv")
    if not base_csv.exists():
        base_csv = pathlib.Path("examples/sample_ticks.csv")
    if not base_csv.exists():
        print(f"Error: {base_csv} not found", file=sys.stderr)
        return 1

    # Generate 100x scaled USDJPY sample ticks in build/
    scaled_csv = build_dir / "test_cli_usdjpy_scaled.csv"
    scaled_csv.parent.mkdir(parents=True, exist_ok=True)
    with base_csv.open("r", encoding="utf-8") as fin, scaled_csv.open("w", newline="", encoding="utf-8") as fout:
        reader = csv.reader(fin)
        writer = csv.writer(fout)
        for row in reader:
            if not row or row[0].startswith("#") or row[0] == "symbol":
                writer.writerow(row)
                continue
            # symbol,bid,ask,source_ts_ns,sequence
            try:
                b = f"{float(row[1]) * 100.0:.5f}"
                a = f"{float(row[2]) * 100.0:.5f}"
            except ValueError:
                b, a = row[1], row[2]
            writer.writerow(["USDJPY", b, a, row[3], row[4]])

    # 1. Run EURUSD CLI
    cmd_eur = [
        str(cli_path),
        "--config", "config/experiment_eurusd.json",
        "--data", str(base_csv),
        "--friction", "0.3"
    ]
    p_eur = subprocess.run(cmd_eur, capture_output=True, text=True, check=True)

    # 2. Run USDJPY CLI (100x scaled)
    cmd_jpy = [
        str(cli_path),
        "--config", "config/experiment_usdjpy.json",
        "--data", str(scaled_csv),
        "--friction", "0.3"
    ]
    p_jpy = subprocess.run(cmd_jpy, capture_output=True, text=True, check=True)

    def parse_output(out_str: str):
        stats = {"signals": 0, "orders": 0, "fills": 0, "gross_pnl": 0.0, "net_pnl": 0.0}
        for line in out_str.splitlines():
            line = line.strip()
            if "usable signals:" in line:
                m = re.search(r"usable signals:\s+(\d+)", line)
                if m:
                    stats["signals"] = int(m.group(1))
            elif line.startswith("orders:"):
                m = re.search(r"orders:\s+(\d+)\s+submitted,\s+(\d+)\s+fills", line)
                if m:
                    stats["orders"] = int(m.group(1))
                    stats["fills"] = int(m.group(2))
            elif "gross P&L:" in line and "net P&L:" in line:
                m = re.search(r"gross P&L:\s+([+-]?[\d\.]+)\s+net P&L:\s+([+-]?[\d\.]+)", line)
                if m:
                    stats["gross_pnl"] = float(m.group(1))
                    stats["net_pnl"] = float(m.group(2))
        return stats

    s_eur = parse_output(p_eur.stdout)
    s_jpy = parse_output(p_jpy.stdout)

    print(f"EUR/USD Base:     signals={s_eur['signals']}, fills={s_eur['fills']}, net={s_eur['net_pnl']:+.2f} USD")
    print(f"USD/JPY (100x):   signals={s_jpy['signals']}, fills={s_jpy['fills']}, net={s_jpy['net_pnl']:+.2f} JPY")

    assert s_eur["signals"] > 0, "Expected positive signal count"
    assert s_eur["signals"] == s_jpy["signals"], f"Signals mismatch: {s_eur['signals']} vs {s_jpy['signals']}"
    assert s_eur["orders"] == s_jpy["orders"], f"Orders mismatch: {s_eur['orders']} vs {s_jpy['orders']}"
    assert s_eur["fills"] == s_jpy["fills"], f"Fills mismatch: {s_eur['fills']} vs {s_jpy['fills']}"
    
    expected_jpy_net = s_eur["net_pnl"] * 100.0
    diff = abs(s_jpy["net_pnl"] - expected_jpy_net)
    assert diff < 0.05, f"Net P&L not scaled by 100x: EUR={s_eur['net_pnl']}, JPY={s_jpy['net_pnl']}, expected={expected_jpy_net}"

    expected_jpy_gross = s_eur["gross_pnl"] * 100.0
    gross_diff = abs(s_jpy["gross_pnl"] - expected_jpy_gross)
    assert gross_diff < 0.05, f"Gross P&L not scaled by 100x: EUR={s_eur['gross_pnl']}, JPY={s_jpy['gross_pnl']}, expected={expected_jpy_gross}"

    print("PASS: End-to-end CLI scale invariance verified successfully.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
