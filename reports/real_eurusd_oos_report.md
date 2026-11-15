# VECTRA replay report

Source: `examples\real_eurusd_oos.csv`

> Experimental research engine. Scores in this report are **not** calibrated probabilities, and nothing here is a claim of profitability. Synthetic data cannot demonstrate a trading edge.

## 1. Market data

| Metric | Value |
|---|---|
| Lines read | 100000 |
| Rows parsed into events | 100000 |
| Valid events | 90477 |
| Rejected events | 9523 |
| Malformed rows | 0 |
| Stale gaps flagged | 1 |
| Rejection reasons | NegativeSpread=9523 |
| Stream duration | 3.55 hours |

## 2. Predictions and signal distribution

| Metric | Value |
|---|---|
| Event-level predictions | 90477 |
| Usable signals (valid, non-neutral) | 4292 |
| Kind distribution | Continuation=1884, Neutral=86185, Reversal=2408 |
| Rejection reasons | InsufficientData=5419, InsufficientEvidence=64116, NoRuleMatch=16649, None=4292, StaleData=1 |
| Signal rate | 4.744% |

### Causal forward-return evaluation (horizon = 600 valid events)

| Signal kind | Evaluated | Hit rate (mid) | Mean signed mid move | Mean spread-crossed move |
|---|---|---|---|---|
| Continuation | 1239 | 30.751% | -0.000196 | -0.000208 |
| Reversal | 2408 | 63.289% | 0.000094 | 0.000082 |
| All usable signals | 3647 | 52.235% | -0.000005 | -0.000017 |
| Naive directional baseline | 82164 | 47.809% | 0.000006 | -0.000006 |

#### Autocorrelation-robust non-overlapping horizon blocks

| Signal kind | Independent blocks | Block hit rate (mid) | Block mean spread-crossed move | Student t-stat |
|---|---|---|---|---|
| Continuation | 14 | 21.429% | -0.000196 | -2.75 |
| Reversal | 29 | 55.172% | 0.000018 | 0.26 |
| All usable signals | 33 | 51.515% | -0.000026 | -0.37 |
| Naive directional baseline | 66 | 54.545% | 0.000021 | 0.49 |

#### Score reliability & Brier calibration (usable signals)

| Score bin | Evaluated | Mean score | Realized hit rate (mid) | Mean spread-crossed move | Brier score |
|---|---|---|---|---|---|
| [0.0, 0.2) | 1007 | 0.1220 | 30.089% | -0.000197 | 0.2462 |
| [0.2, 0.4) | 203 | 0.2625 | 38.424% | -0.000229 | 0.2565 |
| [0.4, 0.6) | 320 | 0.5638 | 46.250% | -0.000012 | 0.2536 |
| [0.6, 0.8) | 2085 | 0.6861 | 64.604% | 0.000086 | 0.2279 |
| [0.8, 1.0] | 32 | 0.8264 | 90.625% | 0.000285 | 0.0877 |
| Total | 3647 | 0.4972 | 52.235% | -0.000017 | 0.2356 |

Scores are continuity/reversal **scores** in [0,1], produced by a fixed formula with uncalibrated thresholds (see docs/mathematical_model.md). They have not been calibrated against realized outcomes and must not be read as probabilities.

## 3. Decisions and risk

| Metric | Value |
|---|---|
| Decisions allowed | 24 |
| Decisions rejected | 90453 |
| Rejection reasons | Cooldown=1888, DataUnreliable=69, InsufficientEdge=2311, InvalidSignal=5420, NoSignal=80765 |
| Kill switch | inactive |
| Min expected edge (price units) | 0.000100 |
| Max position (base units) | 20000.00 |
| Max daily loss (quote) | 250.00 |
| Max open orders | 2 |

## 4. Execution (simulated)

| Metric | Value |
|---|---|
| Order submissions | 47 |
| Accepted by gateway | 47 |
| Venue rejections (simulated) | 0 |
| Cancellations | 0 |
| Duplicate submissions blocked | 0 |
| Fill events | 47 |
| of which partial fills | 0 |
| Horizon exits submitted | 22 |
| Stop-loss exits submitted | 1 |
| Reconciliation | matched (local and broker accounts agree) |

Execution model: fills occur against the next quote after the configured latency, buys lift the ask and sells hit the bid, with configurable slippage and commission. No order fills at the midpoint by default.

## 5. Simulated performance

| Metric | Value |
|---|---|
| Initial equity | 100000.00 |
| Final equity | 99999.30 |
| Realized P&L | -8.90 |
| Gross P&L (after spread, before commission/slippage) | 4.00 |
| Net P&L | -0.70 |
| Commission paid | 2.35 |
| Slippage paid | 2.35 |
| Estimated spread cost (informational) | 3.20 |
| Max drawdown | 23.10 (0.023%) |

These results come from a synthetic replay. They are a **pipeline smoke test**, not evidence of an edge.

## 6. Latency (microseconds)

| Stage | Median | p95 | p99 | Samples |
|---|---|---|---|---|
| Event processing | 0.8 | 1.4 | 2.1 | 90477 |
| Signal calculation | 0.9 | 1.4 | 2.7 | 90477 |
| Decision | 0.2 | 0.4 | 1.4 | 90477 |
| Simulated order (submit to ack, venue clock) | 40000.0 | 721000.0 | 884000.0 | 47 |

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
Run digest (FNV-1a over signals, decisions, fills, P&L): `b4aaaa60a7b78883`
