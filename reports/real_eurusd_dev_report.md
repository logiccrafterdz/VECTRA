# VECTRA replay report

Source: `examples\real_eurusd_dev.csv`

> Experimental research engine. Scores in this report are **not** calibrated probabilities, and nothing here is a claim of profitability. Synthetic data cannot demonstrate a trading edge.

## 1. Market data

| Metric | Value |
|---|---|
| Lines read | 100000 |
| Rows parsed into events | 100000 |
| Valid events | 96419 |
| Rejected events | 3581 |
| Malformed rows | 0 |
| Stale gaps flagged | 54 |
| Rejection reasons | NegativeSpread=3581 |
| Stream duration | 6.48 hours |

## 2. Predictions and signal distribution

| Metric | Value |
|---|---|
| Event-level predictions | 96419 |
| Usable signals (valid, non-neutral) | 6422 |
| Kind distribution | Continuation=2649, Neutral=89997, Reversal=3773 |
| Rejection reasons | InsufficientData=6564, InsufficientEvidence=57484, NoRuleMatch=25897, None=6422, StaleData=52 |
| Signal rate | 6.661% |

### Causal forward-return evaluation (horizon = 600 valid events)

| Signal kind | Evaluated | Hit rate (mid) | Mean signed mid move | Mean spread-crossed move |
|---|---|---|---|---|
| Continuation | 2642 | 54.239% | 0.000607 | 0.000584 |
| Reversal | 3773 | 38.113% | -0.000186 | -0.000210 |
| All usable signals | 6415 | 44.754% | 0.000141 | 0.000117 |

Scores are continuity/reversal **scores** in [0,1], produced by a fixed formula with uncalibrated thresholds (see docs/mathematical_model.md). They have not been calibrated against realized outcomes and must not be read as probabilities.

## 3. Decisions and risk

| Metric | Value |
|---|---|
| Decisions allowed | 45 |
| Decisions rejected | 96374 |
| Rejection reasons | Cooldown=5012, DataUnreliable=50, InsufficientEdge=1315, InvalidSignal=6616, NoSignal=83381 |
| Kill switch | inactive |
| Min expected edge (price units) | 0.000100 |
| Max position (base units) | 20000.00 |
| Max daily loss (quote) | 250.00 |
| Max open orders | 2 |

## 4. Execution (simulated)

| Metric | Value |
|---|---|
| Order submissions | 88 |
| Accepted by gateway | 88 |
| Venue rejections (simulated) | 0 |
| Cancellations | 0 |
| Duplicate submissions blocked | 0 |
| Fill events | 88 |
| of which partial fills | 0 |
| Horizon exits submitted | 39 |
| Stop-loss exits submitted | 2 |
| Take-profit exits submitted | 3 |
| Reconciliation | matched (local and broker accounts agree) |

Execution model: fills occur against the next quote after the configured latency, buys lift the ask and sells hit the bid, with configurable slippage and commission. No order fills at the midpoint by default.

## 5. Simulated performance

| Metric | Value |
|---|---|
| Initial equity | 100000.00 |
| Final equity | 100033.20 |
| Realized P&L | 37.60 |
| Gross P&L (after spread, before commission/slippage) | 42.00 |
| Net P&L | 33.20 |
| Commission paid | 4.40 |
| Slippage paid | 4.40 |
| Estimated spread cost (informational) | 8.15 |
| Max drawdown | 19.10 (0.019%) |

These results come from a synthetic replay. They are a **pipeline smoke test**, not evidence of an edge.

## 6. Latency (microseconds)

| Stage | Median | p95 | p99 | Samples |
|---|---|---|---|---|
| Event processing | 1.5 | 2.0 | 2.6 | 96419 |
| Signal calculation | 1.8 | 2.1 | 3.6 | 96419 |
| Decision | 0.3 | 0.4 | 2.8 | 96419 |
| Simulated order (submit to ack, venue clock) | 65000.0 | 1665000.0 | 2540000.0 | 88 |

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
Run digest (FNV-1a over signals, decisions, fills, P&L): `5e7e34729430d72b`
