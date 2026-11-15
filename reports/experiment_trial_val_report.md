# VECTRA Empirical Experiment Results (Protocol v1.2)

- **Dataset**: `examples\real_eurusd_val.csv`
- **Configuration**: `config\experiment_eurusd.json`
- **CLI Version**: `v0.1.0`

## 1. Primary Hypothesis Evaluation (H1)

| Hypothesis | Blocks (180s) | Mean Net Return (pips) | Student t-stat (vs 0) | Two-Tailed p-value | Fills | Cumulative Net P&L (USD) | Outcome Tier |
|---|---:|---:|---:|---:|---:|---:|---|
| **H1 (Full Model, 0.30 pips)** | 26 | -1.07 | -0.99 | 3.2217e-01 | 40 | -15.50 | **Tier 2: Definitive Falsification** |

- **Paired Comparison vs Naive Baseline on Matched Blocks**: t = -0.36 (n = 26)

## 2. Leave-One-Out (LOO) Diagnostic Ablations

| Model Variant | Blocks | Mean Net (pips) | Student t-stat | Raw p-value | Holm Threshold | Holm Significant | Fills | Net P&L (USD) |
|---|---:|---:|---:|---:|---:|---|---:|---:|
| LOO 1: Without Volatility Extension | 49 | -0.64 | -0.99 | 3.2217e-01 | 1.0000e-02 | NO | 50 | -2.45 |
| LOO 2: Without Velocity Deceleration | 21 | -0.20 | -0.16 | 8.7288e-01 | 5.0000e-02 | NO | 28 | -21.90 |
| LOO 3: Without Response Fade | 25 | -1.13 | -1.09 | 2.7571e-01 | 8.3333e-03 | NO | 42 | -14.20 |
| LOO 4: Without Regime Efficiency | 31 | -0.36 | -0.42 | 6.7449e-01 | 2.5000e-02 | NO | 44 | +6.90 |

## 3. Single-Feature Isolation Rules

| Isolated Feature | Blocks | Mean Net (pips) | Student t-stat | Raw p-value | Holm Threshold | Holm Significant | Fills | Net P&L (USD) |
|---|---:|---:|---:|---:|---:|---|---:|---:|
| Single 1: Extension Only (|z| >= 1.8) | 39 | -0.43 | -0.62 | 5.3526e-01 | 1.6667e-02 | NO | 62 | -16.65 |
| Single 2: Deceleration Only (decel >= 0.50) | 51 | -0.44 | -0.75 | 4.5325e-01 | 1.2500e-02 | NO | 71 | +0.95 |
| Single 3: Response Fade Only (fade >= 0.50) | 50 | -1.13 | -1.73 | 8.3630e-02 | 6.2500e-03 | NO | 63 | -16.15 |
| Single 4: Regime Efficiency Only (eff >= 0.60) | 29 | -1.39 | -1.40 | 1.6151e-01 | 7.1429e-03 | NO | 45 | -13.25 |

## 4. Cost Sensitivity Curve (0.1 to 1.0 pips)

| Additional Friction (pips) | Fills | Gross P&L (USD) | Net P&L (USD) | Block Net Return (pips) | Net t-stat | Max Drawdown (USD) |
|---|---:|---:|---:|---:|---:|---:|
| 0.1 pips | 47 | +10.80 | +8.45 | -0.87 | -0.80 | 21.30 |
| 0.2 pips | 43 | +11.30 | +7.00 | -0.97 | -0.89 | 21.70 |
| 0.3 pips | 40 | -9.50 | -15.50 | -1.07 | -0.99 | 27.80 |
| 0.4 pips | 36 | -13.10 | -20.30 | -1.17 | -1.08 | 31.30 |
| 0.5 pips | 30 | -11.90 | -19.40 | -1.27 | -1.17 | 41.60 |
| 0.6 pips | 30 | -13.30 | -22.30 | -1.37 | -1.26 | 43.10 |
| 0.7 pips | 28 | -27.30 | -37.10 | -1.47 | -1.35 | 46.10 |
| 0.8 pips | 26 | -26.10 | -36.50 | -1.57 | -1.45 | 45.30 |
| 0.9 pips | 26 | -5.60 | -17.30 | -1.67 | -1.54 | 32.50 |
| 1.0 pips | 24 | -15.30 | -27.30 | -1.77 | -1.63 | 37.60 |

- **Estimated Break-Even Friction (delta*)**: `0.2 pips`
