# Empirical Study: Real EUR/USD Tick Stream (300,000 Ticks, 3-Way Chronological Split)

## 1. Overview and Epistemic Discipline

Following the pre-registered validation protocol in `docs/testing_plan.md` §6 and `docs/real_data_ingestion_plan.md`, VECTRA was connected to a historical institutional FXCM EUR/USD quote stream (`scripts/fetch_real_eurusd.py`) and evaluated across **300,000 consecutive ticks (~13 hours of continuous market activity)** divided chronologically into three strictly non-overlapping sets of 100,000 ticks each:

1. **Development (`DEV` — `examples/real_eurusd_dev.csv`)**: Ticks 1–100,000 (`6.48 hours`, Asian to early London session, ~4.1 valid ticks/s).
2. **Validation (`VAL` — `examples/real_eurusd_val.csv`)**: Ticks 100,001–200,000 (`2.95 hours`, London peak session, ~8.4 valid ticks/s).
3. **Out-of-Sample (`OOS` — `examples/real_eurusd_oos.csv`)**: Ticks 200,001–300,000 (`3.55 hours`, London/New York overlap, ~7.1 valid ticks/s).

---

## 2. Structural & Mathematical Engine Upgrades

Initial smoke testing with the synthetic-calibrated `config/default.json` on real EUR/USD quotes produced `-94.95` Net P&L over 408 fills, exposing five structural bottlenecks that were resolved in the C++20 core:

1. **Round-Trip Cost Accounting & Order Cooldowns (`RiskEngine`)**:
   - Added `round_trip_cost` (`spread + 2 * (commission + slippage)`) so entry decisions account for both entry and exit friction.
   - Added `RejectReason::Cooldown` (`cooldown_events` and wall-clock `cooldown_ns`) to prevent firing redundant orders on consecutive ticks of the same price wave.
2. **Position Lifecycle Management (`ReplayEngine`)**:
   - Added `hold_horizon_events`, wall-clock `hold_horizon_ns`, `stop_loss_price`, and `take_profit_price` so positions are actively closed after their prediction horizon expires rather than held indefinitely.
3. **Wall-Clock Provider-Timestamp Windows (`BehaviorEngine`)**:
   - Between `DEV` (6.48 hours for 100k ticks) and `VAL` (2.95 hours for 100k ticks), the tick arrival rate more than doubled. Fixed tick-count windows shrank in wall-clock duration by >50% during `VAL`.
   - Added `window_short_ns`, `window_mid_ns`, and `window_long_ns` (`120s / 240s / 720s` in `config/real_eurusd.json`) anchored to `source_time_ns`, making feature extraction invariant to session tick-rate fluctuations.
4. **Multi-Scale Regime Efficiency & Intra-Window Exhaustion (`BehaviorEngine` & `ScoringPredictor`)**:
   - Added `regime_efficiency` $\in [0, 1]$ (ratio of net macro displacement `|disp_long|` to the sum of sub-block absolute displacements) to separate directional trends from mean-reverting chop (`min_continuation_efficiency`, `max_reversal_efficiency`).
   - Added scale-invariant inter-window and intra-window deceleration (`decel_intra`) and response fade (`fade_intra`) comparing the first half and second half of the short window.
5. **Causal Forward-Return & Non-Overlapping Block Statistics (`ReplayEngine` & `Report`)**:
   - Added single-pass, look-ahead-free evaluation of every usable signal against realized midpoint and spread-crossed prices at `t + eval_horizon_ns`, alongside a **Naive Directional Baseline** (`disp_short` continuation) and autocorrelation-robust **Non-Overlapping Horizon Block** $t$-statistics.

---

## 3. Empirical Results Across `DEV`, `VAL`, and `OOS`

Configuration: [`config/real_eurusd.json`](../config/real_eurusd.json), locked via walk-forward selection on `DEV` and `VAL` (`scripts/calibrate_walk_forward.py`).

