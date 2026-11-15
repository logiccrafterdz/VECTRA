#!/usr/bin/env python3
"""Automated runner for VECTRA Empirical Experiment Suite (Pre-Registration Protocol v1.4).

Executes:
1. Primary Hypothesis H1 on holdout dataset at 0.30 pips baseline friction.
   Evaluates both primary Net Return t-stat (vs 0) and Gross Directional Skill t-stat (vs 0)
   on autocorrelation-robust non-overlapping 180s blocks.
2. Leave-One-Out (LOO) diagnostic ablations (4 modes).
3. Single-feature isolation rules (4 modes).
4. Dual Cost Sensitivity Curves:
   - Monotonic Fixed-Execution Net P&L (passive accounting on baseline trades, linear decrease).
   - Dynamic Re-Execution Net P&L (re-running the full engine with modified friction).
5. Strict Tier Decision Logic:
   - Tier 1: t > 1.96 AND mean_net_return >= 0.15 pips AND Net P&L > 0.
   - Tier 2: t <= 0 OR mean_net_return <= 0 OR Net P&L <= 0.
   - Tier 3: Inconclusive (Gray Zone).
"""

import argparse
import json
import math
import pathlib
import re
import subprocess
import sys
from typing import Dict, List, Optional, Any

CLI_PATH = pathlib.Path("build/Release/vectra.exe")
if not CLI_PATH.exists():
    CLI_PATH = pathlib.Path("build/vectra.exe")


def classify_tier(t_stat: float, mean_pips: float, net_pnl: float) -> str:
    """Classifies outcome into Tier 1, Tier 2, or Tier 3 according to pre-registration v1.4."""
    if t_stat > 1.96 and mean_pips >= 0.15 and net_pnl > 0.0:
        return "Tier 1: Confirmed Edge"
    elif t_stat <= 0.0 or mean_pips <= 0.0 or net_pnl <= 0.0:
        return "Tier 2: Definitive Falsification"
    else:
        return "Tier 3: Inconclusive"


