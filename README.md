# VECTRA

A deterministic C++20 research engine for testing short-horizon price-behavior hypotheses on FX tick data. It reads quotes, validates them, computes causal features, makes rule-based `Continuation` / `Reversal` / `Neutral` calls, passes them through a pre-trade risk gate, and simulates execution with latency and costs. Every run produces a full audit report.

![VECTRA pipeline](docs/assets/pipeline_architecture.svg)

## Summary

| | |
|---|---|
| **Research question** | Do measurable signs of momentum *exhaustion* (extension, deceleration, fading response) in EUR/USD quotes predict continuation or reversal over the next 180 s, after costs? |
| **Answer so far** | **No evidence of an edge.** On one trading day of EUR/USD data, the out-of-sample net result is `+0.80 USD` over 42 trades, and no out-of-sample signal set is statistically different from zero. |
| **What the project does show** | A causal, deterministic, fully tested pipeline that reports negative results honestly, with autocorrelation-robust statistics and a naive baseline for every split. |
| **Language / dependencies** | C++20, CMake >= 3.20, no third-party libraries. Python 3.9+ only for optional scripts. |
| **Tests** | 115 unit tests plus a deterministic sample replay (`ctest`). |
| **Status** | Research prototype (`v0.1`). Not a trading system. |

## Contents

