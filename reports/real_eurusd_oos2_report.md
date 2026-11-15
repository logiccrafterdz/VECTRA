# VECTRA replay report

Source: `examples\real_eurusd_oos2.csv`

> Experimental research engine. Scores in this report are **not** calibrated probabilities, and nothing here is a claim of profitability. Synthetic data cannot demonstrate a trading edge.

## 1. Market data

| Metric | Value |
|---|---|
| Lines read | 161357 |
| Rows parsed into events | 161357 |
| Valid events | 144122 |
| Rejected events | 17235 |
| Malformed rows | 0 |
| Stale gaps flagged | 52 |
| Rejection reasons | NegativeSpread=17081, WideSpread=154 |
| Stream duration | 8.00 hours |

## 2. Predictions and signal distribution

| Metric | Value |
|---|---|
| Event-level predictions | 144122 |
| Usable signals (valid, non-neutral) | 4046 |
| Kind distribution | Continuation=1399, Neutral=140076, Reversal=2647 |
| Rejection reasons | InsufficientData=12570, InsufficientEvidence=109787, NoRuleMatch=17659, None=4046, StaleData=52, WideSpread=8 |
| Signal rate | 2.807% |

### Causal forward-return evaluation (horizon = 600 valid events)

| Signal kind | Evaluated | Hit rate (mid) | Mean signed mid move | Mean spread-crossed move |
|---|---|---|---|---|
| Continuation | 1399 | 57.255% | 0.000092 | 0.000079 |
| Reversal | 2647 | 46.128% | -0.000047 | -0.000059 |
| All usable signals | 4046 | 49.975% | 0.000001 | -0.000012 |
| Naive directional baseline | 129993 | 47.253% | -0.000019 | -0.000030 |

#### Autocorrelation-robust non-overlapping horizon blocks

| Signal kind | Independent blocks | Block hit rate (mid) | Block mean spread-crossed move | Student t-stat |
|---|---|---|---|---|
| Continuation | 16 | 50.000% | 0.000047 | 0.55 |
| Reversal | 44 | 50.000% | -0.000049 | -1.47 |
| All usable signals | 51 | 54.902% | -0.000008 | -0.22 |
| Naive directional baseline | 154 | 40.260% | -0.000053 | -2.51 |

#### Score reliability & Brier calibration (usable signals)

| Score bin | Evaluated | Mean score | Realized hit rate (mid) | Mean spread-crossed move | Brier score |
|---|---|---|---|---|---|
| [0.0, 0.2) | 1046 | 0.1311 | 51.721% | 0.000041 | 0.4019 |
| [0.2, 0.4) | 353 | 0.2397 | 73.654% | 0.000190 | 0.4339 |
| [0.4, 0.6) | 258 | 0.5749 | 70.543% | 0.000055 | 0.2268 |
| [0.6, 0.8) | 2366 | 0.6922 | 43.787% | -0.000070 | 0.3167 |
| [0.8, 1.0] | 23 | 0.8167 | 13.043% | -0.000284 | 0.5877 |
| Total | 4046 | 0.5009 | 49.975% | -0.000012 | 0.3448 |

Scores are continuity/reversal **scores** in [0,1], produced by a fixed formula with uncalibrated thresholds (see docs/mathematical_model.md). They have not been calibrated against realized outcomes and must not be read as probabilities.

## 3. Decisions and risk

| Metric | Value |
|---|---|
| Decisions allowed | 18 |
| Decisions rejected | 144104 |
| Rejection reasons | Cooldown=1664, DataUnreliable=77, InsufficientEdge=2287, InvalidSignal=12630, NoSignal=127446 |
| Kill switch | inactive |
| Min expected edge (price units) | 0.000100 |
| Max position (base units) | 20000.00 |
| Max daily loss (quote) | 250.00 |
| Max open orders | 2 |

## 4. Execution (simulated)

| Metric | Value |
|---|---|
| Order submissions | 36 |
| Accepted by gateway | 36 |
| Venue rejections (simulated) | 0 |
| Cancellations | 0 |
| Duplicate submissions blocked | 0 |
| Fill events | 36 |
| of which partial fills | 0 |
| Horizon exits submitted | 18 |
| Reconciliation | matched (local and broker accounts agree) |

Execution model: fills occur against the next quote after the configured latency, buys lift the ask and sells hit the bid, with configurable slippage and commission. No order fills at the midpoint by default.

## 5. Simulated performance

| Metric | Value |
|---|---|
| Initial equity | 100000.00 |
| Final equity | 100001.50 |
| Realized P&L | 3.30 |
| Gross P&L (after spread, before commission/slippage) | 5.10 |
| Net P&L | 1.50 |
| Commission paid | 1.80 |
| Slippage paid | 1.80 |
| Estimated spread cost (informational) | 1.55 |
| Max drawdown | 14.40 (0.014%) |

These results come from a synthetic replay. They are a **pipeline smoke test**, not evidence of an edge.

## 6. Latency (microseconds)

| Stage | Median | p95 | p99 | Samples |
|---|---|---|---|---|
| Event processing | 0.8 | 1.2 | 1.7 | 144122 |
| Signal calculation | 0.9 | 1.2 | 1.5 | 144122 |
| Decision | 0.2 | 0.3 | 1.4 | 144122 |
| Simulated order (submit to ack, venue clock) | 58000.0 | 1099000.0 | 1473000.0 | 36 |

The first three rows are local processing durations measured with std::chrono::steady_clock. The simulated-order row is measured on the venue's simulated clock (submit to acknowledgement) and, being driven by market-event timestamps, is quantized by the tick interval. Provider timestamps are never subtracted from local timestamps: no clock synchronization model exists in v0.1.

## 7. Limitations and assumptions

- **Synthetic or limited data**: this run does not demonstrate a trading edge on real markets. In-sample threshold tuning would be look-ahead in spirit even where it is not in code.
- **No order flow**: tick updates are quotes only. There is no traded volume, no order book, no aggressor side, and no net positioning data. Tick counts are not volume.
- **No behavioral attribution**: the engine never labels a move as forced liquidation, stop cascades or retail activity; those are latent hypotheses it cannot observe.
- **Simulated venue**: no queue position, no market impact, no depth, no partial-fill selection bias, no rejects beyond the configured model, no swaps/rollover, no margin or financing costs.
- **Uncalibrated scores**: continuation/reversal/confidence values are scores from a fixed formula with configurable, uncalibrated thresholds (marked UNCALIBRATED in code and config).
- **Event-counted horizons**: windows are measured in ticks, not wall-clock time, so results depend on the tick rate of the source.
- **Single instrument**: v0.1 replays one symbol per run; cross-asset and portfolio effects are untested.
- **Split discipline**: when real data arrives, separate development / validation / out-of-sample periods and never tune thresholds on the final test period.

---
Run digest (FNV-1a over signals, decisions, fills, P&L): `cff31ffe05a0d51d`