def test_tier_classification() -> None:
    """Unit tests for the decision tier classification logic."""
    # Tier 1: All three conditions met
    assert classify_tier(2.10, 0.20, 100.0) == "Tier 1: Confirmed Edge"
    assert classify_tier(1.97, 0.15, 0.01) == "Tier 1: Confirmed Edge"

    # Tier 2: Falsification on any non-positive metric
    assert classify_tier(-0.50, 0.20, 100.0) == "Tier 2: Definitive Falsification"
    assert classify_tier(0.0, 0.20, 100.0) == "Tier 2: Definitive Falsification"
    assert classify_tier(2.10, -0.05, 100.0) == "Tier 2: Definitive Falsification"
    assert classify_tier(2.10, 0.0, 100.0) == "Tier 2: Definitive Falsification"
    assert classify_tier(2.10, 0.20, -50.0) == "Tier 2: Definitive Falsification"
    assert classify_tier(2.10, 0.20, 0.0) == "Tier 2: Definitive Falsification"

    # Tier 3: Inconclusive / Gray Zone
    # Positive but below statistical significance
    assert classify_tier(1.50, 0.25, 50.0) == "Tier 3: Inconclusive"
    # Statistically significant but below economic edge requirement (0.15 pips)
    assert classify_tier(2.50, 0.10, 50.0) == "Tier 3: Inconclusive"
    assert classify_tier(3.00, 0.149, 10.0) == "Tier 3: Inconclusive"


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
        "block_hit_rate": 0.0,
        "block_mean_gross_mid": 0.0,
        "gross_t_stat": 0.0,
        "block_mean_net": 0.0,
        "net_t_stat": 0.0,
        "digest": ""
    }
    
    for line in out.splitlines():
        line = line.strip()
        if line.startswith("events valid:"):
            m = re.search(r"events valid:\s+(\d+)", line)
            if m:
                res["valid_events"] = int(m.group(1))
        elif "usable signals:" in line:
            m = re.search(r"usable signals:\s+(\d+)", line)
            if m:
                res["usable_signals"] = int(m.group(1))
        elif line.startswith("orders:"):
            m = re.search(r"orders:\s+(\d+)\s+submitted,\s+(\d+)\s+fills", line)
            if m:
                res["orders"] = int(m.group(1))
                res["fills"] = int(m.group(2))
        elif "gross P&L:" in line and "net P&L:" in line:
            m = re.search(r"gross P&L:\s+([+-]?[\d\.]+)\s+net P&L:\s+([+-]?[\d\.]+)", line)
            if m:
                res["gross_pnl"] = float(m.group(1))
                res["net_pnl"] = float(m.group(2))
        elif line.startswith("max drawdown:"):
            m = re.search(r"max drawdown:\s+([+-]?[\d\.]+)", line)
            if m:
                res["max_drawdown"] = float(m.group(1))
        elif line.startswith("digest:"):
            res["digest"] = line.split(":", 1)[1].strip()

    # Capture exact non-overlapping block stats via temporary report
    report_file = pathlib.Path("build/_temp_report.md")
    report_cmd = cmd + ["--report", str(report_file)]
    subprocess.run(report_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
    
    if report_file.exists():
        rpt = report_file.read_text(encoding="utf-8")
        for rline in rpt.splitlines():
            if "| All usable signals |" in rline:
                cols = [c.strip() for c in rline.split("|")[1:-1]]
                if len(cols) >= 7:
                    try:
                        res["blocks"] = int(cols[1])
                        res["block_hit_rate"] = float(cols[2].rstrip("%"))
                        res["block_mean_gross_mid"] = float(cols[3])
                        res["gross_t_stat"] = float(cols[4]) if cols[4] != "n/a" else 0.0
                        res["block_mean_net"] = float(cols[5])
                        res["net_t_stat"] = float(cols[6]) if cols[6] != "n/a" else 0.0
                    except Exception:
                        pass
        report_file.unlink(missing_ok=True)
        
    return res


def normal_p_value_two_tailed(t_stat: float) -> float:
    """Approximate two-tailed p-value for large degrees of freedom."""
    abs_t = abs(t_stat)
    return math.erfc(abs_t / math.sqrt(2.0))


def apply_holm_bonferroni(tests: List[Dict[str, Any]], alpha: float = 0.05) -> List[Dict[str, Any]]:
    """Applies Holm-Bonferroni step-down correction to a list of test results."""
    m = len(tests)
    sorted_indices = sorted(range(m), key=lambda i: tests[i]["p_value"])
    
    for rank, idx in enumerate(sorted_indices, 1):
        tests[idx]["holm_rank"] = rank
        raw_p = tests[idx]["p_value"]
        holm_crit = alpha / (m - rank + 1)
        tests[idx]["holm_threshold"] = holm_crit
        tests[idx]["holm_significant"] = raw_p < holm_crit
    return tests


def run_full_suite(data_path: str, config_path: str, output_md: str) -> None:
    cfg_json = json.loads(pathlib.Path(config_path).read_text(encoding="utf-8"))
    instrument = cfg_json.get("instrument", "EURUSD")
    pip_size = 0.01 if ("jpy" in instrument.lower() or "usdjpy" in config_path.lower()) else 0.0001
    quote_currency = "JPY" if "jpy" in instrument.lower() else "USD"
    order_units = cfg_json.get("risk", {}).get("order_units", 10000.0)

    print(f"=== Running Pre-Registered Experiment Suite v1.4 ===")
    print(f"Data:           {data_path}")
    print(f"Config:         {config_path} (Instrument: {instrument}, pip_size: {pip_size}, quote: {quote_currency})")
    
    # 1. Primary Hypothesis H1: Full Composite Model at 0.30 pips baseline friction
    print("\n--- [1] Primary Test: Full Model (0.30 pips friction) ---")
    h1_res = run_vectra_replay(data_path, config_path, ablation="none", friction_pips=0.30)
    h1_res["p_value"] = normal_p_value_two_tailed(h1_res["net_t_stat"])
    h1_mean_pips = h1_res["block_mean_net"] / pip_size
    h1_gross_pips = h1_res["block_mean_gross_mid"] / pip_size
    print(f"Blocks: {h1_res['blocks']}, Net t-stat: {h1_res['net_t_stat']:+.2f}, Gross t-stat: {h1_res['gross_t_stat']:+.2f}, "
          f"Mean Net: {h1_mean_pips:.2f} pips, Net P&L: {h1_res['net_pnl']:+.2f} {quote_currency}")

    # 2. Leave-One-Out (LOO) Diagnostic Ablations
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
        r["mean_pips"] = r["block_mean_net"] / pip_size
        loo_results.append(r)
        print(f"  {desc[:35]}: blocks={r['blocks']}, net_t={r['net_t_stat']:+.2f}, Net P&L={r['net_pnl']:+.2f} {quote_currency}")

    # 3. Single-Feature Isolation Rules
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
        r["mean_pips"] = r["block_mean_net"] / pip_size
        single_results.append(r)
        print(f"  {desc[:35]}: blocks={r['blocks']}, net_t={r['net_t_stat']:+.2f}, Net P&L={r['net_pnl']:+.2f} {quote_currency}")

    # Apply Holm-Bonferroni across the 8 secondary exploratory tests for this instrument
    secondary_family = loo_results + single_results
    apply_holm_bonferroni(secondary_family, alpha=0.05)

    # 4. Friction Sensitivity Curve (Monotonic Fixed-Execution vs. Dynamic Re-execution)
    print("\n--- [4] Friction Sensitivity Curves (0.1 to 1.0 pips) ---")
    friction_levels = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0]
    base_net_pnl = h1_res["net_pnl"]
    base_fills = h1_res["fills"]
    base_trades = base_fills / 2.0  # entry and exit legs
    cost_per_pip_total = pip_size * order_units * base_trades

    friction_results = []
    for f in friction_levels:
        # Strictly monotonic fixed-execution accounting:
        fixed_net = base_net_pnl - (f - 0.30) * cost_per_pip_total
        r = run_vectra_replay(data_path, config_path, ablation="none", friction_pips=f)
        r["fixed_net_pnl"] = fixed_net
        r["mean_pips"] = r["block_mean_net"] / pip_size
        friction_results.append(r)
        print(f"  Friction {f:.1f} pips: Fixed Net={fixed_net:+.2f} {quote_currency} | Dynamic Net={r['net_pnl']:+.2f} {quote_currency} (Fills={r['fills']})")

    # Fixed-Execution Break-Even Friction (exact algebraic closed form)
    if cost_per_pip_total > 0:
        fixed_breakeven = 0.30 + (base_net_pnl / cost_per_pip_total)
    else:
        fixed_breakeven = 0.0

    # Decision Tier Logic
    tier = classify_tier(h1_res["net_t_stat"], h1_mean_pips, h1_res["net_pnl"])

    # Generate Markdown Report
    lines = [
        "# VECTRA Empirical Experiment Results (Protocol v1.4)",
        "",
        f"- **Dataset**: `{data_path}`",
        f"- **Configuration**: `{config_path}` (`{instrument}`, `pip_size = {pip_size}`, `quote = {quote_currency}`)",
        f"- **CLI Version**: `v0.1.0`",
        "",
        "## 1. Primary Hypothesis Evaluation (H1)",
        "",
        f"| Hypothesis | Blocks (180s) | Mean Net Return (pips) | Net t-stat (vs 0) | Two-Tailed p-val | Gross t-stat (Mid vs 0) | Executed Fills | Cumulative Net P&L ({quote_currency}) | Outcome Tier |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---|",
        f"| **H1 (Full Model, 0.30 pips)** | {h1_res['blocks']} | "
        f"{h1_mean_pips:+.2f} | {h1_res['net_t_stat']:+.2f} | "
        f"{h1_res['p_value']:.4e} | {h1_res['gross_t_stat']:+.2f} | "
        f"{h1_res['fills']} | {h1_res['net_pnl']:+.2f} | **{tier}** |",
        "",
        f"- **Primary Edge Metric (Net Return After Friction)**: Mean = {h1_mean_pips:+.2f} pips, t = {h1_res['net_t_stat']:+.2f}",
        f"- **Gross Directional Skill (Signed Mid Move)**: Mean = {h1_gross_pips:+.2f} pips, t = {h1_res['gross_t_stat']:+.2f}",
        "",
        "## 2. Leave-One-Out (LOO) Diagnostic Ablations",
        "",
        f"| Model Variant | Blocks | Mean Net (pips) | Student t-stat | Raw p-value | Holm Threshold | Holm Significant | Fills | Net P&L ({quote_currency}) |",
        "|---|---:|---:|---:|---:|---:|---|---:|---:|",
    ]
    for r in loo_results:
        lines.append(
            f"| {r['description']} | {r['blocks']} | {r['mean_pips']:+.2f} | "
            f"{r['net_t_stat']:+.2f} | {r['p_value']:.4e} | {r['holm_threshold']:.4e} | "
            f"{'YES' if r['holm_significant'] else 'NO'} | {r['fills']} | {r['net_pnl']:+.2f} |"
        )
        
    lines.extend([
        "",
        "## 3. Single-Feature Isolation Rules",
        "",
        f"| Isolated Feature | Blocks | Mean Net (pips) | Student t-stat | Raw p-value | Holm Threshold | Holm Significant | Fills | Net P&L ({quote_currency}) |",
        "|---|---:|---:|---:|---:|---:|---|---:|---:|",
    ])
    for r in single_results:
        lines.append(
            f"| {r['description']} | {r['blocks']} | {r['mean_pips']:+.2f} | "
            f"{r['net_t_stat']:+.2f} | {r['p_value']:.4e} | {r['holm_threshold']:.4e} | "
            f"{'YES' if r['holm_significant'] else 'NO'} | {r['fills']} | {r['net_pnl']:+.2f} |"
        )

    lines.extend([
        "",
        "## 4. Cost Sensitivity Curves (0.1 to 1.0 pips)",
        "",
        f"| Additional Friction | Monotonic Fixed Net P&L ({quote_currency}) | Dynamic Re-exec Net P&L ({quote_currency}) | Dynamic Fills | Dynamic Gross P&L ({quote_currency}) | Block Net Return (pips) | Block Net t-stat |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ])
    for r in friction_results:
        lines.append(
            f"| {r['friction_pips']:.1f} pips | {r['fixed_net_pnl']:+.2f} | {r['net_pnl']:+.2f} | "
            f"{r['fills']} | {r['gross_pnl']:+.2f} | {r['mean_pips']:+.2f} | {r['net_t_stat']:+.2f} |"
        )
        
    lines.extend([
        "",
        f"- **Monotonic Fixed-Execution Break-Even Friction (delta*)**: `{fixed_breakeven:.2f} pips`",
        "- **Note on Dynamic Re-execution Curve**: Changes in friction interact with pre-trade edge gates and cooldown timers, shifting the executed trade population.",
        ""
    ])

    report_path = pathlib.Path(output_md)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"\nWritten complete experiment summary to {output_md}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run VECTRA empirical experiment suite.")
    parser.add_argument("--data", help="Path to input tick CSV file")
    parser.add_argument("--config", help="Path to JSON configuration file")
    parser.add_argument("--output", default="reports/experiment_suite_report.md", help="Path to output markdown report")
    parser.add_argument("--test", action="store_true", help="Run self-tests and exit")
    args = parser.parse_args()
    
    if args.test:
        test_tier_classification()
        print("All Python decision tier classification unit tests passed successfully!")
        sys.exit(0)

    if not args.data or not args.config:
        parser.error("--data and --config are required unless --test is specified.")

    run_full_suite(args.data, args.config, args.output)