1. [Results](#results)
2. [Design principles](#design-principles)
3. [Pipeline](#pipeline)
4. [Mathematical model](#mathematical-model)
5. [Data](#data)
6. [Build and run](#build-and-run)
7. [Reproduce the results](#reproduce-the-results)
8. [Limitations and next steps](#limitations-and-next-steps)
9. [Repository map](#repository-map)
10. [Notes for contributors and AI agents](#notes-for-contributors-and-ai-agents)

## Results

The configuration [`config/real_eurusd.json`](config/real_eurusd.json) was tuned on `DEV` and `VAL` and then run on `OOS 1` and `OOS 2`. Full tables, per-branch statistics, score calibration and latency are in [`docs/results_eurusd.md`](docs/results_eurusd.md).

| Split | Duration | Entries | Gross P&L (after spread) | Net P&L (all costs) | "All usable" signals: non-overlapping blocks | Block `t` |
|---|---:|---:|---:|---:|---:|---:|
| `DEV` | 6.48 h | 18 | +22.50 | +18.90 | 51 | +1.46 |
| `VAL` | 2.95 h | 22 | +11.30 | +7.00 | 26 | -0.71 |
| `OOS 1` | 3.55 h | 24 | +4.00 | -0.70 | 33 | -0.37 |
| `OOS 2` | 8.00 h | 18 | +5.10 | +1.50 | 51 | -0.22 |

P&L is in USD for 10,000-unit orders. Source: [`reports/`](reports/).

![Non-overlapping block hit rates](docs/assets/block_hit_rates.svg)

How to read these numbers:

- **The out-of-sample result is indistinguishable from zero.** `OOS 1 + OOS 2` net is `+0.80 USD`, less than the `8.30 USD` paid in commission and slippage. With realistic retail costs (about 0.5-1.0 pip round trip, against the 0.05 pip per side modelled here) it would be negative.
- **One branch fails significantly.** `Continuation` on `OOS 1` has a block `t` of `-2.75`, meaning it loses.
- **The scores are not calibrated.** In the highest score bin the hit rate is 8.9% on `DEV` and 90.6% on `OOS 1`.
- **The out-of-sample splits are no longer blind.** OOS results were looked at between [engineering stages](docs/results_eurusd.md#2-iterative-engineering-progression-five-stages), so they influenced the design.
- **The sample is small.** One trading day of one currency pair cannot establish or rule out an edge.

## Design principles

1. **Causality.** Features, predictions and risk decisions at event `i` use only validated events `0..i`. Forward returns are computed afterwards, purely for evaluation.
2. **Determinism.** Prices are stored as 64-bit fixed-point integers (`10^-8`), timestamps in nanoseconds, and the execution simulator uses a fixed seed. Identical input and configuration give an identical FNV-1a 64-bit run digest on the same toolchain. Digests differ between toolchains (e.g. MSVC vs GCC) because floating-point feature code is not bit-identical across compilers, although every reported metric matches.
3. **Wall-clock horizons.** Tick arrival rates differ by 3-5x between trading sessions. Windows (120 s / 240 s / 720 s), the holding period (180 s) and the cooldown (180 s) are therefore measured on provider timestamps, not tick counts.
4. **Full cost accounting.** The risk gate and the simulated broker both charge spread plus commission and slippage on entry and exit. Orders fill on the next quote after latency, at bid or ask, never at mid.
5. **Honest statistics.** Forward returns from overlapping ticks share most of their path. Significance is therefore measured on non-overlapping 180 s blocks, every split includes a naive baseline (follow the last move), and scores are reported as *scores*, not probabilities.
6. **Neutral is a valid answer.** About 95% of events produce no signal. The system only acts when every gate agrees.

## Pipeline

| Stage | Responsibility | Code |
|---|---|---|
| 1. Ingestion and validation | Parse CSV or stdin into fixed-point quotes; reject crossed or wide spreads, out-of-order and stale data; flag gaps > 15 s. | [`market/`](src/market/) |
| 2. Behavior features | Displacement, response per tick, Welford volatility, sign persistence, deceleration, response fade, and block regime efficiency over wall-clock windows. | [`behavior/behavior_engine.cpp`](src/behavior/behavior_engine.cpp) |
| 3. Exhaustion score | Weighted combination in `[0, 1]` of extension, deceleration, response fade and a data-quality term. | [`behavior/exhaustion.cpp`](src/behavior/exhaustion.cpp) |
| 4. Prediction | Rule-based `Continuation` / `Reversal` / `Neutral`, branch scores, and a heuristic expected move. | [`prediction/scoring_predictor.cpp`](src/prediction/scoring_predictor.cpp) |
| 5. Risk gate | Kill switch, data health, spread ceiling, round-trip edge, position and exposure caps, daily loss, order limits and cooldown. | [`risk/risk_engine.cpp`](src/risk/risk_engine.cpp) |
| 6. Simulated execution | Acknowledgement and fill latency queue, bid/ask crossing, slippage, commission, P&L ledger, and horizon / stop-loss / take-profit exits. | [`execution/`](src/execution/) |
| 7. Evaluation and report | Forward returns, block `t`-statistics, naive baseline, 5-bin Brier table, latency percentiles and run digest. | [`backtest/`](src/backtest/) |

Processing is single-threaded and event-ordered. The median end-to-end cost per tick is about 1.4 µs in a Release build. Architectural invariants are documented in [`docs/architecture.md`](docs/architecture.md).

## Mathematical model

All formulas below are transcribed from the source files named in each heading; thresholds are the values in [`config/real_eurusd.json`](config/real_eurusd.json). The original v0.1 specification and the falsification criteria are in [`docs/mathematical_model.md`](docs/mathematical_model.md).

### Notation

```text
b_i, a_i       bid and ask of valid tick i (fixed-point, 10^-8)
m_i            (b_i + a_i) / 2                     mid price
s_i            a_i - b_i                           quoted spread
t_i            provider timestamp (source_time_ns)
sigma          rolling Welford std of tick log returns ln(m_i / m_{i-1})
S, M, L        short / mid / long windows = 120 s / 240 s / 720 s
```

### 1. Window displacement and response (`src/behavior/behavior_engine.cpp`)

For each window `W`, `k_W` is the oldest buffered tick with `t_i - t_{k_W} <= tau_W`:

```text
D_W  = ln(m_i / m_{k_W})              signed log displacement over W
n_W  = max(1, i - k_W)                ticks inside W
v_W  = D_W / n_W                      response: displacement per tick
z_W  = D_W / (sigma * sqrt(n_W))      volatility-normalized extension
```

### 2. Deceleration

Compares the latest short window with the 120 s before it (inter-window), and the two halves of the short window with each other (intra-window):

```text
vol_scale    = sigma * sqrt(n_S)
D_prev       = D_M - D_S
decel_inter  = sgn(D_S) * (D_prev - D_S) / (vol_scale * sqrt(2))

D_recent     = ln(m_i / m_{k_half})          last 60 s
D_older      = D_S - D_recent                 first 60 s of the short window
decel_intra  = sgn(D_S) * (D_older - D_recent) / vol_scale

deceleration = clamp(max(decel_inter, decel_intra), -1, 1)
```

### 3. Response fade

```text
fade_inter    = clamp(1 - |v_S| / |v_M|, 0, 1)
fade_intra    = clamp(1 - |D_recent / n_half| / |v_S|, 0, 1)
response_fade = max(fade_inter, fade_intra)
```

### 4. Block regime efficiency

The long window is cut into `K = clamp(n_L / n_S, 2, 16)` blocks of equal tick count:

```text
regime_efficiency = clamp( |D_L| / sum_{k=1..K} |ln(m_end(k) / m_start(k))| , 0, 1)
```

Near 1: a steady one-directional path. Near 0: oscillation with little net progress.

### 5. Exhaustion score (`src/behavior/exhaustion.cpp`)

```text
extension     = clamp(|z_S| / 3.5, 0, 1)
deceleration  = clamp(deceleration, 0, 1)
response_fade = clamp(response_fade, 0, 1)
quality       = penalty in [0,1] for elevated spread, stale gaps and low activity

E = 0.30 * extension + 0.25 * deceleration + 0.35 * response_fade + 0.10 * quality
```

### 6. Branch scores and decision rule (`src/prediction/scoring_predictor.cpp`)

```text
gate    = clamp(|z_S| / 1.8, 0, 1)
P+      = clamp(persistence, 0, 1)            persistence = mean of sgn(r_j) * sgn(D_S) over the last 80 tick returns
C_cont  = (1 - E) * gate * P+
C_rev   =      E  * gate * (1 - P+)

if |z_S| < 1.8                                                  -> Neutral
Reversal      (direction = -sgn(D_S))  if E >= 0.65 and persistence <= 0.20
Continuation  (direction = +sgn(D_S))  if E <= 0.35 and persistence >= 0.10
                                          and regime_efficiency >= 0.55
                                          and D_S * (D_M - D_S) > 0        (mid-window confirmation)
otherwise                                                        -> Neutral
```

A signal is also dropped if its data-quality confidence is below 0.35.

### 7. Expected move

```text
strength          = C_branch * scale_branch          scale_cont = 1.00, scale_rev = 0.45
expected_move_log = direction * strength * clamp(|D_S|, 0, 0.005)
```

`scale_rev` offsets the fact that `C_rev` is structurally larger than `C_cont` (persistence is usually small, so `1 - P+` is close to 1). The expected move is a heuristic magnitude, not a forecast distribution.

### 8. Round-trip cost gate (`src/risk/risk_engine.cpp`)

```text
cost  = 2 * (s_i / 2 + commission + slippage) = s_i + 2 * (commission + slippage)
edge  = expm1(|expected_move_log|) * m_i - cost
trade only if edge >= 1e-4 and every other gate passes
```

Other gates: kill switch, data health (invalid-tick ratio and staleness), spread ceiling, position and exposure caps, daily loss limit, open-order and daily-order limits, and a 180 s wall-clock cooldown.

## Data

| File | Content |
|---|---|
| [`examples/real_eurusd_{dev,val,oos,oos2}.csv`](examples/) | 461,357 EUR/USD quotes from Friday 29 June 2018 (00:00-20:59 UTC), split chronologically. Source: the public FXCM sample distributed with *Python for Finance, 2nd ed.* (`yhilpisch/py4fi2nd`), fetched by [`scripts/fetch_real_eurusd.py`](scripts/fetch_real_eurusd.py). |
| [`examples/sample_ticks.csv`](examples/sample_ticks.csv) | 6,000 lines of synthetic data with injected malformed rows, crossed spreads, stale timestamps, trends and reversals. Used by the test suite. |

Input schema: `symbol,bid,ask,source_ts_ns,sequence`. Data quality notes: about 10% of the raw EUR/USD ticks have `ask < bid` and are rejected by the validator, and about 21% have `ask == bid`. Quotes carry no volume or order-book information. See [`docs/data_limitations.md`](docs/data_limitations.md).

## Build and run

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The executable is `build/vectra` (Linux/macOS) or `build\Release\vectra.exe` (Windows, Visual Studio generator).

```text
vectra --data <ticks.csv|-> [--config <config.json>] [--report <out.md>] [--stream] [--quiet]

  --data <path>    CSV tick file, or '-' to read a live stream from stdin
  --config <path>  JSON configuration (default: config/default.json if present)
  --report <path>  write the Markdown audit report
  --stream         emit JSONL DECISION / FILL / SUMMARY events to stdout
  --quiet          print only the one-line summary
```

Streaming example:

```bash
head -n 5000 examples/real_eurusd_dev.csv | ./build/vectra --config config/real_eurusd.json --data - --stream
```

## Reproduce the results

```bash
for s in dev val oos oos2; do
  ./build/vectra --config config/real_eurusd.json \
                 --data examples/real_eurusd_$s.csv \
                 --report reports/real_eurusd_${s}_report.md --quiet
done

python scripts/calibrate_walk_forward.py   # grid search on DEV + VAL, then a locked run on OOS 1 and OOS 2 (expects the Windows binary path)
python scripts/update_site_data.py         # refresh docs/data/metrics.json for the GitHub Pages view
```

Apart from the run digest and latency figures, the regenerated reports should match the committed ones line for line. This was verified with GCC 11 on Linux against reports produced with MSVC on Windows.

An interactive view of the same data is published at <https://logiccrafterdz.github.io/VECTRA/>.

## Limitations and next steps

**Limitations**

- One trading day, one instrument, quotes only (no volume, depth or aggressor side).
- The out-of-sample splits have been seen during development.
- Optimistic cost model; no swaps, financing, market impact or queue position.
- Scores are uncalibrated heuristics; the thresholds are hand-set or grid-searched on a small sample.

**Next steps that would make the result meaningful**

1. Run on several months of fresh, untouched data (for example Dukascopy or HistData) and on several currency pairs.
2. Pre-register the configuration and the evaluation before looking at the new data.
3. Report sensitivity to costs (0.1 to 1.0 pip round trip) and to ±10% threshold changes.
4. Test each feature on its own against the naive baseline before combining them.

## Repository map

```text
include/vectra/<module>/*.hpp   public interfaces (market, behavior, prediction, risk, execution, backtest, config, telemetry)
src/<module>/*.cpp              implementations; src/main.cpp is the CLI
tests/test_*.cpp                115 unit tests (self-contained harness, no framework)
config/default.json             configuration for the synthetic sample and tests
config/real_eurusd.json         locked configuration for the EUR/USD study
examples/                       input data (see Data)
reports/                        generated audit reports, one per run
scripts/                        data fetch/ingest, sample generation, walk-forward calibration, site data
docs/results_eurusd.md          full empirical results
docs/mathematical_model.md      model specification and falsification criteria
docs/architecture.md            layer boundaries and invariants
docs/testing_plan.md            validation and calibration protocol
docs/data_limitations.md        what the data cannot tell you
docs/audit_v0.1.md              v0.1 self-audit and fixes
docs/index.html, docs/assets/   GitHub Pages view and SVG figures
```

## Notes for contributors and AI agents

- **Invariants that must not break:** no feature may read events after `i`; prices stay in fixed-point `PriceFixed`; timestamps are `source_time_ns` (nanoseconds); costs are charged on entry and exit.
- **Single source of truth:** results in this README and in `docs/` must come from `reports/*.md`. After changing any code or configuration, regenerate the reports and update the tables from them rather than editing numbers by hand.
- **Verifying a change:** run `ctest`, then replay `examples/sample_ticks.csv` twice and confirm the digest is unchanged on your toolchain.
- **Language of results:** call the outputs *scores*, not probabilities, and do not describe a result as an edge unless it holds on untouched data with non-overlapping statistics.
- **Configuration:** every threshold lives in `config/*.json` and is parsed in [`src/config/settings.cpp`](src/config/settings.cpp). New parameters need a default, a parser entry and a test.

## License

MIT. See [`LICENSE`](LICENSE).
