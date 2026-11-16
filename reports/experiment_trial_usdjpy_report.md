# VECTRA Empirical Experiment Results (Protocol v1.7)

- **Dataset**: `examples/sample_ticks_usdjpy.csv`
- **Configuration**: `config/experiment_usdjpy.json` (`USDJPY`, `pip_size = 0.01`, `quote = JPY`)
- **CLI Version**: `v0.1.0`

## 1. Primary Hypothesis Evaluation (H1)

| Hypothesis | Blocks (180s) | Mean Net Return (pips) | Net t-stat (vs 0) | Two-Tailed p-val | Gross t-stat (Mid vs 0) | Executed Fills | Cumulative Net P&L (JPY) | Outcome Tier |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| **H1 (Full Model, 0.30 pips)** | 33 | +231.61 | +13.82 | 1.9307e-43 | +13.92 | 64 | +40790.00 | **Tier 1: Confirmed Edge** |

- **Primary Edge Metric (Net Return After Friction)**: Mean = +231.61 pips, t = +13.82
- **Gross Directional Skill (Signed Mid Move)**: Mean = +233.46 pips, t = +13.92

## 2. Leave-One-Out (LOO) Diagnostic Ablations

| Model Variant | Blocks | Mean Net (pips) | Student t-stat | Raw p-value | Holm Threshold | Holm Significant | Fills | Net P&L (JPY) |
|---|---:|---:|---:|---:|---:|---|---:|---:|
| LOO 1: Without Volatility Extension | 45 | +200.57 | +10.27 | 9.6205e-25 | 8.3333e-03 | YES | 86 | +50140.00 |
| LOO 2: Without Velocity Deceleration | 3 | +176.67 | +1.72 | 8.5432e-02 | 5.0000e-02 | NO | 4 | +2930.00 |
| LOO 3: Without Response Fade | 4 | +140.60 | +1.77 | 7.6727e-02 | 2.5000e-02 | NO | 8 | +1510.00 |
| LOO 4: Without Regime Efficiency | 35 | +229.62 | +12.95 | 2.3495e-38 | 6.2500e-03 | YES | 66 | +41820.00 |

## 3. Single-Feature Isolation Rules

| Isolated Feature | Blocks | Mean Net (pips) | Student t-stat | Raw p-value | Holm Threshold | Holm Significant | Fills | Net P&L (JPY) |
|---|---:|---:|---:|---:|---:|---|---:|---:|
| Single 1: Extension Only (|z| >= 1.8) | 47 | -226.47 | -12.26 | 1.4850e-34 | 7.1429e-03 | YES | 56 | -25370.00 |
| Single 2: Deceleration Only (decel >= 0.50) | 46 | -200.92 | -9.70 | 3.0150e-22 | 1.0000e-02 | YES | 58 | -26040.00 |
| Single 3: Response Fade Only (fade >= 0.50) | 9 | +81.03 | +1.92 | 5.4858e-02 | 1.6667e-02 | NO | 12 | +390.00 |
| Single 4: Regime Efficiency Only (eff >= 0.60) | 44 | +204.02 | +9.29 | 1.5429e-20 | 1.2500e-02 | YES | 89 | +48610.00 |

## 4. Cost Sensitivity Curves (0.1 to 1.0 pips)

| Additional Friction | Monotonic Fixed Net P&L (JPY) | Dynamic Re-exec Net P&L (JPY) | Dynamic Fills | Dynamic Gross P&L (JPY) | Block Net Return (pips) | Block Net t-stat |
|---|---:|---:|---:|---:|---:|---:|
| 0.1 pips | +41430.00 | +40900.00 | 64 | +41220.00 | +231.81 | +13.83 |
| 0.2 pips | +41110.00 | +41020.00 | 64 | +41660.00 | +231.71 | +13.82 |
| 0.3 pips | +40790.00 | +40790.00 | 64 | +41750.00 | +231.61 | +13.82 |
| 0.4 pips | +40470.00 | +40360.00 | 64 | +41640.00 | +231.51 | +13.81 |
| 0.5 pips | +40150.00 | +40180.00 | 64 | +41780.00 | +231.41 | +13.80 |
| 0.6 pips | +39830.00 | +39820.00 | 64 | +41740.00 | +231.31 | +13.80 |
| 0.7 pips | +39510.00 | +39680.00 | 64 | +41920.00 | +231.21 | +13.79 |
| 0.8 pips | +39190.00 | +39450.00 | 64 | +42010.00 | +231.11 | +13.79 |
| 0.9 pips | +38870.00 | +39130.00 | 64 | +42010.00 | +231.01 | +13.78 |
| 1.0 pips | +38550.00 | +38810.00 | 64 | +42010.00 | +230.91 | +13.77 |

- **Monotonic Fixed-Execution Break-Even Friction (delta*)**: `13.05 pips`
- **Note on Dynamic Re-execution Curve**: Changes in friction interact with pre-trade edge gates and cooldown timers, shifting the executed trade population.
