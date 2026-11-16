# VECTRA Empirical Experiment Results (Protocol v1.7)

- **Dataset**: `examples/real_eurusd_val.csv`
- **Configuration**: `config/experiment_eurusd.json` (`EURUSD`, `pip_size = 0.0001`, `quote = USD`)
- **CLI Version**: `v0.1.0`

## 1. Primary Hypothesis Evaluation (H1)

| Hypothesis | Blocks (180s) | Mean Net Return (pips) | Net t-stat (vs 0) | Two-Tailed p-val | Gross t-stat (Mid vs 0) | Executed Fills | Cumulative Net P&L (USD) | Outcome Tier |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| **H1 (Full Model, 0.30 pips)** | 26 | -1.08 | -1.00 | 3.1731e-01 | -0.59 | 40 | -15.50 | **Tier 2: Definitive Falsification** |

- **Primary Edge Metric (Net Return After Friction)**: Mean = -1.08 pips, t = -1.00
- **Gross Directional Skill (Signed Mid Move)**: Mean = -0.64 pips, t = -0.59

## 2. Leave-One-Out (LOO) Diagnostic Ablations

| Model Variant | Blocks | Mean Net (pips) | Student t-stat | Raw p-value | Holm Threshold | Holm Significant | Fills | Net P&L (USD) |
|---|---:|---:|---:|---:|---:|---|---:|---:|
| LOO 1: Without Volatility Extension | 49 | -0.63 | -0.98 | 3.2709e-01 | 8.3333e-03 | NO | 50 | -2.45 |
| LOO 2: Without Velocity Deceleration | 21 | -0.20 | -0.16 | 8.7288e-01 | 5.0000e-02 | NO | 28 | -21.90 |
| LOO 3: Without Response Fade | 25 | -1.13 | -1.09 | 2.7571e-01 | 7.1429e-03 | NO | 42 | -14.20 |
| LOO 4: Without Regime Efficiency | 31 | -0.37 | -0.43 | 6.6720e-01 | 2.5000e-02 | NO | 44 | +6.90 |

## 3. Single-Feature Isolation Rules

| Isolated Feature | Blocks | Mean Net (pips) | Student t-stat | Raw p-value | Holm Threshold | Holm Significant | Fills | Net P&L (USD) |
|---|---:|---:|---:|---:|---:|---|---:|---:|
| Single 1: Extension Only (|z| >= 1.8) | 39 | -0.44 | -0.64 | 5.2217e-01 | 1.6667e-02 | NO | 62 | -16.65 |
| Single 2: Deceleration Only (decel >= 0.50) | 51 | -0.44 | -0.76 | 4.4725e-01 | 1.2500e-02 | NO | 71 | +0.95 |
| Single 3: Response Fade Only (fade >= 0.50) | 50 | -1.14 | -1.76 | 7.8408e-02 | 6.2500e-03 | NO | 63 | -16.15 |
| Single 4: Regime Efficiency Only (eff >= 0.60) | 29 | -0.97 | -0.96 | 3.3706e-01 | 1.0000e-02 | NO | 45 | -19.45 |

## 4. Cost Sensitivity Curves (0.1 to 1.0 pips)

| Additional Friction | Monotonic Fixed Net P&L (USD) | Dynamic Re-exec Net P&L (USD) | Dynamic Fills | Dynamic Gross P&L (USD) | Block Net Return (pips) | Block Net t-stat |
|---|---:|---:|---:|---:|---:|---:|
| 0.1 pips | -11.50 | +8.45 | 47 | +10.80 | -0.88 | -0.81 |
| 0.2 pips | -13.50 | +7.00 | 43 | +11.30 | -0.98 | -0.90 |
| 0.3 pips | -15.50 | -15.50 | 40 | -9.50 | -1.08 | -1.00 |
| 0.4 pips | -17.50 | -20.30 | 36 | -13.10 | -1.18 | -1.09 |
| 0.5 pips | -19.50 | -19.40 | 30 | -11.90 | -1.28 | -1.18 |
| 0.6 pips | -21.50 | -22.30 | 30 | -13.30 | -1.38 | -1.27 |
| 0.7 pips | -23.50 | -37.10 | 28 | -27.30 | -1.48 | -1.36 |
| 0.8 pips | -25.50 | -36.50 | 26 | -26.10 | -1.58 | -1.46 |
| 0.9 pips | -27.50 | -17.30 | 26 | -5.60 | -1.68 | -1.55 |
| 1.0 pips | -29.50 | -27.30 | 24 | -15.30 | -1.78 | -1.64 |

- **Monotonic Fixed-Execution Break-Even Friction (delta*)**: `-0.48 pips`
- **Note on Dynamic Re-execution Curve**: Changes in friction interact with pre-trade edge gates and cooldown timers, shifting the executed trade population.
