# VECTRA Large-Scale Empirical Pre-Registration & Falsification Protocol

This pre-registration locks the experimental design, hypotheses, baseline configurations, datasets, single-feature isolation tests, and cost sensitivity thresholds **prior** to running on newly downloaded out-of-sample tick streams.

---

## 1. Research Hypothesis and Baseline Configuration Lock

### 1.1 Core Hypothesis
Measurable microstructural exhaustion features (volatility-normalized extension, deceleration of velocity across horizons, and impulse response fade), combined with block path efficiency, contain causal predictive information for short-horizon directional movement (180 s) after deducting realistic round-trip costs and execution latency.

### 1.2 Baseline Configuration Lock
The baseline configuration is locked to [`config/real_eurusd.json`](../config/real_eurusd.json):
- **Observation Horizons**: Short = 120 s, Mid = 240 s, Long = 720 s (anchored to `source_time_ns`).
- **Hold Horizon**: 180 s wall-clock.
- **Entry Cooldown**: 180 s wall-clock.
- **Feature Weights**: Extension = 0.30, Deceleration = 0.25, Response Fade = 0.35, Quality = 0.10.
- **Decision Filters**: `min_continuation_efficiency` = 0.55, `continuation_move_scale` = 1.0, `reversal_move_scale` = 0.45.
- **Round-Trip Cost Gate**: `spread + 2 * (commission + slippage) + min_expected_edge` (with `min_expected_edge` = 1.0 pip).

---

## 2. Pre-Registered Success and Falsification (Failure) Criteria

To prevent p-hacking, overfitting, and data snooping, evaluation criteria are strictly pre-registered:

### 2.1 Criteria for Success (Confirmed Edge)
A feature or strategy configuration is considered to demonstrate a real predictive edge if and only if **all three** conditions are met on out-of-sample data:
1. **Statistical Significance on Non-Overlapping Blocks**:
   $$\text{Hit Rate}_{\text{spread\_crossed}} > \text{Hit Rate}_{\text{naive\_baseline}} \quad \text{with} \quad t > 2.0 \quad (p < 0.05)$$
   on strictly independent, non-overlapping 180-second horizon blocks.
2. **Economic Viability Under Costs**:
   $$\text{Net P&L} > 0 \quad \text{after all commissions, slippage, and spread}$$
   with an average net gain $\ge 0.2\text{ pips}$ per filled order.
3. **Cross-Instrument Robustness**:
   The directional signal direction must replicate across at least two independent major currency pairs without re-tuning parameters.

### 2.2 Criteria for Falsification (Failure / Null Result)
The exhaustion hypothesis is formally **falsified** if any of the following occur:
1. **Feature Inefficacy in Isolation**: No single feature ($z$-score, deceleration, fade, regime efficiency) beats the naive baseline on independent non-overlapping blocks at $t > 1.96$.
2. **Cost Asphyxiation**: Gross profits are entirely consumed by round-trip transaction costs, yielding Net P&L $\le 0$ under realistic spreads ($\ge 0.3\text{ pips}$).
3. **Horizon Decay / Random Walk**: Directional hit rates on untouched multi-month holdouts converge to $50.0\% \pm 1.0\%$ across non-overlapping blocks.

---

## 3. Untouched Multi-Month Dataset Plan (Dukascopy)

Data will be ingested once and only once across 3 to 6 months of historical institutional quotes:
- **Instruments**:
  1. `EURUSD` (Primary benchmark)
  2. `GBPUSD` (High-volatility cable benchmark)
  3. `USDJPY` (Cross-session Asian/US regime benchmark)
- **Period**: 3 to 6 continuous months of tick data.
- **Protocol**: Single evaluation run per instrument. Zero hyperparameter tuning on holdouts.

---

## 4. Single-Feature Ablation and Isolation Suite

Each feature is evaluated in strict isolation to test whether it carries independent predictive content:

| Test Run | Active Feature | Other Features Disabled | Baseline Comparison |
|---|---|---|---|
| **Ablation 1** | Volatility Extension ($z$-score) | Decel = 0, Fade = 0, Efficiency = 0 | Naive Directional Baseline |
| **Ablation 2** | Velocity Deceleration | Extension = 0, Fade = 0, Efficiency = 0 | Naive Directional Baseline |
| **Ablation 3** | Response Fade | Extension = 0, Decel = 0, Efficiency = 0 | Naive Directional Baseline |
| **Ablation 4** | Block Regime Efficiency | Extension = 0, Decel = 0, Fade = 0 | Naive Directional Baseline |
| **Full Model** | Composite Exhaustion + Efficiency | All Active (Locked Config) | Naive Directional Baseline |

**Rule of Inference**: If none of Ablations 1–4 demonstrate $t > 1.96$ against the naive baseline on non-overlapping blocks, any profitability in the full model is considered a fitting artifact or variance.

---

## 5. Cost Sensitivity Curve Protocol

To determine the exact economic break-even boundary, the execution engine is evaluated across a 10-point friction grid:

$$\text{Friction Grid} = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0]\text{ pips}$$

For each point, we report:
- Total Orders Filled
- Win Rate (Gross vs. Net)
- Realized Net P&L
- Break-Even Friction Point ($\text{Friction}^*$ where Net P&L = 0)
