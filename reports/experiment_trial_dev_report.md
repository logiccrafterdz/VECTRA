# VECTRA Empirical Experiment Results (Protocol v1.7)

- **Dataset**: `examples/real_eurusd_dev.csv`
- **Configuration**: `config/experiment_eurusd.json` (`EURUSD`, `pip_size = 0.0001`, `quote = USD`)
- **CLI Version**: `v0.1.0`

## 1. Primary Hypothesis Evaluation (H1)

| Hypothesis | Blocks (180s) | Mean Net Return (pips) | Net t-stat (vs 0) | Two-Tailed p-val | Gross t-stat (Mid vs 0) | Executed Fills | Cumulative Net P&L (USD) | Outcome Tier |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| **H1 (Full Model, 0.30 pips)** | 51 | +0.93 | +1.09 | 2.7571e-01 | +1.65 | 32 | +19.70 | **Tier 3: Inconclusive** |

- **Primary Edge Metric (Net Return After Friction)**: Mean = +0.93 pips, t = +1.09
- **Gross Directional Skill (Signed Mid Move)**: Mean = +1.42 pips, t = +1.65

## 2. Leave-One-Out (LOO) Diagnostic Ablations

| Model Variant | Blocks | Mean Net (pips) | Student t-stat | Raw p-value | Holm Threshold | Holm Significant | Fills | Net P&L (USD) |
|---|---:|---:|---:|---:|---:|---|---:|---:|
| LOO 1: Without Volatility Extension | 112 | +0.09 | +0.22 | 8.2587e-01 | 1.2500e-02 | NO | 36 | +18.10 |
| LOO 2: Without Velocity Deceleration | 42 | +0.59 | +0.56 | 5.7548e-01 | 8.3333e-03 | NO | 26 | -18.60 |
| LOO 3: Without Response Fade | 52 | +0.24 | +0.28 | 7.7948e-01 | 1.0000e-02 | NO | 38 | -32.20 |
| LOO 4: Without Regime Efficiency | 62 | -0.05 | -0.06 | 9.5216e-01 | 5.0000e-02 | NO | 34 | +17.40 |

## 3. Single-Feature Isolation Rules

| Isolated Feature | Blocks | Mean Net (pips) | Student t-stat | Raw p-value | Holm Threshold | Holm Significant | Fills | Net P&L (USD) |
|---|---:|---:|---:|---:|---:|---|---:|---:|
| Single 1: Extension Only (|z| >= 1.8) | 82 | -0.57 | -1.02 | 3.0773e-01 | 6.2500e-03 | NO | 73 | -11.10 |
| Single 2: Deceleration Only (decel >= 0.50) | 117 | -0.38 | -0.85 | 3.9533e-01 | 7.1429e-03 | NO | 70 | +22.50 |
| Single 3: Response Fade Only (fade >= 0.50) | 114 | +0.04 | +0.10 | 9.2034e-01 | 2.5000e-02 | NO | 40 | -48.50 |
| Single 4: Regime Efficiency Only (eff >= 0.60) | 74 | +0.10 | +0.18 | 8.5715e-01 | 1.6667e-02 | NO | 83 | +12.05 |

## 4. Cost Sensitivity Curves (0.1 to 1.0 pips)

| Additional Friction | Monotonic Fixed Net P&L (USD) | Dynamic Re-exec Net P&L (USD) | Dynamic Fills | Dynamic Gross P&L (USD) | Block Net Return (pips) | Block Net t-stat |
|---|---:|---:|---:|---:|---:|---:|
| 0.1 pips | +22.90 | +14.30 | 44 | +16.50 | +1.13 | +1.32 |
| 0.2 pips | +21.30 | +18.90 | 36 | +22.50 | +1.03 | +1.21 |
| 0.3 pips | +19.70 | +19.70 | 32 | +24.50 | +0.93 | +1.09 |
| 0.4 pips | +18.10 | +5.50 | 30 | +11.50 | +0.83 | +0.97 |
| 0.5 pips | +16.50 | +4.00 | 30 | +11.50 | +0.73 | +0.86 |
| 0.6 pips | +14.90 | +3.50 | 28 | +11.90 | +0.63 | +0.74 |
| 0.7 pips | +13.30 | -7.80 | 24 | +0.60 | +0.53 | +0.62 |
| 0.8 pips | +11.70 | -8.20 | 24 | +1.40 | +0.43 | +0.51 |
| 0.9 pips | +10.10 | -7.70 | 22 | +2.20 | +0.33 | +0.39 |
| 1.0 pips | +8.50 | -7.00 | 20 | +3.00 | +0.23 | +0.27 |

- **Monotonic Fixed-Execution Break-Even Friction (delta*)**: `1.53 pips`
- **Note on Dynamic Re-execution Curve**: Changes in friction interact with pre-trade edge gates and cooldown timers, shifting the executed trade population.
