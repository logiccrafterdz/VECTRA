# VECTRA

**VECTRA** is an experimental, rule-based, mathematical Forex prediction
engine written in C++20. It models *observable* market phenomena —
short-horizon price displacement, movement persistence, per-tick price
response, and a movement-exhaustion hypothesis — and tests whether transparent
scoring rules over those observables produce signals that survive costs in an
event-driven replay.

It contains **no machine learning, no neural networks, no LLM calls, no
traditional technical-indicator strategy, and no profitability claims**. Every
threshold is explicitly marked `[U]` = *uncalibrated*, every signal can be
rejected, and the engine is built to report negative results honestly.

> **Status: v0.1 research engine.** Live trading is out of scope. The sample
> dataset is synthetic and only proves the pipeline works — it is not
> evidence of a trading edge.

## What is implemented

| Component | Summary |
|---|---|
| **Market data layer** | Normalized `MarketEvent` with fixed-point integer prices (1e-7, no float equality bugs), bid/ask/spread/timestamp/sequence validation, rejection of malformed, duplicated, out-of-order, stale, future and wide-spread quotes |
| **Behavioral pressure engine** | Rolling Welford windows over mid-price log returns: displacement across nested event windows, displacement change, movement persistence, price response per observable tick, response fade, volatility-normalized z, deceleration, data-quality flags |
| **Prediction engine** | Transparent scoring: continuation / reversal / neutral candidates, uncalibrated scores in `[0,1]`, expected move, confidence, and explicit rejection reasons (insufficient data, invalid input, stale, wide spread, zero volatility, low confidence, no rule match) |
| **Risk & decision engine** | Min expected-edge threshold, spread/commission/slippage cost model, max position, max exposure, max daily loss, max open orders, max orders/day, rolling invalid-ratio data-health gate, stale-data gate, **kill switch** |
| **Execution** | Abstract `IExecutionGateway` + `SimulatedBroker`: order ids, ack/reject/partial-fill/cancel events, explicit guarded state machine, duplicate `client_order_id` protection, account reconciliation |
| **Backtesting** | Single-pass event replay, no look-ahead (tested), bid/ask-aware fills with latency, commission and slippage, per-event records of signals/decisions/orders/fills, gross vs net P&L, drawdown, deterministic digest |
| **Telemetry** | `steady_clock` timing per stage, median/p95/p99 (only when enough samples), invalid/stale/signal/rejection counters, markdown report |

## Build and test

Requirements: CMake ≥ 3.20 and a C++20 compiler (MSVC, GCC or Clang).
No third-party dependencies — not even for tests.

```bash
# Linux / macOS
./scripts/build.sh                 # configure + build + ctest

# Windows (Visual Studio generator; verified with VS 18 Build Tools / MSVC 19.50)
scripts\build.bat
```

Equivalent manual commands:

```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Run a filtered subset:

```bash
./build/Release/vectra_tests.exe --list      # Windows (VS generator)
./build/Release/vectra_tests.exe --filter Risk
./build/vectra_tests --filter Risk           # Linux/macOS (single-config build)
```

## Run a replay

```bash
./build/Release/vectra.exe \
    --config config/default.json \
    --data    examples/sample_ticks.csv \
    --report  reports/sample_report.md
```

Regenerate the synthetic dataset (deterministic seed):

```bash
python scripts/gen_sample_ticks.py
```

## Actual results (v0.1, sample replay, this machine)

Build: MSVC 19.50 (`/W4 /permissive-`), CMake 4.1, Release. **105/105 unit
tests pass**, both `ctest` entries pass (`vectra_unit_tests`,
`vectra_sample_replay`).

Replay of `examples/sample_ticks.csv` (6 000 synthetic rows, 10 min of model
time, `config/default.json`):

| Metric | Value |
|---|---|
| Valid events / rejected / malformed | 5 395 / 604 / 1 |
| Rejection reasons | WideSpread=600, Duplicate=2, NegativeSpread=1, WrongSymbol=1 |
| Stale gaps flagged | 2 |
| Event-level predictions | 5 395 |
| Usable signals | 2 339 (Continuation 2 333, Reversal 6, Neutral 3 056) |
| Signal rejections | InsufficientEvidence 1 824, NoRuleMatch 758, LowVolatility 344, InsufficientData 128, StaleData 2 |
| Decisions allowed / rejected | 40 / 5 355 (NoSignal 2 582, InsufficientEdge 1 067, InvalidSignal 474, MaxPosition 684, MaxOrdersPerDay 433, CostTooHigh 115) |
| Orders submitted / fills (partial) | 40 / 53 (13) |
| Account reconciliation | matched |
| Gross P&L / Net P&L | 3 795.68 / 3 791.70 |
| Commission / slippage / spread cost | 1.99 / 1.99 / 10.99 |
| Max drawdown | 899.91 (0.861 %) |
| Latency median / p95 / p99 (µs) | event 0.2 / 0.3 / 0.4 — signal 0.3 / 1.0 / 1.1 — decision 0.2 / 1.5 / 1.6 (steady_clock; varies per run) |
| Simulated order submit→ack (venue clock) | 100 000 µs (event-quantized: quotes arrive every 100 ms) |

The P&L figures come from a **synthetic** path and are a pipeline smoke test.
They are reported because hiding them would be dishonest; they carry no
information about real-market profitability. Full report:
[reports/sample_report.md](reports/sample_report.md).

## Repository structure

```
VECTRA/
  CMakeLists.txt
  README.md  LICENSE  .gitignore
  docs/
    architecture.md         data flow, module responsibilities, extension points
    mathematical_model.md   every formula, every [U] uncalibrated parameter
    data_limitations.md     what the data and the simulator cannot support
    testing_plan.md         coverage map + validation plan for real data
    full_project_report.md  complete A-to-Z build, verification and results report
    audit_v0.1.md           evidence-based audit: findings, fixes and go/no-go
  config/default.json       all thresholds (UNCALIBRATED values marked)
  include/vectra/
    market/ behavior/ prediction/ risk/ execution/ backtest/ telemetry/ config/
  src/                      implementations mirroring include/, + main.cpp
  tests/                    self-contained harness + one file per component
  examples/sample_ticks.csv synthetic dataset with injected anomalies
  scripts/                  build.sh, build.bat, replay_sample.sh, gen_sample_ticks.py
  reports/sample_report.md  generated replay report
```

## Design rules

1. **Observable vs latent.** Code only ever computes quote observables;
   "forced liquidation", "stop cascades" and "retail positioning" are never
   asserted — the data cannot support them ([docs/data_limitations.md](docs/data_limitations.md)).
2. **Scores, not probabilities.** Nothing is called a probability until a
   calibration study on held-out data says so.
3. **Signals can be rejected.** Insufficient data, invalid input, stale
   quotes, wide spread, zero volatility and low confidence all refuse to
   produce a signal; neutral is a first-class outcome.
4. **Prediction ≠ trading.** The risk engine can veto any prediction, and the
   kill switch vetoes everything.
5. **Deterministic and reproducible.** Seeded PRNG, single pass, digest
   verified by test; `steady_clock` for durations, never cross-clock math.
6. **No hidden failures.** Tests, validation rejects and reconciliation
   mismatches all exit non-zero.

## Next experiment

Ingest real tick data (Dukascopy/TrueFX-style CSV), split chronologically into
development / validation / out-of-sample **before** touching any threshold,
then run the pre-registered threshold grid and compare against an
always-neutral baseline with block-bootstrap confidence intervals — see
[docs/testing_plan.md](docs/testing_plan.md) §6.

## License

MIT — see [LICENSE](LICENSE).
