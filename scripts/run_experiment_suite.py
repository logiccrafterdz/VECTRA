#!/usr/bin/env python3
"""
VECTRA Empirical Experiment Suite (v1.2)
Automates the pre-registered execution of:
1. Full Model (Primary Hypothesis H1, 0.30 pips friction)
2. Leave-One-Out (LOO) Ablations (4 tests)
3. Single-Feature Isolations (4 tests)
4. Friction Sensitivity Curve (10 levels: 0.1 to 1.0 pips)
5. Holm-Bonferroni correction and Markdown summary tables.
"""

import argparse
import json
import math
import os
import pathlib
import re
import subprocess
import sys
from typing import Dict, List, Optional, Any

CLI_PATH = pathlib.Path("build/Release/vectra.exe")
if not CLI_PATH.exists():
    CLI_PATH = pathlib.Path("build/vectra.exe")


def run_vectra_replay(data_path: str, config_path: str, ablation: Optional[str] = None,
                      friction_pips: Optional[float] = None) -> Dict[str, Any]:
    cmd = [str(CLI_PATH), "--data", data_path, "--config", config_path]
    if ablation and ablation != "none":
        cmd.extend(["--ablation", ablation])
    if friction_pips is not None:
        cmd.extend(["--friction", str(friction_pips)])
    
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
    out = proc.stdout
    
    res: Dict[str, Any] = {
        "ablation": ablation or "none",
        "friction_pips": friction_pips if friction_pips is not None else 0.30,
        "valid_events": 0,
        "usable_signals": 0,
        "orders": 0,
        "fills": 0,
        "net_pnl": 0.0,
        "gross_pnl": 0.0,
        "max_drawdown": 0.0,
        "blocks": 0,
        "block_mean_spread": 0.0,
        "block_mean_net": 0.0,
        "net_t_stat": 0.0,
        "paired_t_stat": 0.0,
        "paired_blocks": 0,
        "digest": ""
    }
    
    # Parse CLI console output
    for line in out.splitlines():
        line = line.strip()
        if line.startswith("events:"):
            # events: 96419 valid, 3581 rejected
            m = re.search(r"(\d+)\s+valid", line)
            if m:
                res["valid_events"] = int(m.group(1))
        elif line.startswith("signals:"):
            # signals: 6422 usable (6.66%), 0 neutral
            m = re.search(r"(\d+)\s+usable", line)
            if m:
                res["usable_signals"] = int(m.group(1))
        elif line.startswith("orders:"):
            # orders: 36 submitted, 36 fills
            m = re.search(r"(\d+)\s+submitted,\s+(\d+)\s+fills", line)
            if m:
                res["orders"] = int(m.group(1))
                res["fills"] = int(m.group(2))
        elif "gross P&L:" in line and "net P&L:" in line:
            m = re.search(r"gross P&L:\s+([+-]?[\d\.]+)\s+net P&L:\s+([+-]?[\d\.]+)", line)
            if m:
                res["gross_pnl"] = float(m.group(1))
                res["net_pnl"] = float(m.group(2))
        elif line.startswith("max drawdown:"):
            # max drawdown: 16.10
            m = re.search(r"max drawdown:\s+([\d\.]+)", line)
            if m:
                res["max_drawdown"] = float(m.group(1))
        elif line.startswith("digest:"):
            res["digest"] = line.split(":", 1)[1].strip()

    # To get exact non-overlapping block stats, also write a temporary report
    report_file = pathlib.Path("build/_temp_report.md")
    report_cmd = cmd + ["--report", str(report_file)]
    subprocess.run(report_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
    
    if report_file.exists():
        rpt = report_file.read_text(encoding="utf-8")
        # Look for the non-overlapping table:
        # | All usable signals | 51 | 50.98% | 0.000045 | 0.000015 | +1.46 | +1.20 (n=51) |
        for rline in rpt.splitlines():
            if "| All usable signals |" in rline:
                cols = [c.strip() for c in rline.split("|")[1:-1]]
                if len(cols) >= 7:
                    try:
                        res["blocks"] = int(cols[1])
                        res["block_mean_spread"] = float(cols[3])
                        res["block_mean_net"] = float(cols[4])
                        res["net_t_stat"] = float(cols[5])
                        pm = re.search(r"([+-]?[\d\.]+)\s+\(n=(\d+)\)", cols[6])
                        if pm:
                            res["paired_t_stat"] = float(pm.group(1))
                            res["paired_blocks"] = int(pm.group(2))
                    except Exception:
                        pass
        report_file.unlink(missing_ok=True)
        
    return res


def normal_p_value_two_tailed(t_stat: float) -> float:
    """Approximate two-tailed p-value for large degrees of freedom."""
    abs_t = abs(t_stat)
    # Using complementary error function for standard normal approximation
    return math.erfc(abs_t / math.sqrt(2.0))


def apply_holm_bonferroni(tests: List[Dict[str, Any]], alpha: float = 0.05) -> List[Dict[str, Any]]:
    """Applies Holm-Bonferroni step-down correction to a list of test results."""
    m = len(tests)
    # Sort ascending by unadjusted p-value
    sorted_indices = sorted(range(m), key=lambda i: tests[i]["p_value"])
    
    for rank, idx in enumerate(sorted_indices, 1):
        tests[idx]["holm_rank"] = rank
        raw_p = tests[idx]["p_value"]
        holm_crit = alpha / (m - rank + 1)
        tests[idx]["holm_threshold"] = holm_crit
        tests[idx]["holm_significant"] = raw_p < holm_crit
    return tests


def run_full_suite(data_path: str, config_path: str, output_md: str) -> None:
    print(f"=== Running Pre-Registered Experiment Suite v1.2 ===")
    print(f"Data:   {data_path}")
    print(f"Config: {config_path}")
    
    # 1. Primary Hypothesis H1: Full Composite Model at 0.30 pips friction
    print("\n--- [1] Primary Test: Full Model (0.30 pips friction) ---")
    h1_res = run_vectra_replay(data_path, config_path, ablation="none", friction_pips=0.30)
    h1_res["p_value"] = normal_p_value_two_tailed(h1_res["net_t_stat"])
    print(f"Blocks: {h1_res['blocks']}, Net t-stat: {h1_res['net_t_stat']}, p-val: {h1_res['p_value']:.4e}, Net P&L: {h1_res['net_pnl']} USD")

    # 2. Leave-One-Out (LOO) Ablations
    print("\n--- [2] Leave-One-Out (LOO) Ablations ---")
    loo_modes = [
        ("loo_no_extension", "LOO 1: Without Volatility Extension"),
        ("loo_no_deceleration", "LOO 2: Without Velocity Deceleration"),
        ("loo_no_fade", "LOO 3: Without Response Fade"),
        ("loo_no_efficiency", "LOO 4: Without Regime Efficiency"),
    ]
    loo_results = []
    for mode, desc in loo_modes:
        r = run_vectra_replay(data_path, config_path, ablation=mode, friction_pips=0.30)
        r["description"] = desc
        r["p_value"] = normal_p_value_two_tailed(r["net_t_stat"])
        loo_results.append(r)
        print(f"  {desc[:35]}: blocks={r['blocks']}, net_t={r['net_t_stat']}, Net P&L={r['net_pnl']} USD")

    # 3. Single-Feature Isolations
    print("\n--- [3] Single-Feature Isolation Rules ---")
    single_modes = [
        ("single_extension", "Single 1: Extension Only (|z| >= 1.8)"),
        ("single_deceleration", "Single 2: Deceleration Only (decel >= 0.50)"),
        ("single_fade", "Single 3: Response Fade Only (fade >= 0.50)"),
        ("single_efficiency", "Single 4: Regime Efficiency Only (eff >= 0.60)"),
    ]
    single_results = []
    for mode, desc in single_modes:
        r = run_vectra_replay(data_path, config_path, ablation=mode, friction_pips=0.30)
        r["description"] = desc
        r["p_value"] = normal_p_value_two_tailed(r["net_t_stat"])
        single_results.append(r)
        print(f"  {desc[:35]}: blocks={r['blocks']}, net_t={r['net_t_stat']}, Net P&L={r['net_pnl']} USD")

    # Apply Holm-Bonferroni across the 8 secondary exploratory tests
    secondary_family = loo_results + single_results
    apply_holm_bonferroni(secondary_family, alpha=0.05)

    # 4. Friction Sensitivity Curve (10 levels: 0.1 to 1.0 pips)
    print("\n--- [4] Friction Sensitivity Curve (0.1 to 1.0 pips) ---")
    friction_levels = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0]
    friction_results = []
    breakeven_friction = None
    for f in friction_levels:
        r = run_vectra_replay(data_path, config_path, ablation="none", friction_pips=f)
        friction_results.append(r)
        print(f"  Friction {f:.1f} pips: Fills={r['fills']}, Gross={r['gross_pnl']:.2f}, Net={r['net_pnl']:.2f} USD, t={r['net_t_stat']}")
        if r["net_pnl"] >= 0.0:
            breakeven_friction = f

    # Generate Markdown Report
    lines = [
        "# VECTRA Empirical Experiment Results (Protocol v1.2)",
        "",
        f"- **Dataset**: `{data_path}`",
        f"- **Configuration**: `{config_path}`",
        f"- **CLI Version**: `v0.1.0`",
        "",
        "## 1. Primary Hypothesis Evaluation (H1)",
        "",
        "| Hypothesis | Blocks (180s) | Mean Net Return (pips) | Student t-stat (vs 0) | Two-Tailed p-value | Fills | Cumulative Net P&L (USD) | Outcome Tier |",
        "|---|---:|---:|---:|---:|---:|---:|---|",
    ]
    
    tier = "Tier 3: Inconclusive"
    if h1_res["net_t_stat"] > 1.96 and h1_res["net_pnl"] > 0:
        tier = "Tier 1: Confirmed Edge"
    elif h1_res["net_t_stat"] <= 0.0 or h1_res["net_pnl"] <= 0.0:
        tier = "Tier 2: Definitive Falsification"
        
    lines.append(
        f"| **H1 (Full Model, 0.30 pips)** | {h1_res['blocks']} | "
        f"{h1_res['block_mean_net']*10000.0:.2f} | {h1_res['net_t_stat']:+.2f} | "
        f"{h1_res['p_value']:.4e} | {h1_res['fills']} | "
        f"{h1_res['net_pnl']:+.2f} | **{tier}** |"
    )
    lines.extend([
        "",
        f"- **Paired Comparison vs Naive Baseline on Matched Blocks**: t = {h1_res['paired_t_stat']:+.2f} (n = {h1_res['paired_blocks']})",
        "",
        "## 2. Leave-One-Out (LOO) Diagnostic Ablations",
        "",
        "| Model Variant | Blocks | Mean Net (pips) | Student t-stat | Raw p-value | Holm Threshold | Holm Significant | Fills | Net P&L (USD) |",
        "|---|---:|---:|---:|---:|---:|---|---:|---:|",
    ])
    for r in loo_results:
        lines.append(
            f"| {r['description']} | {r['blocks']} | {r['block_mean_net']*10000.0:.2f} | "
            f"{r['net_t_stat']:+.2f} | {r['p_value']:.4e} | {r['holm_threshold']:.4e} | "
            f"{'YES' if r['holm_significant'] else 'NO'} | {r['fills']} | {r['net_pnl']:+.2f} |"
        )
        
    lines.extend([
        "",
        "## 3. Single-Feature Isolation Rules",
        "",
        "| Isolated Feature | Blocks | Mean Net (pips) | Student t-stat | Raw p-value | Holm Threshold | Holm Significant | Fills | Net P&L (USD) |",
        "|---|---:|---:|---:|---:|---:|---|---:|---:|",
    ])
    for r in single_results:
        lines.append(
            f"| {r['description']} | {r['blocks']} | {r['block_mean_net']*10000.0:.2f} | "
            f"{r['net_t_stat']:+.2f} | {r['p_value']:.4e} | {r['holm_threshold']:.4e} | "
            f"{'YES' if r['holm_significant'] else 'NO'} | {r['fills']} | {r['net_pnl']:+.2f} |"
        )

    lines.extend([
        "",
        "## 4. Cost Sensitivity Curve (0.1 to 1.0 pips)",
        "",
        "| Additional Friction (pips) | Fills | Gross P&L (USD) | Net P&L (USD) | Block Net Return (pips) | Net t-stat | Max Drawdown (USD) |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ])
    for r in friction_results:
        lines.append(
            f"| {r['friction_pips']:.1f} pips | {r['fills']} | {r['gross_pnl']:+.2f} | "
            f"{r['net_pnl']:+.2f} | {r['block_mean_net']*10000.0:.2f} | {r['net_t_stat']:+.2f} | "
            f"{r['max_drawdown']:.2f} |"
        )
        
    lines.extend([
        "",
        f"- **Estimated Break-Even Friction (delta*)**: `{breakeven_friction:.1f} pips`" if breakeven_friction else "- **Estimated Break-Even Friction (delta*)**: `< 0.1 pips`",
        ""
    ])

    report_path = pathlib.Path(output_md)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"\nWritten complete experiment summary to {output_md}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run VECTRA empirical experiment suite.")
    parser.add_argument("--data", required=True, help="Path to input tick CSV file")
    parser.add_argument("--config", required=True, help="Path to JSON configuration file")
    parser.add_argument("--output", default="reports/experiment_suite_report.md", help="Path to output markdown report")
    args = parser.parse_args()
    
    run_full_suite(args.data, args.config, args.output)
