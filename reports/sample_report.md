# VECTRA replay report

Source: `examples/sample_ticks.csv`

> Experimental research engine. Scores in this report are **not** calibrated probabilities, and nothing here is a claim of profitability. Synthetic data cannot demonstrate a trading edge.

## 1. Market data

| Metric | Value |
|---|---|
| Lines read | 6000 |
| Rows parsed into events | 5999 |
| Valid events | 5395 |
| Rejected events | 604 |
| Malformed rows | 1 |
| Stale gaps flagged | 2 |
| Rejection reasons | Duplicate=2, Malformed=1, NegativeSpread=1, WideSpread=600, WrongSymbol=1 |
| Stream duration | 10.16 minutes |

## 2. Predictions and signal distribution

| Metric | Value |
|---|---|
| Event-level predictions | 5395 |
| Usable signals (valid, non-neutral) | 2339 |
| Kind distribution | Continuation=2333, Neutral=3056, Reversal=6 |
| Rejection reasons | InsufficientData=128, InsufficientEvidence=1824, LowVolatility=344, NoRuleMatch=758, None=2339, StaleData=2 |
| Signal rate | 43.355% |

Scores are continuity/reversal **scores** in [0,1], produced by a fixed formula with uncalibrated thresholds (see docs/mathematical_model.md). They have not been calibrated against realized outcomes and must not be read as probabilities.

## 3. Decisions and risk

| Metric | Value |
|---|---|
| Decisions allowed | 40 |
| Decisions rejected | 5355 |
| Rejection reasons | CostTooHigh=115, InsufficientEdge=1067, InvalidSignal=474, MaxOrdersPerDay=433, MaxPosition=684, NoSignal=2582 |
| Kill switch | inactive |
| Min expected edge (price units) | 0.000150 |
| Max position (base units) | 50000.00 |
| Max daily loss (quote) | 250.00 |
| Max open orders | 3 |

## 4. Execution (simulated)

| Metric | Value |
|---|---|
| Order submissions | 40 |
| Accepted by gateway | 40 |
| Venue rejections (simulated) | 0 |
| Cancellations | 0 |
| Duplicate submissions blocked | 0 |
| Fill events | 53 |
| of which partial fills | 13 |
| Reconciliation | matched (local and broker accounts agree) |

Execution model: fills occur against the next quote after the configured latency, buys lift the ask and sells hit the bid, with configurable slippage and commission. No order fills at the midpoint by default.

## 5. Simulated performance

| Metric | Value |
|---|---|
| Initial equity | 100000.00 |
| Final equity | 103791.70 |
| Realized P&L | 3581.47 |
| Gross P&L (after spread, before commission/slippage) | 3795.68 |
| Net P&L | 3791.70 |
| Commission paid | 1.99 |
| Slippage paid | 1.99 |
| Estimated spread cost (informational) | 10.99 |
| Max drawdown | 899.91 (0.861%) |

These results come from a synthetic replay. They are a **pipeline smoke test**, not evidence of an edge.

## 6. Processing latency (steady_clock, microseconds)

| Stage | Median | p95 | p99 | Samples |
|---|---|---|---|---|
| Event processing | 0.4 | 0.6 | 0.7 | 5395 |
| Signal calculation | 0.5 | 1.9 | 2.2 | 5395 |
| Decision | 0.3 | 2.9 | 3.2 | 5395 |
| Simulated order (submit to ack) | 100000.0 | 100000.0 | 100000.0 | 40 |

Measured exclusively with std::chrono::steady_clock durations. Provider timestamps are never subtracted from local timestamps: no clock synchronization model exists in v0.1.

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
Run digest (FNV-1a over signals, decisions, fills, P&L): `bc06ff21b93aecfb`
