"""Fetch real EUR/USD tick data from verified FXCM feed and convert to VECTRA CSV schema.

Supports single-file output or chronological 3-way split (dev / val / oos) as required
by docs/audit_v0.1.md §6 and docs/data_limitations.md §6.
"""

import sys
import os
import datetime
import urllib.request

URL = "https://raw.githubusercontent.com/yhilpisch/py4fi2nd/master/source/fxcm_eur_usd_tick_data.csv"

def parse_fxcm_line(line, sequence, symbol="EURUSD"):
    parts = line.split(",")
    if len(parts) < 3:
        return None
    ts_str = parts[0].strip()
    bid_raw = parts[1].strip()
    ask_raw = parts[2].strip()
    if not ts_str or not bid_raw or not ask_raw:
        return None
    try:
        bid_f = float(bid_raw)
        ask_f = float(ask_raw)
        bid_str = f"{bid_f:.5f}"
        ask_str = f"{ask_f:.5f}"

        year = int(ts_str[0:4])
        month = int(ts_str[5:7])
        day = int(ts_str[8:10])
        hour = int(ts_str[11:13])
        minute = int(ts_str[14:16])
        second = int(ts_str[17:19])
        subsecond = ts_str[20:].ljust(6, '0')[:6]
        micros = int(subsecond)

        dt = datetime.datetime(year, month, day, hour, minute, second, tzinfo=datetime.timezone.utc)
        epoch = datetime.datetime(1970, 1, 1, tzinfo=datetime.timezone.utc)
        whole_seconds = int((dt - epoch).total_seconds())
        ts_ns = whole_seconds * 1_000_000_000 + micros * 1_000
        return f"{symbol},{bid_str},{ask_str},{ts_ns},{sequence}\n"
    except Exception:
        return None

def fetch_and_convert(max_rows=50000, out_path="examples/real_eurusd_ticks.csv", symbol="EURUSD"):
    print(f"Connecting to FXCM tick data stream ({URL}) ...", flush=True)
    req = urllib.request.Request(URL, headers={"User-Agent": "Mozilla/5.0"})
    os.makedirs(os.path.dirname(out_path), exist_ok=True)

    count = 0
    skipped = 0
    sequence = 1

    with urllib.request.urlopen(req, timeout=30) as resp, open(out_path, "w", encoding="utf-8") as out_f:
        out_f.write("symbol,bid,ask,source_ts_ns,sequence\n")
        resp.readline()  # skip header

        for line_bytes in resp:
            line = line_bytes.decode("utf-8").strip()
            if not line:
                continue
            formatted = parse_fxcm_line(line, sequence, symbol)
            if formatted is None:
                skipped += 1
                continue
            out_f.write(formatted)
            sequence += 1
            count += 1
            if count % 25000 == 0:
                print(f"Processed {count} ticks...", flush=True)
            if max_rows and count >= max_rows:
                break

    print(f"Done! Converted {count} real EURUSD ticks (skipped {skipped}) to {out_path}", flush=True)

def fetch_and_split(dev_rows=100000, val_rows=100000, oos_rows=100000, oos2_rows=0, symbol="EURUSD"):
    print(f"Downloading and splitting FXCM stream into chronological dev/val/oos/oos2 sets...", flush=True)
    req = urllib.request.Request(URL, headers={"User-Agent": "Mozilla/5.0"})
    os.makedirs("examples", exist_ok=True)

    splits = [
        ("dev", "examples/real_eurusd_dev.csv", dev_rows),
        ("val", "examples/real_eurusd_val.csv", val_rows),
        ("oos", "examples/real_eurusd_oos.csv", oos_rows),
        ("oos2", "examples/real_eurusd_oos2.csv", oos2_rows),
    ]

    with urllib.request.urlopen(req, timeout=60) as resp:
        resp.readline()  # skip header
        for name, path, target_count in splits:
            count = 0
            sequence = 1
            with open(path, "w", encoding="utf-8") as out_f:
                out_f.write("symbol,bid,ask,source_ts_ns,sequence\n")
                for line_bytes in resp:
                    line = line_bytes.decode("utf-8").strip()
                    if not line:
                        continue
                    formatted = parse_fxcm_line(line, sequence, symbol)
                    if formatted is None:
                        continue
                    out_f.write(formatted)
                    sequence += 1
                    count += 1
                    if target_count and count >= target_count:
                        break
            print(f"  [{name.upper()}] Wrote {count} ticks -> {path}", flush=True)

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--split":
        fetch_and_split(100000, 100000, 100000)
    else:
        n = int(sys.argv[1]) if len(sys.argv) > 1 else 50000
        fetch_and_convert(max_rows=n)
