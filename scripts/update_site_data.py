#!/usr/bin/env python3
"""
Periodic Data Updater for the VECTRA GitHub Pages Documentation & Research Portal.

Parses the generated Markdown reports in reports/ and config files in config/,
writes structured JSON data to docs/data/metrics.json, and synchronizes the
embedded data payload inside docs/index.html so the page stays up to date both
on GitHub Pages and when opened locally.
"""

import datetime
import json
import os
import re

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPORTS_DIR = os.path.join(ROOT_DIR, "reports")
DOCS_DIR = os.path.join(ROOT_DIR, "docs")
DATA_DIR = os.path.join(DOCS_DIR, "data")
METRICS_JSON_PATH = os.path.join(DATA_DIR, "metrics.json")
INDEX_HTML_PATH = os.path.join(DOCS_DIR, "index.html")


def extract_kv_table(section_text):
    result = {}
    for line in section_text.splitlines():
        line = line.strip()
        if not line.startswith("|") or line.startswith("|---") or line.startswith("| Metric"):
            continue
        parts = [p.strip() for p in line.strip("|").split("|")]
        if len(parts) >= 2:
            result[parts[0]] = parts[1]
    return result


def parse_number(val_str, default=0.0):
    if not val_str:
        return default
    m = re.search(r"[-+]?\d+(?:\.\d+)?", val_str.replace(" ", ""))
    return float(m.group(0)) if m else default


def parse_int(val_str, default=0):
    if not val_str:
        return default
    m = re.search(r"\d+", val_str.replace(" ", ""))
    return int(m.group(0)) if m else default


