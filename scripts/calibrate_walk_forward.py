#!/usr/bin/env python3
"""
Walk-Forward Calibration Script for VECTRA (EUR/USD Real Tick Data).

Strict Split Discipline:
  1. Evaluates candidate threshold configurations on DEV and VAL splits ONLY.
  2. Selects the configuration that maximizes out-of-development consistency:
     requires positive Net P&L on BOTH DEV and VAL, then maximizes
     min(Net_DEV, Net_VAL) + 0.5 * (Net_DEV + Net_VAL).
  3. Writes the winning configuration to config/real_eurusd.json and runs
     a single locked evaluation on OOS (examples/real_eurusd_oos.csv).
"""
import copy
import json
import os
import re
import subprocess
import sys
import tempfile

BASE_CONFIG_PATH = os.path.join("config", "real_eurusd.json")
VECTRA_BIN = os.path.join("build", "Release", "vectra.exe")
DEV_CSV = os.path.join("examples", "real_eurusd_dev.csv")
VAL_CSV = os.path.join("examples", "real_eurusd_val.csv")
OOS_CSV = os.path.join("examples", "real_eurusd_oos.csv")


def run_replay(cfg_dict, data_path, report_path=None):
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as tf:
        json.dump(cfg_dict, tf, indent=2)
        tmp_cfg = tf.name

    if report_path is None:
        rep_fd, rep_path = tempfile.mkstemp(suffix=".md")
        os.close(rep_fd)
        cleanup_rep = True
    else:
        rep_path = report_path
        cleanup_rep = False

    try:
        proc = subprocess.run(
            [VECTRA_BIN, "--config", tmp_cfg, "--data", data_path, "--report", rep_path],
            capture_output=True,
            text=True,
            check=True,
        )
        out = proc.stdout
        gross_m = re.search(r"gross P&L:\s*([-0-9.]+)", out)
        net_m = re.search(r"net P&L:\s*([-0-9.]+)", out)
        dd_m = re.search(r"max drawdown:\s*([-0-9.]+)", out)
        fills_m = re.search(r"orders:\s*\d+\s+submitted,\s*(\d+)\s+fills", out)
        sig_m = re.search(r"usable signals:\s*(\d+)", out)

        with open(rep_path, "r", encoding="utf-8") as rf:
            rep_text = rf.read()

        cont_m = re.search(
            r"\|\s*Continuation\s*\|\s*(\d+)\s*\|\s*([0-9.]+)%\s*\|\s*([-0-9.]+)\s*\|\s*([-0-9.]+)\s*\|",
            rep_text,
        )
        rev_m = re.search(
            r"\|\s*Reversal\s*\|\s*(\d+)\s*\|\s*([0-9.]+)%\s*\|\s*([-0-9.]+)\s*\|\s*([-0-9.]+)\s*\|",
            rep_text,
        )

        return {
            "gross_pnl": float(gross_m.group(1)) if gross_m else 0.0,
            "net_pnl": float(net_m.group(1)) if net_m else 0.0,
            "max_dd": float(dd_m.group(1)) if dd_m else 0.0,
            "fills": int(fills_m.group(1)) if fills_m else 0,
            "signals": int(sig_m.group(1)) if sig_m else 0,
            "cont_n": int(cont_m.group(1)) if cont_m else 0,
            "cont_hit": float(cont_m.group(2)) if cont_m else 0.0,
            "cont_move": float(cont_m.group(4)) if cont_m else 0.0,
            "rev_n": int(rev_m.group(1)) if rev_m else 0,
            "rev_hit": float(rev_m.group(2)) if rev_m else 0.0,
            "rev_move": float(rev_m.group(4)) if rev_m else 0.0,
        }
    finally:
        if os.path.exists(tmp_cfg):
            os.remove(tmp_cfg)
        if cleanup_rep and os.path.exists(rep_path):
            os.remove(rep_path)


