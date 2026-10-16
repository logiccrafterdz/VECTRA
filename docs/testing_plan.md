# Testing plan (v0.1)

## 1. Principles

* **Failures are never hidden.** The test runner prints every failure with
  file and line and exits non-zero; `ctest` surfaces it.
* **No external test framework.** `tests/vectra_test.hpp` is a ~150-line
  static-registration + assertion-macro harness. GoogleTest would require
  network access or a package manager at configure time, which this project
  deliberately avoids; the harness can be swapped for GoogleTest later without
  touching test bodies (the `VT_*` macros map 1:1 to `TEST`/`EXPECT_*`).
* **Determinism is tested, not assumed.** Same input ⇒ same digest.
* **Synthetic data is for coverage, not for evidence** (see
  `docs/data_limitations.md`).

## 2. What is covered today (102 tests, `vectra_tests`)

| Area | Tests | Representative cases |
|---|---|---|
| Fixed-point prices | 3 | parse/format round trip, over-precision rejection, exact integer midpoint |
| Bid/ask validation | 5 | healthy quote, crossed market, zero/negative prices, wide spread, wrong symbol, missing timestamp |
| Ordering / staleness | 6 | duplicate sequence, duplicate timestamp, out-of-order sequence and time, stale beyond threshold, forward-gap flag, future timestamp vs reference clock, rejected events do not advance state |
| CSV | 4 | with/without sequence, malformed row reported (not skipped), header/comment skipping |
| Rolling windows | 4 | mean/sample variance, eviction, constant input, empty/reuse |
| Behavior features | 8 | insufficient history, displacement signs, zero volatility (no NaN), bounded features over 500-tick walk, invalid event not ingested, stale-gap propagation, wide-spread flag |
| Exhaustion score | 6 | invalid features, unit-interval bounds, zero weights (no div-by-zero), normal-spread baseline, stale-gap penalty, steady vs stopping move ordering |
| Prediction | 10 | five data-gate rejections, continuation rule, reversal rule, unextended neutral, neutral band, low-confidence suppression, determinism, expected-move cap |
| Risk engine | 13 | allow path, kill switch (+ recovery), disabled engine, daily loss + day reset, invalid/neutral signals, insufficient edge, cost-too-high, max position/exposure/open orders/orders-per-day, invalid-ratio gate, stale gate |
| Order states | 3 | legal transitions, illegal transitions refused, failed transition leaves order untouched |
| Simulated broker | 9 | submit, **duplicate client id protection**, invalid request, venue rejection, latency-gated fill at the ask, partial fill, cumulative partials to full fill, cancel lifecycle, invalid quotes do not drive fills |
| Account | 2 | average-price realized P&L, reconciliation match/mismatch |
| Replay | 8 | **no look-ahead (prefix == full-run prefix)**, deterministic digest, anomaly accounting per reason, kill switch ⇒ zero orders, net ≤ gross, signal counts vs records, end-to-end signal path |
| Report | 2 | all required sections present, unavailable latency shown as `n/a` |
| Telemetry | 6 | nearest-rank quantiles on known samples, sample-count gating, unsorted input, steady-clock stopwatch |
| Config | 8 | JSON typed access, defaults, malformed input with position, settings overlay (ms→ns), missing file, shipped `config/default.json` validity |

Plus a second `ctest` entry, `vectra_sample_replay`, which runs the actual
binary over `examples/sample_ticks.csv` and fails on non-zero exit or on a
reconciliation mismatch.

## 3. How to run

```bash
# Linux / macOS
./scripts/build.sh              # configure + build + ctest

# Windows (Visual Studio generator)
scripts\build.bat

# Just the tests, after a build
ctest --test-dir build -C Release --output-on-failure

# Filter to one area
./build/Release/vectra_tests.exe --filter Prediction
./build/Release/vectra_tests.exe --list
```

## 4. No-look-ahead test in detail

Two runs are made from the same synthetic stream: one with all 400 events, one
truncated to the first 200. Every `SignalRecord` of the truncated run must be
**bit-identical** to the corresponding record of the full run (timestamp,
kind, rejection reason, exhaustion score, expected move, z-score). If any
feature, score or decision ever consulted data beyond the current event, the
prefix would diverge and the test would fail.

## 5. Determinism test in detail

Two engines, same CSV, same configuration: signal/decision/fill records are
hashed (FNV-1a) together with net P&L and max drawdown. The digests must be
equal. This covers PRNG draws (seeded splitmix64), ordering of pending venue
actions, and every floating-point computation in the loop.

## 6. Planned validation work (when real data is available)

1. **Ingest real ticks** (e.g. Dukascopy/TrueFX-style tick data) through a CSV
   converter; verify validation statistics on real anomalies first — the
   engine's value depends on rejecting bad rows correctly.
2. **Split before tuning**: chronological development / validation /
   out-of-sample (e.g. 60/20/20). Lock the test period; never read its
   results during tuning.
3. **Pre-register a threshold grid** for the six `[U]` parameters; evaluate
   the whole grid on development, select on validation, report once on test.
4. **Baseline comparisons**: always compare against (a) always-neutral,
   (b) always-continue-in-direction, (c) a cost-free random signal with the
   same trade count. A model that cannot beat these has no edge.
5. **Statistical rigor**: block bootstrap for confidence intervals (ticks are
   autocorrelated), binomial test of signal hit-rate against base rate,
   multiple-comparison correction across the grid.
6. **Calibration study**: only if signals look informative, test whether
   `continuation_score`/`reversal_score` can be called probabilities
   (reliability diagram, Brier score) — otherwise they stay scores forever.
7. **Negative-result reporting**: add a summary section that stays in the
   report even when the outcome is "no edge found".

## 7. Explicit non-goals of the current suite

* No performance/regression benchmarking harness yet (latency is measured in
  production runs and reported, not asserted).
* No fuzzing of the CSV parser (planned: random byte soup must never crash
  or UB).
* No live-broker adapter tests (no adapter exists in v0.1).