def parse_report(md_path, key, label, session_desc):
    with open(md_path, "r", encoding="utf-8") as f:
        text = f.read()

    def get_section(header):
        idx = text.find(header)
        if idx < 0:
            return ""
        rest = text[idx + len(header):]
        next_h = re.search(r"\n##\s+\d+\.", rest)
        return rest[:next_h.start()] if next_h else rest

    sec1 = extract_kv_table(get_section("## 1. Market data"))
    sec2 = extract_kv_table(get_section("## 2. Predictions and signal distribution"))
    sec3 = extract_kv_table(get_section("## 3. Decisions and risk"))
    sec4 = extract_kv_table(get_section("## 4. Execution (simulated)"))
    sec5 = extract_kv_table(get_section("## 5. Simulated performance"))

    # Parse causal forward-return table
    forward_rows = []
    fwd_match = re.search(
        r"### Causal forward-return evaluation[^\n]*\n+((?:\|[^\n]+\n)+)", text
    )
    if fwd_match:
        for line in fwd_match.group(1).splitlines():
            if not line.startswith("|") or "---" in line or "Signal kind" in line:
                continue
            cols = [c.strip() for c in line.strip("|").split("|")]
            if len(cols) >= 5:
                forward_rows.append({
                    "kind": cols[0],
                    "evaluated": parse_int(cols[1]),
                    "hit_rate_pct": parse_number(cols[2]) if cols[2] != "n/a" else None,
                    "mean_mid_move": parse_number(cols[3]) if cols[3] != "n/a" else None,
                    "mean_spread_move": parse_number(cols[4]) if cols[4] != "n/a" else None,
                })

    # Parse non-overlapping block table
    block_rows = []
    blk_match = re.search(
        r"#### Autocorrelation-robust non-overlapping horizon blocks\n+((?:\|[^\n]+\n)+)",
        text,
    )
    if blk_match:
        for line in blk_match.group(1).splitlines():
            if not line.startswith("|") or "---" in line or "Signal kind" in line:
                continue
            cols = [c.strip() for c in line.strip("|").split("|")]
            if len(cols) >= 5:
                block_rows.append({
                    "kind": cols[0],
                    "blocks": parse_int(cols[1]),
                    "hit_rate_pct": parse_number(cols[2]) if cols[2] != "n/a" else None,
                    "mean_spread_move": parse_number(cols[3]) if cols[3] != "n/a" else None,
                    "t_stat": parse_number(cols[4]) if cols[4] != "n/a" else None,
                })

    # Parse 5-bin calibration table
    cal_rows = []
    cal_match = re.search(
        r"#### Score reliability & Brier calibration[^\n]*\n+((?:\|[^\n]+\n)+)", text
    )
    if cal_match:
        for line in cal_match.group(1).splitlines():
            if not line.startswith("|") or "---" in line or "Score bin" in line:
                continue
            cols = [c.strip() for c in line.strip("|").split("|")]
            if len(cols) >= 6:
                cal_rows.append({
                    "bin": cols[0],
                    "evaluated": parse_int(cols[1]),
                    "mean_score": parse_number(cols[2]),
                    "hit_rate_pct": parse_number(cols[3]),
                    "mean_spread_move": parse_number(cols[4]),
                    "brier_score": parse_number(cols[5]),
                })

    # Parse latency table
    latency_rows = []
    lat_match = re.search(r"## 6\. Latency[^\n]*\n+((?:\|[^\n]+\n)+)", text)
    if lat_match:
        for line in lat_match.group(1).splitlines():
            if not line.startswith("|") or "---" in line or "Stage" in line:
                continue
            cols = [c.strip() for c in line.strip("|").split("|")]
            if len(cols) >= 5:
                latency_rows.append({
                    "stage": cols[0],
                    "median_us": parse_number(cols[1]) if "n/a" not in cols[1] else None,
                    "p95_us": parse_number(cols[2]) if "n/a" not in cols[2] else None,
                    "p99_us": parse_number(cols[3]) if "n/a" not in cols[3] else None,
                    "samples": parse_int(cols[4]),
                })

    digest_match = re.search(r"Run digest[^`]*`([0-9a-fA-F]+)`", text)
    dd_raw = sec5.get("Max drawdown", "0.0 (0.0%)")
    dd_pct_m = re.search(r"\(([-0-9.]+)%\)", dd_raw)

    return {
        "key": key,
        "label": label,
        "session": session_desc,
        "report_file": os.path.basename(md_path),
        "digest": digest_match.group(1) if digest_match else "",
        "market": {
            "lines_read": parse_int(sec1.get("Lines read")),
            "events_valid": parse_int(sec1.get("Valid events")),
            "events_rejected": parse_int(sec1.get("Rejected events")),
            "events_malformed": parse_int(sec1.get("Malformed rows")),
            "stale_gaps": parse_int(sec1.get("Stale gaps flagged")),
            "rejection_reasons": sec1.get("Rejection reasons", ""),
            "duration": sec1.get("Stream duration", ""),
        },
        "predictions": {
            "total": parse_int(sec2.get("Event-level predictions")),
            "usable": parse_int(sec2.get("Usable signals (valid, non-neutral)")),
            "kind_distribution": sec2.get("Kind distribution", ""),
            "rejection_reasons": sec2.get("Rejection reasons", ""),
            "signal_rate_pct": parse_number(sec2.get("Signal rate")),
        },
        "decisions": {
            "allowed": parse_int(sec3.get("Decisions allowed")),
            "rejected": parse_int(sec3.get("Decisions rejected")),
            "rejection_reasons": sec3.get("Rejection reasons", ""),
            "min_expected_edge": parse_number(sec3.get("Min expected edge (price units)")),
        },
        "execution": {
            "orders_submitted": parse_int(sec4.get("Order submissions")),
            "fills": parse_int(sec4.get("Fill events")),
            "partial_fills": parse_int(sec4.get("of which partial fills")),
            "horizon_exits": parse_int(sec4.get("Horizon exits submitted")),
            "stop_loss_exits": parse_int(sec4.get("Stop-loss exits submitted")),
            "take_profit_exits": parse_int(sec4.get("Take-profit exits submitted")),
            "reconciliation": sec4.get("Reconciliation", ""),
        },
        "performance": {
            "initial_equity": parse_number(sec5.get("Initial equity"), 100000.0),
            "final_equity": parse_number(sec5.get("Final equity"), 100000.0),
            "realized_pnl": parse_number(sec5.get("Realized P&L")),
            "gross_pnl": parse_number(
                sec5.get("Gross P&L (after spread, before commission/slippage)")
            ),
            "net_pnl": parse_number(sec5.get("Net P&L")),
            "commission_paid": parse_number(sec5.get("Commission paid")),
            "slippage_paid": parse_number(sec5.get("Slippage paid")),
            "spread_cost_info": parse_number(
                sec5.get("Estimated spread cost (informational)")
            ),
            "max_drawdown": parse_number(dd_raw),
            "max_drawdown_pct": float(dd_pct_m.group(1)) if dd_pct_m else 0.0,
        },
        "forward_evaluation": forward_rows,
        "non_overlapping_blocks": block_rows,
        "calibration_bins": cal_rows,
        "latency": latency_rows,
    }