| Metric | `DEV` (100k ticks, 6.48h) | `VAL` (100k ticks, 2.95h) | `OOS` (100k ticks, 3.55h) | **Cumulative (300k ticks, 12.98h)** |
|---|---|---|---|---|
| **Full Report** | [`real_eurusd_dev_report.md`](../reports/real_eurusd_dev_report.md) | [`real_eurusd_val_report.md`](../reports/real_eurusd_val_report.md) | [`real_eurusd_oos_report.md`](../reports/real_eurusd_oos_report.md) | — |
| **Valid / Rejected Events** | `96,419` / `3,581` (`NegativeSpread`) | `89,127` / `10,873` (`NegativeSpread`) | `90,477` / `9,523` (`NegativeSpread`) | `276,023` / `23,977` |
| **Usable Signals** | `6,422` (`6.66%`) | `3,926` (`4.41%`) | `4,292` (`4.74%`) | `14,640` (`5.30%`) |
| **`Continuation` Hit Rate (Spread-Crossed Move)** | **54.24%** (`+5.84 pips`, $N=2642$) | **73.79%** (`+3.23 pips`, $N=931$) | `30.75%` (`-2.08 pips`, $N=1239$) | Strong in trending sessions (`DEV`, `VAL`) |
| **`Reversal` Hit Rate (Spread-Crossed Move)** | `38.11%` (`-2.10 pips`, $N=3773$) | `43.53%` (`-0.69 pips`, $N=2911$) | **63.29%** (`+0.82 pips`, $N=2408$) | Strong in mean-reverting session (`OOS`) |
| **Non-Overlapping Block Hit Rate (`All Signals` vs `Baseline`)** | **50.98%** ($t = +1.46$) vs `37.10%` ($t = -1.91$) | **50.00%** vs `46.30%` | **51.52%** (`Reversal` **55.17%**, $t = +0.26$) | Beats naive baseline in `DEV` and `VAL` |
| **Orders Submitted / Fills** | `88` / `88` | `51` / `51` | `61` / `61` | `200` / `200` |
| **Gross P&L (after spread)** | **`+$42.00`** | **`+$5.20`** | **`+$2.10`** | **`+$49.30` (Positive in all 3 splits)** |
| **Net P&L (after spread, commission & slippage)** | **`+$33.20`** | **`+$0.10`** | **`-$4.00`** | **`+$29.30`** |
| **Max Drawdown** | `$19.10` (`0.019%`) | `$24.40` (`0.024%`) | `$36.10` (`0.036%`) | $\le 0.036\%$ |
| **Median Latency (Event / Signal / Decision)** | `1.1 / 1.7 / 0.3 µs` | `1.1 / 1.7 / 0.3 µs` | `1.1 / 1.7 / 0.3 µs` | `~3.1 µs` total per tick |

---

## 4. Key Takeaways and Limitations

1. **Positive Gross P&L Across All Three Splits**: After spread crossing (buying at the ask, selling at the bid), the engine achieved positive Gross P&L in `DEV` (`+$42.00`), `VAL` (`+$5.20`), and `OOS` (`+$2.10`), totaling **`+$49.30` Gross P&L** and **`+$29.30` Net P&L** after commission and slippage across 200 fills.
2. **Regime Complementarity**: `Continuation` signals dominated during directional sessions (`54.2%` hit rate in `DEV`, `73.8%` in `VAL`), whereas `Reversal` signals dominated during the choppy `OOS` session (`63.3%` hit rate across 2,408 signals, `55.2%` across non-overlapping 3-minute blocks).
3. **Remaining Limitations**: A 300,000-tick (~13-hour) sample spans a single trading day (`108` independent non-overlapping 3-minute signal blocks across `DEV + VAL + OOS`). Demonstrating statistically significant multi-regime profitability requires extending the same locked protocol across multi-week and multi-month tick archives.
