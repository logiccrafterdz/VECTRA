# VECTRA Empirical Experiment Results (Protocol v1.4)

- **Dataset**: `examples/sample_ticks_usdjpy.csv`
- **Configuration**: `config/experiment_usdjpy.json` (`USDJPY`, `pip_size = 0.01`, `quote = JPY`)
- **CLI Version**: `v0.1.0`

## 1. Primary Hypothesis Evaluation (H1)

| Hypothesis | Blocks (180s) | Mean Net Return (pips) | Net t-stat (vs 0) | Two-Tailed p-val | Gross t-stat (Mid vs 0) | Executed Fills | Cumulative Net P&L (JPY) | Outcome Tier |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| **H1 (Full Model, 0.30 pips)** | 33 | +231.91 | +13.83 | 1.6802e-43 | +13.92 | 64 | +1650.31 | **Tier 1: Confirmed Edge** |

- **Primary Edge Metric (Net Return After Friction)**: Mean = +231.91 pips, t = +13.83
- **Gross Directional Skill (Signed Mid Move)**: Mean = +233.46 pips, t = +13.92

## 2. Leave-One-Out (LOO) Diagnostic Ablations

| Model Variant | Blocks | Mean Net (pips) | Student t-stat | Raw p-value | Holm Threshold | Holm Significant | Fills | Net P&L (JPY) |
|---|---:|---:|---:|---:|---:|---|---:|---:|
| LOO 1: Without Volatility Extension | 45 | +200.87 | +10.29 | 7.8176e-25 | 8.3333e-03 | YES | 86 | +2078.34 |
| LOO 2: Without Velocity Deceleration | 3 | +176.96 | +1.72 | 8.5432e-02 | 5.0000e-02 | NO | 4 | +117.36 |
| LOO 3: Without Response Fade | 4 | +140.90 | +1.77 | 7.6727e-02 | 2.5000e-02 | NO | 8 | +68.95 |
| LOO 4: Without Regime Efficiency | 35 | +229.92 | +12.97 | 1.8103e-38 | 6.2500e-03 | YES | 66 | +1681.35 |

## 3. Single-Feature Isolation Rules

| Isolated Feature | Blocks | Mean Net (pips) | Student t-stat | Raw p-value | Holm Threshold | Holm Significant | Fills | Net P&L (JPY) |
|---|---:|---:|---:|---:|---:|---|---:|---:|
| Single 1: Extension Only (|z| >= 1.8) | 47 | -226.17 | -12.25 | 1.6800e-34 | 7.1429e-03 | YES | 20 | -270.05 |
| Single 2: Deceleration Only (decel >= 0.50) | 46 | -200.63 | -9.69 | 3.3253e-22 | 1.0000e-02 | YES | 22 | -257.13 |
| Single 3: Response Fade Only (fade >= 0.50) | 9 | +81.33 | +1.93 | 5.3607e-02 | 1.6667e-02 | NO | 13 | +109.00 |
| Single 4: Regime Efficiency Only (eff >= 0.60) | 44 | +204.32 | +9.30 | 1.4045e-20 | 1.2500e-02 | YES | 90 | +1957.19 |

## 4. Cost Sensitivity Curves (0.1 to 1.0 pips)

| Additional Friction | Monotonic Fixed Net P&L (JPY) | Dynamic Re-exec Net P&L (JPY) | Dynamic Fills | Dynamic Gross P&L (JPY) | Block Net Return (pips) | Block Net t-stat |
|---|---:|---:|---:|---:|---:|---:|
| 0.1 pips | +2290.31 | +1635.07 | 64 | +1635.19 | +231.91 | +13.83 |
| 0.2 pips | +1970.31 | +1650.44 | 64 | +1650.69 | +231.91 | +13.83 |
| 0.3 pips | +1650.31 | +1650.31 | 64 | +1650.69 | +231.91 | +13.83 |
| 0.4 pips | +1330.31 | +1650.18 | 64 | +1650.69 | +231.91 | +13.83 |
| 0.5 pips | +1010.31 | +1650.06 | 64 | +1650.69 | +231.91 | +13.83 |
| 0.6 pips | +690.31 | +1649.93 | 64 | +1650.69 | +231.91 | +13.83 |
| 0.7 pips | +370.31 | +1649.80 | 64 | +1650.69 | +231.91 | +13.83 |
| 0.8 pips | +50.31 | +1649.67 | 64 | +1650.69 | +231.90 | +13.83 |
| 0.9 pips | -269.69 | +1649.55 | 64 | +1650.69 | +231.90 | +13.83 |
| 1.0 pips | -589.69 | +1649.42 | 64 | +1650.69 | +231.90 | +13.83 |

- **Monotonic Fixed-Execution Break-Even Friction (delta*)**: `0.82 pips`
- **Note on Dynamic Re-execution Curve**: Changes in friction interact with pre-trade edge gates and cooldown timers, shifting the executed trade population.