def build_payload():
    os.makedirs(DATA_DIR, exist_ok=True)
    with open(os.path.join(ROOT_DIR, "config", "real_eurusd.json"), "r", encoding="utf-8") as f:
        real_cfg = json.load(f)

    splits_spec = [
        ("dev", "real_eurusd_dev_report.md", "DEV Split (Ticks 1-100k)", "Asian to Early London Session (6.48 hours)"),
        ("val", "real_eurusd_val_report.md", "VAL Split (Ticks 100k-200k)", "London Peak Session (2.95 hours)"),
        ("oos", "real_eurusd_oos_report.md", "OOS 1 Split (Ticks 200k-300k)", "London / New York Overlap (3.55 hours)"),
        ("oos2", "real_eurusd_oos2_report.md", "OOS 2 Holdout (Ticks 300k-461.4k)", "US / Late New York Session (8.00 hours)"),
        ("synthetic", "sample_report.md", "Synthetic Smoke Benchmark (6k Ticks)", "Deterministic Anomaly & Pipeline Test (10.00 minutes)"),
    ]

    datasets = {}
    for key, fname, label, desc in splits_spec:
        p = os.path.join(REPORTS_DIR, fname)
        if os.path.exists(p):
            datasets[key] = parse_report(p, key, label, desc)

    real_keys = [k for k in ["dev", "val", "oos", "oos2"] if k in datasets]
    total_lines = sum(datasets[k]["market"]["lines_read"] for k in real_keys)
    total_valid = sum(datasets[k]["market"]["events_valid"] for k in real_keys)
    total_rejected = sum(datasets[k]["market"]["events_rejected"] for k in real_keys)
    total_signals = sum(datasets[k]["predictions"]["usable"] for k in real_keys)
    total_fills = sum(datasets[k]["execution"]["fills"] for k in real_keys)
    total_gross = round(sum(datasets[k]["performance"]["gross_pnl"] for k in real_keys), 2)
    total_net = round(sum(datasets[k]["performance"]["net_pnl"] for k in real_keys), 2)
    oos_combined_gross = round(
        sum(datasets[k]["performance"]["gross_pnl"] for k in ["oos", "oos2"] if k in datasets), 2
    )
    oos_combined_net = round(
        sum(datasets[k]["performance"]["net_pnl"] for k in ["oos", "oos2"] if k in datasets), 2
    )
    max_dd = max((datasets[k]["performance"]["max_drawdown"] for k in real_keys), default=0.0)
    max_dd_pct = max((datasets[k]["performance"]["max_drawdown_pct"] for k in real_keys), default=0.0)

    payload = {
        "generated_at_utc": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC"),
        "engine_version": "0.1.0",
        "unit_tests_passed": 115,
        "unit_tests_total": 115,
        "ctest_targets_passed": 2,
        "sample_replay_digest": "bc06ff21b93aecfb",
        "aggregate_real_eurusd": {
            "total_ticks_read": total_lines,
            "total_valid_events": total_valid,
            "total_rejected_events": total_rejected,
            "total_hours": 20.98,
            "total_usable_signals": total_signals,
            "total_fills": total_fills,
            "cumulative_gross_pnl": total_gross,
            "cumulative_net_pnl": total_net,
            "combined_oos_gross_pnl": oos_combined_gross,
            "combined_oos_net_pnl": oos_combined_net,
            "worst_split_drawdown": max_dd,
            "worst_split_drawdown_pct": max_dd_pct,
        },
        "engineering_progression": [
            {
                "stage": "Baseline (v0.1 Default Config)",
                "description": "Tick-counted windows (8/32/128), one-way cost gate, no cooldown, no position exit lifecycle.",
                "dev_net_pnl": -94.95,
                "val_net_pnl": None,
                "oos_net_pnl": None,
                "oos2_net_pnl": None,
                "cumulative_net_pnl": -94.95,
                "fills_dev": 408,
            },
            {
                "stage": "Stage 1: Round-Trip Cost, Cooldown & Lifecycle Exits",
                "description": "Added 2x commission/slippage gate, cooldown timer, horizon exits, and stop-loss/take-profit.",
                "dev_net_pnl": 14.35,
                "val_net_pnl": -26.40,
                "oos_net_pnl": -22.25,
                "oos2_net_pnl": None,
                "cumulative_net_pnl": -34.30,
                "fills_dev": 42,
            },
            {
                "stage": "Stage 2: Wall-Clock Windows & Multi-Window Deceleration",
                "description": "Anchored observation windows (120s/240s/720s) to provider timestamps; added scale-invariant deceleration.",
                "dev_net_pnl": 11.30,
                "val_net_pnl": 11.10,
                "oos_net_pnl": -16.80,
                "oos2_net_pnl": None,
                "cumulative_net_pnl": 5.60,
                "fills_dev": 26,
            },
            {
                "stage": "Stage 3: Regime Efficiency & Intra-Window Exhaustion",
                "description": "Added block path efficiency filter (min_continuation_efficiency=0.55) and intra-window stall detection.",
                "dev_net_pnl": 33.20,
                "val_net_pnl": 0.10,
                "oos_net_pnl": -4.00,
                "oos2_net_pnl": -6.70,
                "cumulative_net_pnl": 22.60,
                "fills_dev": 88,
            },
            {
                "stage": "Stage 4: Score-Bin Calibration & Expected-Move Scaling",
                "description": "Normalized expected-move scale (reversal_move_scale=0.45) using DEV/VAL Brier reliability analysis.",
                "dev_net_pnl": 18.90,
                "val_net_pnl": 7.00,
                "oos_net_pnl": -0.70,
                "oos2_net_pnl": 1.50,
                "cumulative_net_pnl": 26.70,
                "fills_dev": 36,
            },
        ],
        "active_config": real_cfg,
        "datasets": datasets,
    }

    with open(METRICS_JSON_PATH, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2)
        f.write("\n")

    if os.path.exists(INDEX_HTML_PATH):
        with open(INDEX_HTML_PATH, "r", encoding="utf-8") as f:
            html = f.read()
        pattern = r"(<!-- VECTRA_DATA_START -->\s*<script id=\"vectra-embedded-data\" type=\"application/json\">)(.*?)(</script>\s*<!-- VECTRA_DATA_END -->)"
        if re.search(pattern, html, flags=re.DOTALL):
            new_html = re.sub(
                pattern,
                lambda m: m.group(1) + "\n" + json.dumps(payload, indent=2) + "\n" + m.group(3),
                html,
                flags=re.DOTALL,
            )
            with open(INDEX_HTML_PATH, "w", encoding="utf-8") as f:
                f.write(new_html)
            print(f"Updated embedded data in {INDEX_HTML_PATH}")

    print(f"Wrote {METRICS_JSON_PATH} successfully.")


if __name__ == "__main__":
    build_payload()
