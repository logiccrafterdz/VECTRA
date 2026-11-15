# VECTRA Empirical Experiment Results (Protocol v1.2)

- **Dataset**: `examples\real_eurusd_dev.csv`
- **Configuration**: `config\experiment_eurusd.json`
- **CLI Version**: `v0.1.0`

## 1. Primary Hypothesis Evaluation (H1)

| Hypothesis | Blocks (180s) | Mean Net Return (pips) | Student t-stat (vs 0) | Two-Tailed p-value | Fills | Cumulative Net P&L (USD) | Outcome Tier |
|---|---:|---:|---:|---:|---:|---:|---|
| **H1 (Full Model, 0.30 pips)** | 51 | 0.95 | +1.11 | 2.6700e-01 | 32 | +19.70 | **Tier 3: Inconclusive** |

- **Paired Comparison vs Naive Baseline on Matched Blocks**: t = +1.33 (n = 51)

## 2. Leave-One-Out (LOO) Diagnostic Ablations

| Model Variant | Blocks | Mean Net (pips) | Student t-stat | Raw p-value | Holm Threshold | Holm Significant | Fills | Net P&L (USD) |
|---|---:|---:|---:|---:|---:|---|---:|---:|
| LOO 1: Without Volatility Extension | 112 | 0.10 | +0.25 | 8.0259e-01 | 1.6667e-02 | NO | 36 | +18.10 |
| LOO 2: Without Velocity Deceleration | 42 | 0.63 | +0.60 | 5.4851e-01 | 8.3333e-03 | NO | 26 | -18.60 |
| LOO 3: Without Response Fade | 52 | 0.28 | +0.32 | 7.4897e-01 | 1.0000e-02 | NO | 38 | -32.20 |
| LOO 4: Without Regime Efficiency | 62 | -0.05 | -0.06 | 9.5216e-01 | 5.0000e-02 | NO | 34 | +17.40 |

## 3. Single-Feature Isolation Rules

| Isolated Feature | Blocks | Mean Net (pips) | Student t-stat | Raw p-value | Holm Threshold | Holm Significant | Fills | Net P&L (USD) |
|---|---:|---:|---:|---:|---:|---|---:|---:|
| Single 1: Extension Only (|z| >= 1.8) | 82 | -0.57 | -1.03 | 3.0301e-01 | 6.2500e-03 | NO | 73 | -11.10 |
| Single 2: Deceleration Only (decel >= 0.50) | 117 | -0.39 | -0.87 | 3.8430e-01 | 7.1429e-03 | NO | 70 | +22.50 |
| Single 3: Response Fade Only (fade >= 0.50) | 114 | 0.06 | +0.12 | 9.0448e-01 | 2.5000e-02 | NO | 40 | -48.50 |
| Single 4: Regime Efficiency Only (eff >= 0.60) | 74 | 0.18 | +0.31 | 7.5656e-01 | 1.2500e-02 | NO | 83 | +12.85 |

## 4. Cost Sensitivity Curve (0.1 to 1.0 pips)

| Additional Friction (pips) | Fills | Gross P&L (USD) | Net P&L (USD) | Block Net Return (pips) | Net t-stat | Max Drawdown (USD) |
|---|---:|---:|---:|---:|---:|---:|
| 0.1 pips | 44 | +16.50 | +14.30 | 1.15 | +1.34 | 18.50 |
| 0.2 pips | 36 | +22.50 | +18.90 | 1.05 | +1.22 | 16.10 |
| 0.3 pips | 32 | +24.50 | +19.70 | 0.95 | +1.11 | 16.20 |
| 0.4 pips | 30 | +11.50 | +5.50 | 0.85 | +0.99 | 19.70 |
| 0.5 pips | 30 | +11.50 | +4.00 | 0.75 | +0.87 | 20.10 |
| 0.6 pips | 28 | +11.90 | +3.50 | 0.65 | +0.76 | 21.30 |
| 0.7 pips | 24 | +0.60 | -7.80 | 0.55 | +0.64 | 28.60 |
| 0.8 pips | 24 | +1.40 | -8.20 | 0.45 | +0.52 | 28.60 |
| 0.9 pips | 22 | +2.20 | -7.70 | 0.35 | +0.41 | 26.60 |
| 1.0 pips | 20 | +3.00 | -7.00 | 0.25 | +0.29 | 26.60 |

- **Estimated Break-Even Friction (delta*)**: `0.6 pips`
