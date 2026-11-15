# VECTRA replay report

Source: `examples\real_eurusd_val.csv`

> Experimental research engine. Scores in this report are **not** calibrated probabilities, and nothing here is a claim of profitability. Synthetic data cannot demonstrate a trading edge.

## 1. Market data

| Metric | Value |
|---|---|
| Lines read | 100000 |
| Rows parsed into events | 100000 |
| Valid events | 89127 |
| Rejected events | 10873 |
| Malformed rows | 0 |
| Stale gaps flagged | 6 |
| Rejection reasons | NegativeSpread=10873 |
| Stream duration | 2.95 hours |

## 2. Predictions and signal distribution

| Metric | Value |
|---|---|
| Event-level predictions | 89127 |
| Usable signals (valid, non-neutral) | 3926 |
| Kind distribution | Continuation=974, Neutral=85201, Reversal=2952 |
| Rejection reasons | InsufficientData=6060, InsufficientEvidence=57377, NoRuleMatch=21758, None=3926, StaleData=6 |
| Signal rate | 4.405% |

### Causal forward-return evaluation (horizon = 600 valid events)

| Signal kind | Evaluated | Hit rate (mid) | Mean signed mid move | Mean spread-crossed move |
|---|---|---|---|---|
| Continuation | 931 | 73.792% | 0.000338 | 0.000323 |
| Reversal | 2911 | 43.525% | -0.000055 | -0.000069 |
| All usable signals | 3842 | 50.859% | 0.000040 | 0.000026 |
| Naive directional baseline | 81405 | 49.690% | 0.000015 | 0.000002 |

#### Autocorrelation-robust non-overlapping horizon blocks

| Signal kind | Independent blocks | Block hit rate (mid) | Block mean spread-crossed move | Student t-stat |
|---|---|---|---|---|
| Continuation | 8 | 62.500% | 0.000067 | 0.30 |
| Reversal | 25 | 60.000% | -0.000045 | -0.46 |
| All usable signals | 26 | 50.000% | -0.000077 | -0.71 |
| Naive directional baseline | 54 | 46.296% | -0.000019 | -0.30 |

#### Score reliability & Brier calibration (usable signals)

| Score bin | Evaluated | Mean score | Realized hit rate (mid) | Mean spread-crossed move | Brier score |
|---|---|---|---|---|---|
| [0.0, 0.2) | 775 | 0.1282 | 73.290% | 0.000300 | 0.5624 |
| [0.2, 0.4) | 156 | 0.2318 | 76.282% | 0.000434 | 0.4606 |
| [0.4, 0.6) | 185 | 0.5735 | 59.459% | 0.000158 | 0.2494 |
| [0.6, 0.8) | 2576 | 0.6948 | 41.887% | -0.000092 | 0.3273 |
| [0.8, 1.0] | 150 | 0.8171 | 52.000% | 0.000044 | 0.3383 |
| Total | 3842 | 0.5607 | 50.859% | 0.000026 | 0.3768 |

Scores are continuity/reversal **scores** in [0,1], produced by a fixed formula with uncalibrated thresholds (see docs/mathematical_model.md). They have not been calibrated against realized outcomes and must not be read as probabilities.

## 3. Decisions and risk

| Metric | Value |
|---|---|
| Decisions allowed | 22 |
| Decisions rejected | 89105 |
| Rejection reasons | Cooldown=2759, DataUnreliable=131, InsufficientEdge=1014, InvalidSignal=6066, NoSignal=79135 |
| Kill switch | inactive |
| Min expected edge (price units) | 0.000100 |
| Max position (base units) | 20000.00 |
| Max daily loss (quote) | 250.00 |
| Max open orders | 2 |

## 4. Execution (simulated)

| Metric | Value |
|---|---|
| Order submissions | 43 |
| Accepted by gateway | 43 |
| Venue rejections (simulated) | 0 |
| Cancellations | 0 |
| Duplicate submissions blocked | 0 |
| Fill events | 43 |
| of which partial fills | 0 |
| Horizon exits submitted | 17 |
| Stop-loss exits submitted | 4 |
| Reconciliation | matched (local and broker accounts agree) |

Execution model: fills occur against the next quote after the configured latency, buys lift the ask and sells hit the bid, with configurable slippage and commission. No order fills at the midpoint by default.

## 5. Simulated performance

| Metric | Value |
|---|---|
| Initial equity | 100000.00 |
| Final equity | 100007.00 |
| Realized P&L | 7.70 |
| Gross P&L (after spread, before commission/slippage) | 11.30 |
| Net P&L | 7.00 |
| Commission paid | 2.15 |
| Slippage paid | 2.15 |
| Estimated spread cost (informational) | 2.80 |
| Max drawdown | 21.70 (0.022%) |

These results come from a synthetic replay. They are a **pipeline smoke test**, not evidence of an edge.

## 6. Latency (microseconds)

| Stage | Median | p95 | p99 | Samples |
|---|---|---|---|---|
| Event processing | 0.8 | 1.1 | 1.5 | 89127 |
| Signal calculation | 0.9 | 1.0 | 1.7 | 89127 |
| Decision | 0.2 | 0.3 | 1.4 | 89127 |
| Simulated order (submit to ack, venue clock) | 76000.0 | 1450000.0 | 4052000.0 | 43 |

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
Run digest (FNV-1a over signals, decisions, fills, P&L): `250ba6c3e99a6a3c`
