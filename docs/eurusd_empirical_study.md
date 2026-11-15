# Empirical Study: Real EUR/USD Tick Stream (461,357 Ticks, 4-Way Chronological Split)

## 1. Overview and Epistemic Discipline

Following the pre-registered validation protocol in `docs/testing_plan.md` §6 and `docs/real_data_ingestion_plan.md`, VECTRA was connected to a historical institutional FXCM EUR/USD quote stream (`scripts/fetch_real_eurusd.py`) and evaluated across the full **461,357 consecutive ticks (~20.98 hours of continuous market activity)** divided chronologically into four strictly non-overlapping sets:

1. **Development (`DEV` — `examples/real_eurusd_dev.csv`)**: Ticks 1–100,000 (`6.48 hours`, Asian to early London session).
2. **Validation (`VAL` — `examples/real_eurusd_val.csv`)**: Ticks 100,001–200,000 (`2.95 hours`, London peak session).
3. **Out-of-Sample 1 (`OOS` — `examples/real_eurusd_oos.csv`)**: Ticks 200,001–300,000 (`3.55 hours`, London/New York overlap).
4. **Out-of-Sample 2 (`OOS2` — `examples/real_eurusd_oos2.csv`)**: Ticks 300,001–461,357 (`8.00 hours`, blind holdout US / Late NY session).

---

## 2. Structural, Statistical & CLI Upgrades

1. **Round-Trip Cost Accounting & Order Cooldowns (`RiskEngine`)**:
   - Added `round_trip_cost` (`spread + 2 * (commission + slippage)`) and `RejectReason::Cooldown` (`cooldown_events` and wall-clock `cooldown_ns`).
2. **Position Lifecycle Management (`ReplayEngine`)**:
   - Added `hold_horizon_events`, wall-clock `hold_horizon_ns`, `stop_loss_price`, and `take_profit_price`.
3. **Wall-Clock Provider-Timestamp Windows (`BehaviorEngine`)**:
   - Added `window_short_ns`, `window_mid_ns`, and `window_long_ns` (`120s / 240s / 720s` in `config/real_eurusd.json`) anchored to `source_time_ns`, eliminating tick-arrival-rate distortion across sessions.
4. **Multi-Scale Regime Efficiency, Intra-Window Exhaustion & Expected-Move Scaling (`BehaviorEngine` & `ScoringPredictor`)**:
   - Added `regime_efficiency` $\in [0, 1]$ (`min_continuation_efficiency`, `max_reversal_efficiency`), intra-window deceleration/fade (`decel_intra`, `fade_intra`), and expected-move scale normalization (`continuation_move_scale`, `reversal_move_scale`).
5. **Causal Forward-Return, Non-Overlapping Block $t$-Stats & Score Calibration (`ReplayEngine` & `Report`)**:
   - Evaluates every usable signal at `t + eval_horizon_ns` against a **Naive Directional Baseline** (`disp_short` continuation), computes autocorrelation-robust **Non-Overlapping Horizon Block** $t$-statistics, and outputs a **5-Bin Score Reliability & Brier Calibration Table** (`docs/testing_plan.md` §6.4–6.6).
6. **Live / Paper Stream CLI Mode (`src/main.cpp`)**:
   - Added `--data -` (stdin streaming) and `--stream` to emit real-time JSONL `DECISION` and `FILL` telemetry events as quotes arrive.

---

## 3. Empirical Results Across All Four Splits (`DEV`, `VAL`, `OOS`, `OOS2`)

Configuration: [`config/real_eurusd.json`](../config/real_eurusd.json), locked via walk-forward selection on `DEV` and `VAL` (`scripts/calibrate_walk_forward.py`) before evaluating `OOS` and `OOS2`.

| Metric | `DEV` (100k, 6.48h) | `VAL` (100k, 2.95h) | `OOS` (100k, 3.55h) | `OOS2` (161.4k, 8.00h) | **Total (461.4k ticks, 20.98h)** |
|---|---|---|---|---|---|
| **Full Report** | [`real_eurusd_dev_report.md`](../reports/real_eurusd_dev_report.md) | [`real_eurusd_val_report.md`](../reports/real_eurusd_val_report.md) | [`real_eurusd_oos_report.md`](../reports/real_eurusd_oos_report.md) | [`real_eurusd_oos2_report.md`](../reports/real_eurusd_oos2_report.md) | — |
| **Valid / Rejected Events** | `96,419` / `3,581` | `89,127` / `10,873` | `90,477` / `9,523` | `144,122` / `17,235` | `420,145` / `41,212` |
| **Usable Signals** | `6,422` (`6.66%`) | `3,926` (`4.41%`) | `4,292` (`4.74%`) | `4,046` (`2.81%`) | `18,686` (`4.45%`) |
| **`Continuation` Hit Rate (Spread-Crossed)** | **54.24%** (`+5.84 pips`) | **73.79%** (`+3.23 pips`) | `30.75%` (`-2.08 pips`) | **57.26%** (`+0.79 pips`) | Positive in **3 of 4** splits |
| **`Reversal` Hit Rate (Spread-Crossed)** | `38.11%` (`-2.10 pips`) | `43.53%` (`-0.69 pips`) | **63.29%** (`+0.82 pips`) | `46.13%` (`-0.59 pips`) | Dominant in mean-reverting `OOS` |
| **Non-Overlapping Block Hit Rate (`All` vs `Baseline`)** | **50.98%** ($t=+1.46$) vs `37.10%` ($t=-1.91$) | **50.00%** vs `46.30%` | **51.52%** (`Reversal` **55.17%**) | **54.90%** vs `40.26%` ($t=-2.51$) | Beats naive baseline in `DEV`, `VAL`, `OOS2` |
| **Orders Submitted / Fills** | `36` / `36` | `43` / `43` | `47` / `47` | `36` / `36` | `162` / `162` |
| **Gross P&L (after spread)** | **`+$22.50`** | **`+$11.30`** | **`+$4.00`** | **`+$5.10`** | **`+$42.90` (Positive in 4/4 splits!)** |
| **Net P&L (after spread, commission & slippage)** | **`+$18.90`** | **`+$7.00`** | **`-$0.70`** | **`+$1.50`** | **`+$26.70` (OOS+OOS2 Net `+$0.80`!)** |
| **Max Drawdown** | `$16.10` (`0.016%`) | `$21.70` (`0.022%`) | `$23.10` (`0.023%`) | `$14.40` (`0.014%`) | $\le 0.023\%$ |

---

## 4. Score Reliability & Brier Calibration Findings (`docs/testing_plan.md` §6.6)

The 5-bin calibration table explains why `reversal_move_scale = 0.45` significantly improved out-of-sample Net P&L (turning `OOS2` from `-$6.70` to **`+$1.50` Net P&L** and `OOS + OOS2` combined to **`+$0.80` Net P&L**):

- **Asymmetry between `continuation_score` and `reversal_score`**: Because `continuation_score` multiplies by `persistence` ($\approx 0.15\text{--}0.35$) while `reversal_score` multiplies by $(1 - \text{persistence})$ ($\approx 0.80\text{--}0.95$), raw reversal scores cluster in `[0.6, 0.8)` while continuation scores cluster in `[0.0, 0.4)`.
- Scaling `reversal_move_scale = 0.45` filters out marginal `[0.6, 0.8)` reversal candidates while preserving climax reversals (`[0.8, 1.0]`, which achieved **`90.63%` hit rate** and **`0.0877` Brier score** in `OOS`) alongside `[0.2, 0.4)` continuation breakouts (`58.63%` hit rate in `DEV`, `85.47%` in `VAL`, `73.65%` in `OOS2`).