def main():
    with open(BASE_CONFIG_PATH, "r", encoding="utf-8") as f:
        base_cfg = json.load(f)

    candidates = []
    for min_eff in [0.50, 0.55]:
        for max_rev_eff in [0.45, 0.55, 1.0]:
            for min_edge in [0.00010, 0.00012, 0.00014]:
                candidates.append({
                    "min_continuation_efficiency": min_eff,
                    "max_reversal_efficiency": max_rev_eff,
                    "reversal_exhaustion_threshold": 0.65,
                    "continuation_exhaustion_threshold": 0.35,
                    "max_persistence_for_reversal": 0.20,
                    "min_expected_edge": min_edge,
                })

    print(f"Evaluating {len(candidates)} candidate parameter sets on DEV and VAL (OOS untouched)...", flush=True)
    best_score = -1e9
    best_params = None
    best_dev = None
    best_val = None

    for idx, params in enumerate(candidates, 1):
        cfg = copy.deepcopy(base_cfg)
        cfg["backtest"]["retain_tick_records"] = False
        cfg["prediction"]["min_continuation_efficiency"] = params["min_continuation_efficiency"]
        cfg["prediction"]["max_reversal_efficiency"] = params["max_reversal_efficiency"]
        cfg["prediction"]["reversal_exhaustion_threshold"] = params["reversal_exhaustion_threshold"]
        cfg["prediction"]["continuation_exhaustion_threshold"] = params["continuation_exhaustion_threshold"]
        cfg["prediction"]["max_persistence_for_reversal"] = params["max_persistence_for_reversal"]
        cfg["risk"]["min_expected_edge"] = params["min_expected_edge"]

        dev_res = run_replay(cfg, DEV_CSV)
        if dev_res["net_pnl"] <= 0.0 or dev_res["fills"] < 6:
            continue

        val_res = run_replay(cfg, VAL_CSV)
        if val_res["net_pnl"] <= 0.0 or val_res["fills"] < 6:
            continue

        robust_score = (
            min(dev_res["net_pnl"], val_res["net_pnl"])
            + 0.25 * (dev_res["net_pnl"] + val_res["net_pnl"])
            - 0.15 * max(dev_res["max_dd"], val_res["max_dd"])
        )
        if robust_score > best_score:
            best_score = robust_score
            best_params = params
            best_dev = dev_res
            best_val = val_res
            print(
                f"  [New Best #{idx}] score={robust_score:.2f} | "
                f"DEV net={dev_res['net_pnl']:+.2f} (DD={dev_res['max_dd']:.1f}, fills={dev_res['fills']}, "
                f"cont={dev_res['cont_hit']:.1f}%, rev={dev_res['rev_hit']:.1f}%[{dev_res['rev_n']}]) | "
                f"VAL net={val_res['net_pnl']:+.2f} (DD={val_res['max_dd']:.1f}, fills={val_res['fills']}, "
                f"cont={val_res['cont_hit']:.1f}%, rev={val_res['rev_hit']:.1f}%[{val_res['rev_n']}]) | "
                f"params={params}"
            )

    if best_params is None:
        print("No candidate achieved positive Net P&L on both DEV and VAL.")
        sys.exit(1)

    print("\n=== Locked Best Walk-Forward Parameters (DEV + VAL) ===")
    print(json.dumps(best_params, indent=2))

    final_cfg = copy.deepcopy(base_cfg)
    final_cfg["prediction"]["min_continuation_efficiency"] = best_params["min_continuation_efficiency"]
    final_cfg["prediction"]["max_reversal_efficiency"] = best_params["max_reversal_efficiency"]
    final_cfg["prediction"]["reversal_exhaustion_threshold"] = best_params["reversal_exhaustion_threshold"]
    final_cfg["prediction"]["continuation_exhaustion_threshold"] = best_params["continuation_exhaustion_threshold"]
    final_cfg["prediction"]["max_persistence_for_reversal"] = best_params["max_persistence_for_reversal"]
    final_cfg["risk"]["min_expected_edge"] = best_params["min_expected_edge"]

    with open(BASE_CONFIG_PATH, "w", encoding="utf-8") as f:
        json.dump(final_cfg, f, indent=2)
        f.write("\n")

    dev_final = run_replay(final_cfg, DEV_CSV, os.path.join("reports", "real_eurusd_dev_report.md"))
    val_final = run_replay(final_cfg, VAL_CSV, os.path.join("reports", "real_eurusd_val_report.md"))
    oos_final = run_replay(final_cfg, OOS_CSV, os.path.join("reports", "real_eurusd_oos_report.md"))
    run_replay(final_cfg, os.path.join("examples", "real_eurusd_ticks.csv"), os.path.join("reports", "real_data_test_report.md"))

    print("\n=== Final 3-Split Summary (DEV / VAL / OOS) ===")
    for label, res in [("DEV", dev_final), ("VAL", val_final), ("OOS", oos_final)]:
        print(
            f"  {label}: Gross={res['gross_pnl']:+.2f}, Net={res['net_pnl']:+.2f}, "
            f"MaxDD={res['max_dd']:.2f}, Fills={res['fills']}, "
            f"ContHit={res['cont_hit']:.1f}% ({res['cont_n']}), "
            f"RevHit={res['rev_hit']:.1f}% ({res['rev_n']})"
        )


if __name__ == "__main__":
    main()
