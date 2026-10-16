# VECTRA — Complete Project Report (A to Z)

**Subject:** building the VECTRA C++ behavioral Forex prediction engine from an empty repository
**Scope:** everything created, changed, verified and repaired, in chronological order
**Language:** English
**Date of report:** 15 October 2026
**Version reported:** v0.1 (working tree, uncommitted)

---

## Table of contents

1. [Executive summary](#1-executive-summary)
2. [Requirements-to-deliverables mapping](#2-requirements-to-deliverables-mapping)
3. [Step 1 — Environment inspection](#3-step-1--environment-inspection)
4. [Step 2 — Skeleton and build configuration](#4-step-2--skeleton-and-build-configuration)
5. [Step 3 — Market data layer](#5-step-3--market-data-layer)
6. [Behavioral pressure engine](#6-behavioral-pressure-engine)
7. [Prediction engine](#7-prediction-engine)
8. [Risk and decision engine](#8-risk-and-decision-engine)
9. [Execution interface and simulated broker](#9-execution-interface-and-simulated-broker)
10. [Telemetry subsystem](#10-telemetry-subsystem)
11. [Backtesting and event replay](#11-backtesting-and-event-replay)
12. [Configuration subsystem and CLI](#12-configuration-subsystem-and-cli)
13. [Mathematical model as implemented](#13-mathematical-model-as-implemented)
14. [Sample dataset design](#14-sample-dataset-design)
15. [Step 7 — Test suite](#15-step-7--test-suite)
16. [Steps 8–9 — Build, failures, fixes, verification log](#16-steps-89--build-failures-fixes-verification-log)
17. [Step 9 — Full sample replay: actual results](#17-step-9--full-sample-replay-actual-results)
18. [Step 10 — Documentation deliverables](#18-step-10--documentation-deliverables)
19. [Design decisions and rationale](#19-design-decisions-and-rationale)
20. [Known limitations](#20-known-limitations)
21. [Next experiments](#21-next-experiments)
22. [Appendix A — File inventory](#22-appendix-a--file-inventory)
23. [Appendix B — Exact commands and exit statuses](#23-appendix-b--exact-commands-and-exit-statuses)
24. [Appendix C — Full test inventory (105 tests)](#24-appendix-c--full-test-inventory-105-tests)

---

## 1. Executive summary

An empty Git repository (one commit: `chore: initial commit`, containing only a
placeholder `README.md`) was turned into **VECTRA v0.1**, a working,
reproducible, dependency-free C++20 research engine for testing whether
observable price-behavior features can support short-horizon Forex
predictions.

Final state:

| Indicator | Value |
|---|---|
| C++ source/header files | 42 files, **6 407 lines** (headers + sources + tests) |
| Documentation | 6 markdown files, **977 lines**, plus this report |
| Unit tests | **105, all passing** (self-contained harness, no third-party test framework) |
| `ctest` entries | **2 / 2 passing** (`vectra_unit_tests`, `vectra_sample_replay`) |
| Compiler warnings | **0** (MSVC `/W4 /permissive-`, also configured for `-Wall -Wextra -Wpedantic`) |
| External dependencies | **0** (standard library only, including the JSON reader and test harness) |
| Third-party services / APIs | none (no AI, no ML, no LLM, no broker, no network at build time) |
| Sample replay | 6 000 rows → 5 395 valid events → 2 339 signals → 40 orders → 53 fills, reconciliation **matched** |
| Threads / lock-free structures | none (single-threaded by design and by requirement) |

The engine is honest by construction: it rejects bad data loudly, calls its
outputs *scores* rather than probabilities, reports neutral/no-trade as a
first-class outcome, marks every uncalibrated parameter `[U]`, and prints the
word "synthetic" next to every P&L figure produced from the sample dataset.

---

## 2. Requirements-to-deliverables mapping

| Spec section | Requirement | Delivered in | Status |
|---|---|---|---|
| 2 | C++20, CMake, std lib, minimal deps, Linux primary + Windows practical | `CMakeLists.txt`, dual build scripts | Done — verified on Windows/MSVC; scripts provided for Linux |
| 3A | Normalized market event, integer prices, validation of prices/timestamps/sequence/spread, reject or flag stale/malformed/duplicated/out-of-order | `include/vectra/market/*`, `src/market/*` | Done |
| 3A | No invented order-flow information | `behavior_engine.hpp` header comment + docs | Done (tick count never called volume) |
| 3B | Rolling state, displacement, change across windows, persistence, response vs activity, exhaustion, uncertainty/quality flags, observable vs latent separation | `behavior/`, `exhaustion.*` | Done |
| 3C | Continuation/reversal score, expected move over configurable horizon, confidence, validity; reject on bad data; scores not probabilities | `prediction/*` | Done |
| 3D | Min expected edge, cost filters, max position/exposure, daily loss, open orders, kill switch, safe degraded behavior | `risk/*` | Done |
| 3E | Abstract execution interface + simulated impl, ids, ack/reject/partial fill/cancel, states, duplicate protection, reconciliation | `execution/*` | Done — no live broker |
| 3F | Event replay, no look-ahead, bid/ask-aware fills, commission/slippage, latency/rejection assumptions, no midpoint fills, full recording, documented simulator blind spots | `backtest/*`, `data_limitations.md` §4 | Done |
| 3G | Timing per stage, invalid/stale counts, signal counts, gross/net P&L, drawdown, costs, median/p95/p99, `steady_clock` only | `telemetry/*`, report §6 | Done |
| 4 | Repository layout (dirs, docs, config, examples, scripts) | full tree, §22 | Done (+ extra `config/` module) |
| 5 | Midpoint, `r_h = log(mid_t/mid_{t-h})`, multiple windows, exhaustion inputs, documented rule, neutral outcome, configurable thresholds, uncalibrated values marked | `mathematical_model.md`, code | Done |
| 6 | Tests for validation, invalid/stale, ordering, rolling windows, zero volatility, exhaustion edges, no-look-ahead, order states, duplicate orders, risk/kill switch, deterministic replay, realized-P&L→risk wiring, fill-attempt-before-ack, daily-loss gating | 105 tests | Done — all required categories covered |
| 6 | Small synthetic dataset, explicitly not proof of edge | `examples/sample_ticks.csv` + generator + repeated disclaimers | Done |
| 6 | Report with events, valid predictions, signal distribution, gross/net, costs, drawdown, latency quantiles, limitations | `reports/sample_report.md` | Done — all 9 items present |
| 7 | Warnings, explicit errors, RAII, no unsafe concurrency, reproducible commands, sample config, no hidden failures, no profitability claims | project-wide | Done |
| 8 | 10-step workflow ending in a working, replayed, documented repo | §16–18 | Done |

---

## 3. Step 1 — Environment inspection

Commands used to discover the toolchain before writing any code:

```bash
pwd && ls -la && git log --oneline
cmake --version          # cmake version 4.1.1-msvc1
g++ --version            # not found
which clang++ cl g++ gcc ninja make python
ls "/c/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC"
find ... -name cl.exe    # ...\MSVC\14.50.35717\bin\Hostx64\x64\cl.exe
find ... -name vcvars*.bat
wsl -l -v                # only docker-desktop distro
```

Findings:

* **No GCC/Clang in PATH.** A Linux-first project still had to be built here,
  so the available MSVC toolset was chosen rather than stopping or asking.
* **Visual Studio Build Tools 18** with MSVC **19.50.35718.0** (toolset
  14.50.35717) — full C++20 support, including `<chrono>`, concepts,
  `std::optional`, structured bindings.
* **CMake 4.1.1** auto-detected the generator `Visual Studio 18 2026` and a
  Windows SDK `10.0.26100.0`.
* **Ninja** was available, but the Visual Studio generator needs no `vcvars`
  environment inside Git Bash, so it was used for reliability.
* **Python 3.14** available for the deterministic dataset generator.
* Docker/WSL present but deliberately unused — no container is required for a
  dependency-free C++ project.

Resulting portability story: CMakeLists enables `-Wall -Wextra -Wpedantic`
(and optional `-Werror`) for non-MSVC compilers, so the same tree builds
unchanged on Linux; `scripts/build.sh` covers that path.

---

## 4. Step 2 — Skeleton and build configuration

Created first:

* `CMakeLists.txt` — one top-level file:
  * `CMAKE_CXX_STANDARD 20`, `STANDARD_REQUIRED ON`, `EXTENSIONS OFF`
  * per-compiler warning blocks (`/W4 /permissive- /Zc:__cplusplus /EHsc`
    for MSVC; `-Wall -Wextra -Wpedantic` elsewhere) and an opt-in
    `VECTRA_WARNINGS_AS_ERRORS`
  * `vectra_core` static library (all components), `vectra` CLI executable,
    `vectra_tests` executable
  * `enable_testing()` with two tests: `vectra_unit_tests` (working directory
    = source tree so `config/default.json` is reachable) and
    `vectra_sample_replay` (runs the real binary over the sample CSV and
    fails on non-zero exit)
* `.gitignore` — build dirs, CMake artifacts, IDE files, logs (later adjusted
  so the delivered `reports/sample_report.md` is *not* ignored)
* `LICENSE` — MIT
* `tests/vectra_test.hpp` — the self-contained harness:
  * static registration (`VT_TEST(suite, name)` + `Registrar`)
  * `VT_ASSERT_TRUE/FALSE/NEAR/EQ/NE/STREQ/FAIL` macros that print
    file:line plus decoded values and throw a local `AssertFailed`
  * concept-guarded value formatting (works for enums and numeric types)
* `tests/test_main.cpp` — runner with `--list`, `--filter`, per-test
    PASS/FAIL lines, summary, and non-zero exit on any failure

**Why not GoogleTest:** dependency management would have required network
access or a package manager at configure time, which the brief discourages;
the harness maps 1:1 onto `TEST`/`EXPECT_*` so a later migration is mechanical
(documented in `docs/testing_plan.md` §1).

---

## 5. Step 3 — Market data layer

**Files:** `include/vectra/market/{market_event,validator,csv_reader}.hpp`,
`src/market/{market_event,validator,csv_reader}.cpp` (≈ 440 lines).

### Fixed-point price representation

```cpp
using Price = std::int64_t;
inline constexpr Price kPriceScale = 10'000'000;   // 1e-7
```

* `parse_price("1.10010") → 11'001'000` — implemented with pure integer
  arithmetic (no `atof`), overflow-checked, and **rejects** more than 7
  fractional digits instead of rounding silently.
* `format_price` renders ≥5 and ≤7 decimals with trailing-zero trimming
  (`11001000 → "1.10010"`).
* `mid_fixed() = bid + (ask - bid) / 2` — exact integer midpoint used for all
  comparisons; doubles appear only inside `log`/`exp`.
* Every validation comparison (`ask >= bid`, spread ceiling, equality) is an
  integer comparison, so no floating-point equality hazard exists.

### MarketEvent

Fields: `symbol`, `bid`, `ask`, `has_source_time` + `source_time_ns`
(provider clock), `local_receive_ns` (`steady_clock`, durations only),
`optional<uint64_t> sequence`, `Validity validity`, `EventFlags flags`
(bitmask, currently `StaleGap`), `note`.

`Validity` enumerates: `Ok, Malformed, WrongSymbol, NonPositivePrice,
NegativeSpread, WideSpread, Duplicate, OutOfOrder, Stale, FutureTimestamp`.

### Stateful validator

Check order (deliberate):

1. expected symbol → 2. prices > 0 → 3. `ask >= bid` → 4. spread ≤ max →
5. source time present (if required) → 6. sequence present (if required) →
7. future check against an optional independent reference clock →
8. sequence monotonicity (`< last` = OutOfOrder, `== last` = Duplicate) →
9. timestamp regression: lag > `stale_threshold` → `Stale`;
   lag > `reorder_tolerance` → `OutOfOrder`; equal timestamp without
   sequence → `Duplicate` → 10. forward gap > `stale_gap_threshold` →
   **flag only**, event stays valid.

Key invariant (tested by `Validator.RejectedEventsDoNotAdvanceStreamState`):
stream state (`last_time`, `last_seq`) advances **only for accepted events**,
so a rejected row can never corrupt later ordering decisions.

`reorder_tolerance_ns = 0` by default → strict ordering; staleness only
becomes reachable when tolerance is configured > 0, which is documented.

### CSV reader

`symbol,bid,ask,source_ts_ns[,sequence]`, optional header, `#` comments,
blank lines. Malformed rows are **delivered as errors**, not skipped, so the
replay counts them (`ParsedLine.ok == false` + reason + line number).

---

## 6. Behavioral pressure engine

**Files:** `behavior/{rolling_stats,behavior_engine,exhaustion}.hpp`,
`src/behavior/{behavior_engine,exhaustion}.cpp` (≈ 390 lines).

### RollingWindow (Welford with exact removal)

`push()` updates mean/M2 in O(1); eviction applies the exact removal formulas
`new_mean = (mean·n − x)/(n−1)` and `M2 −= (x − mean)(x − new_mean)`, clamping
round-off to zero. Sample variance (`n−1`) for `n ≥ 2`, otherwise 0. Capacity,
size, full/empty, `values()` access for sign-based persistence.

### BehaviorEngine

State: bounded deque of midpoints (longest window + 2), a `RollingWindow` of
consecutive-event log returns (volatility), a bounded deque of recent returns
(persistence), previous mid, previous short displacement.

Ingestion rule: an event with `validity != Ok` or a non-positive midpoint is
**not ingested** — features are reset with `invalid_event = true`, leaving the
rolling state untouched (tested by `Behavior.InvalidEventIsNotIngested`).

Configured windows (all counted in **events**, not wall-clock):
`window_short = 8`, `window_mid = 32`, `window_long = 128`,
`vol_window = 256`, `persistence_window = 16`, `min_vol_samples = 32`,
`min_volatility = 1e-8`, `spread_warn_ratio = 5e-4`. The constructor
canonicalizes windows (`short ≤ mid ≤ long`, floors at 1/2).

Features produced (observables only):

| Feature | Meaning |
|---|---|
| `disp_short/mid/long` | `log(mid_t / mid_{t−h})` per window |
| `disp_delta` | `r_short(t) − r_short(t−1)` (change across updates) |
| `disp_window_diff` | `r_short − r_mid` (change across windows) |
| `persistence` | mean sign agreement of recent returns with `sign(r_short)`, ∈ [−1,1] |
| `response_short/mid` | `r_h / h` — displacement **per tick** (explicitly not per unit volume) |
| `response_delta`, `response_fade` | response change and relative decline, clamped [0,1] |
| `volatility` | Welford sample std of per-event log returns |
| `z_short` | `r_short / (σ·√h_short)`, **only** when `σ > min_volatility` (never divides by zero) |
| `deceleration` | `clamp(sign(r_short)·(r_prev − r_short)/(σ·√h), −1, 1)` |
| `flags` | `insufficient_history, low_volatility, wide_spread, low_activity, stale_gap, invalid_event` + `blocks_signal()` and `describe()` |

### Exhaustion score

```
extension      = clamp(|z| / z_norm, 0, 1)                       [U] z_norm = 3
decel_component = clamp(deceleration, 0, 1)
fade_component = clamp(response_fade, 0, 1)
spread_baseline = 0.5 · spread_reference                          [U]
spread_component = clamp((spread_ratio − spread_baseline)/spread_baseline, 0, 1)
quality = clamp(0.6·spread_component + 0.2·stale_gap + 0.2·low_activity, 0, 1)   [U]
exhaustion = (0.35·extension + 0.30·decel + 0.20·fade + 0.15·quality) / Σw       [U]
```

**Design correction made during development:** the first draft used
`spread_ratio / reference` directly, which made a *normal* 1-pip spread add
≈0.017 to every score — enough to push steady trends across the continuation
threshold purely because of ordinary spread. Half-reference baseline
subtraction was introduced so normal spreads contribute exactly zero, and the
continuation threshold was moved from 0.35 to 0.45 so that the steady-trend
baseline (`w_extension = 0.35`) sits *inside* the neutral-safe region rather
than exactly on the boundary. Both changes are documented in
`docs/mathematical_model.md` §6–7 and covered by
`Exhaustion.NormalSpreadContributesNoQualityPenalty`.

Guard: all-zero weights return score 0 with an explanatory note instead of
dividing by zero (`Exhaustion.ZeroWeightsDoNotDivideByZero`).

---

## 7. Prediction engine

**Files:** `prediction/{prediction,scoring_predictor}.hpp`,
`src/prediction/scoring_predictor.cpp` (≈ 275 lines).

`Prediction` carries: `valid` (data gates), `kind` (Neutral/Continuation/
Reversal), `rejection` reason, `continuation_score`, `reversal_score`,
`confidence`, `exhaustion`, `direction`, `expected_move_log`, `horizon_events`,
`has_signal()`.

Data gates (each sets `valid = false` and returns early): `InvalidInput`,
`InsufficientData`, `StaleData`, `WideSpread`, `LowVolatility`.

Scoring (after gates):

```
gate         = clamp(|z| / min_extension_z, 0, 1)                 [U] 1.5
continuation = (1 − exhaustion) · gate · clamp(persistence, 0, 1)
reversal     =  exhaustion     · gate · (1 − clamp(persistence, 0, 1))
confidence   = quality · sample · gate          (uncalibrated composite)
```

Rule outcome table: unextended move → Neutral/`InsufficientEvidence`;
exhaustion ≥ 0.65 and persistence ≤ 0.50 → Reversal (direction against the
move); exhaustion ≤ 0.45 and persistence ≥ 0.25 → Continuation (direction
with the move); inside the band → Neutral/`NoRuleMatch`; a fired rule with
`confidence < 0.30` → Neutral/`LowConfidence`. Expected move =
`direction · strength · clamp(|r_short|, 0, 0.004)`.

Terminology discipline: the header states that nothing here is a calibrated
probability and that the name may only change after a calibration study.

---

## 8. Risk and decision engine

**Files:** `risk/risk_engine.hpp`, `src/risk/risk_engine.cpp` (≈ 360 lines).

`evaluate(prediction, event, now) → Decision{action, reason, detail, cost,
expected_move, edge, size_units}` with this exact check order:

1. `RiskDisabled` 2. `KillSwitch` 3. `DailyLossLimit`
4. `InvalidSignal` (prediction failed a data gate) 5. `NoSignal` (Neutral)
6. `DataUnreliable` (stale gate: no valid event within
   `data_stale_gate_ms`, or invalid-event ratio over the rolling
   `data_health_window` above `max_invalid_ratio`)
7. `CostTooHigh` (spread > `risk.max_spread`)
8. `InsufficientEdge` where
   `cost = spread/2 + commission + slippage`,
   `expected_move = expm1(|expected_move_log|)·mid`, `edge = expected_move − cost`,
   `edge ≥ min_expected_edge` required
9. `MaxOpenOrders` 10. `MaxOrdersPerDay` 11. `MaxPosition` (directional
   headroom) 12. `MaxExposure` (`|position|·mid ≤ max_exposure`)
13. **Allow** with `size = min(order_units, position headroom, exposure headroom)`.

State mutated exclusively by replay-driven `record_*` calls
(`record_data_point`, `record_position`, `record_open_orders`,
`record_order_placed`, `record_realized_pnl`, `on_new_day`,
`set_kill_switch`) — day rollover resets daily P&L and order counters, and
passing the *same* day index does not reset (tested). The realized-P&L feed is
wired from the replay's local bookkeeping and covered by
`Replay.RealizedPnlIsFedToRiskEngine` and
`Replay.DailyLossLimitBlocksOrdersAfterLoss`.

---

## 9. Execution interface and simulated broker

**Files:** `execution/{order,execution_gateway,simulated_broker}.hpp`,
`src/execution/{order,simulated_broker}.cpp` (≈ 690 lines).

### Order model

States: `Created, Submitted, Acknowledged, PartiallyFilled, Filled,
CancelPending, Cancelled, Rejected` with an explicit transition table
(`can_transition`) and `apply_transition(order, to, &error)` that leaves the
order untouched on refusal. Terminal states allow nothing;
`PartiallyFilled → PartiallyFilled` is the only legal self-transition.

### IExecutionGateway (the broker-agnostic seam)

`submit`, `cancel`, `on_market`, `drain_events`, `orders`, `fills`, `find`,
`account`, `duplicate_submissions` + a free `reconcile(local, broker,
tolerance)` returning `ReconciliationReport`.

### SimulatedBroker

* Seeded **splitmix64** PRNG — same seed + same event order ⇒ same draws.
* Latencies: ack 50 ms, fill 100 ms, cancel 100 ms (configurable).
* **Duplicate protection:** `client_order_id` → order-id map; a repeat returns
  the *original* order and emits a `Duplicate` event (counted, never a second
  order).
* Venue reject probability 0.05 `[U]` decided at submit, scheduled at ack time.
* Fills only after acknowledgement, only on valid quotes, and no earlier than
  `max(fill_latency, ack_latency)` after submit — an order submitted at event
  *t* can never fill at *t* (no look-ahead), and a fill scheduled before the
  acknowledgement is deferred rather than dropped.
* Bid/ask aware: buys lift `ask + slippage`, sells hit `bid − slippage`;
  marketable-limit checks re-schedule instead of filling.
* Partial fills with probability 0.30 `[U]` of the remaining size at fraction
  0.5 `[U]`, re-scheduled until terminal (loop test proves accumulation to
  exactly 100 units).
* Cancel: `* → CancelPending → Cancelled` at cancel latency; a fill racing a
  pending cancel is resolved by state (tested).
* Account: average-price bookkeeping with realized P&L on reducing/flipping
  fills, cash and commission tracking — independently re-implemented on the
  replay side for cross-checking.

---

## 10. Telemetry subsystem

**File:** `include/vectra/telemetry/latency.hpp` (header-only, ≈ 110 lines).

* `Stopwatch` — `std::chrono::steady_clock` only; used around feature
  computation, signal calculation, and decision evaluation.
* `LatencyRecorder` — stores raw nanosecond samples; `stats()` sorts a copy
  and computes **nearest-rank** median/p95/p99 (always a real observed sample,
  never interpolated), mean/min/max, and sets `available = false` when
  `count < min_samples` so the report prints `n/a (only N samples, need M)`
  instead of inventing quantiles.
* Clock-domain rule (documented in code and docs): `source_time_ns` and
  `local_receive_ns` are never subtracted from each other; there is no
  synchronization model in v0.1.

---

## 11. Backtesting and event replay

**Files:** `backtest/{replay_engine,report}.hpp`,
`src/backtest/{replay_engine,report}.cpp` (≈ 840 lines).

### Replay loop (per parsed row)

1. Parse → malformed rows counted and fed to the risk data-health gate as
   invalid, then skipped.
2. Validate → rejected rows counted by reason, gate fed `false`, stream state
   untouched.
3. Valid → stream clock, first/last timestamps, stale-gap counter, day
   rollover.
4. **`broker.on_market(event)` first** — already-pending orders become
   fillable on this quote; drain events into local accounting, telemetry,
   risk position/open-order state.
5. `behavior.update` — timed → `SignalRecord`.
6. `predict` — timed → recorded with kind/rejection/exhaustion/scores/z/
   persistence/quality flags.
7. `risk.evaluate` — timed → `DecisionRecord`.
8. If allowed → `submit` with idempotency key
   `SIG-<event_index>-<counter>`, record submit time for latency, drain.
9. Mark to market — **long marked at bid, short at ask** (conservative
   default), running peak/drawdown, bounded downsampled equity curve.

### Accounting

* `net` book: actual fill prices (incl. slippage) + commission.
* `gross` book: same prices minus slippage, no commission ⇒
  `net ≤ gross` always (asserted by `Replay.CostsDoNotInflateNetPnl`).
* `spread_cost_total` = Σ units × half-spread at fill time, reported
  **informationally** because the crossing is already inside both books.
* Final reconciliation: replay-computed account vs broker-internal account
  (tolerance 1e-6); mismatch ⇒ CLI exits 1.
* Determinism digest: FNV-1a over signal records, decision records, fills,
  net P&L and max drawdown.

### Report writer

Seven sections: market data, predictions/signal distribution, decisions/risk,
execution, simulated performance, latency table (µs: three local
`steady_clock` rows plus one venue-clock row, with `n/a` gating),
limitations & assumptions (8 bullets) + run digest. Every report carries the
disclaimer that scores are not calibrated probabilities and that synthetic
data cannot demonstrate an edge.

---

## 12. Configuration subsystem and CLI

**Files:** `config/{json,settings}.hpp`, `src/config/{json,settings}.cpp`
(≈ 780 lines), `src/main.cpp` (171 lines), `config/default.json` (95 lines).

* **Strict JSON reader** (recursive descent): objects, arrays, strings with
  escapes incl. `\uXXXX`, numbers with exponent (`1e-8` works), booleans,
  null, depth limit 64, position-carrying errors, trailing-garbage rejection.
  No third-party parser.
* **Settings overlay:** defaults → JSON file → typed fields for validation,
  behavior, exhaustion, prediction, risk, execution, backtest, telemetry;
  millisecond keys converted to nanoseconds; `instrument` copied into the
  validator's `expected_symbol`.
* **CLI:** `--data` (required), `--config`, `--report`, `--quiet`, `--help`.
  Exit codes verified: `0` success, `2` usage/IO errors, `1` reconciliation
  mismatch.

---

## 13. Mathematical model as implemented

Full derivations live in `docs/mathematical_model.md`; the core chain:

```
mid        = (bid + ask)/2                                   (integer)
r_h(t)     = log(mid(t) / mid(t−h))                          h in events
σ          = sample std of {x_i = log(mid_i/mid_{i−1})}      Welford, N=256
z          = r_h1 / (σ·√h1)                                  0 if σ ≤ min_vol
persistence= (1/K)·Σ sign(x_i)·sign(r_h1)                    K=16, ∈[−1,1]
decel      = clamp(sign(r_h1)·(r_prev − r_h1)/(σ·√h1), −1, 1)
response   = r_h / h                                        per tick
fade       = clamp(1 − |resp_h1|/(|resp_h2|+ε), 0, 1)
exhaustion = (0.35·ext + 0.30·dec⁺ + 0.20·fade + 0.15·quality)/1.0    [U]
cont_score = (1−exh)·gate·clamp(persistence,0,1)
rev_score  = exh·gate·(1−clamp(persistence,0,1))
confidence = (1 − 0.25·spread_ratio/ref − 0.20·low_activity)⁺ · min(N/64,1) · gate   [U]
edge       = expm1(|expected_move_log|)·mid − (spread/2 + commission + slippage)
```

Falsification criteria are written down (§10 of that document): no lift over
base rate on held-out data, exhaustion adds nothing beyond `z`, costs
eliminate the edge, or ±10 % threshold perturbations flip the sign.

---

## 14. Sample dataset design

`scripts/gen_sample_ticks.py` (111 lines, seed `20261015`) →
`examples/sample_ticks.csv`, **6 000 rows + header + 2 comments = 6 003 lines**,
100 ms spacing starting 2026-01-05T00:00:00Z, 5-decimal quotes.

Regime schedule (`drift, volatility, spread`):

| Ticks | Regime |
|---|---|
| 0–799 | quiet range (σ 2e-5) |
| 800–1599 | strong steady uptrend (drift +6e-5) |
| 1600–1799 | abrupt momentum flip (−7e-5) — reversal path |
| 1800–2399 | steady downtrend |
| 2400–2999 | choppy |
| 3000–3599 | wide spread 0.00060 — validator rejects (600 rows) |
| 3600–4199 | perfectly flat — zero-volatility path |
| 4200–4399 | elevated but legal spread 0.00045 — risk `CostTooHigh` path |
| 4400–4999 | recovery uptrend |
| 5000–5399 | second momentum flip |
| 5400–5999 | mild recovery |

Injected anomalies (each one is asserted by a test or visible in the report):

| Index | Anomaly | Resulting classification |
|---|---|---|
| 900 | exact duplicate of row 899 (same ts + seq) | `Duplicate` |
| 1500 | bid/ask swapped | `NegativeSpread` |
| 2000 | `not-a-price` bid | malformed row (parse error) |
| 2500 | symbol `GBPUSD` | `WrongSymbol` |
| 3000 | timestamp −50 ms | `OutOfOrder` |
| 4500+ | +10 s timestamp gap | 1 `StaleGap` flag → `StaleData` rejection |
| 4700 | repeated sequence number | `Duplicate` |

---

## 15. Step 7 — Test suite

Harness design and full inventory are in §24. Coverage by requirement
(spec §6 → tests):

| Required test area | Tests |
|---|---|
| Bid/ask validation | `Validator.*` (13) + `Price.*` (4) + `Csv.*` (4) |
| Invalid and stale data | `Validator.RejectsStaleTimestampBeyondThreshold`, `FlagsForwardGapWithoutRejecting`, `Behavior.StaleGapFlagPropagates`, `Prediction.StaleGapIsRejected`, `Risk.StaleDataStreamMarksDataUnreliable` |
| Out-of-order events | `RejectsOutOfOrderSequence`, `RejectsBackwardTimestamps`, `RejectedEventsDoNotAdvanceStreamState`, `Replay.CountsEveryAnomalyClass` |
| Rolling-window calculations | `RollingWindow.*` (4) + `Behavior.*` (8) |
| Zero / near-zero volatility | `Behavior.ZeroVolatilityIsFlagged`, `Prediction.ZeroVolatilityIsRejected`, `RollingWindow.HandlesConstantInputAndSingleSample` |
| Exhaustion-score edge cases | `Exhaustion.*` (6) |
| No-look-ahead | `Replay.NoLookAheadOnPrefix` (bit-identical prefix records) |
| Order state transitions | `OrderStates.*` (3) |
| Duplicate-order prevention | `Broker.DuplicateClientOrderIdIsProtected` |
| Risk limits and kill switch | `Risk.*` (14), `Replay.KillSwitchBlocksAllOrders` |
| Deterministic replay | `Replay.DeterministicReplayProducesIdenticalDigest` |
| Report completeness | `Report.*` (2) |
| Telemetry quantiles | `Telemetry.*` (6) |
| Config loading | `Json.*` (4), `Settings.*` (4) |

Test data helpers (`tests/test_util.hpp`): `make_event`, `make_quote`,
`make_walk` (LCG-based deterministic walk — identical on every standard
library), `to_csv`.

---

## 16. Steps 8–9 — Build, failures, fixes, verification log

Chronological record of everything that failed and how it was repaired. No
test was skipped, weakened, or suppressed to make a check pass; no warning
flags were removed.

| # | Failure | Root cause | Repair |
|---|---|---|---|
| 1 | CMake generate error: `Cannot find source file: tests/test_market.cpp` | CMake ran before the test files existed (expected, listed in workflow order) | Wrote all nine test files, re-configured |
| 2 | `C2065: 'Order' : identifiant non déclaré` in `order.hpp` | `apply_transition(Order&, ...)` declared **before** `struct Order` | Moved the declaration below the struct definition |
| 3 | `C2819: SimulatedBroker has no operator->` (header + cpp) | Mixed `->` (pointer syntax) and `.` on a value member | Standardized all call sites on `.` |
| 4 | `C2660 hash_double : function does not take 1 arguments` ×2 | Cascading MSVC error from (3): the loop variable type was unresolved | Disappeared with (3); confirmed no real signature bug |
| 5 | `C2039: 'side' is not a member of OrderEvent` | Local P&L bookkeeping needs the side from the fill event | Added `Side side` to `OrderEvent` and set it on `Filled`/`PartialFill` emissions |
| 6 | `C2660 make_quote : does not take 5 arguments` in `test_market.cpp` | Wrong helper overload for the wrong-symbol case | Switched to `make_event(bid, ask, ts, seq, symbol)` |
| 7 | `C4100: 'source_name' unreferenced parameter` (warning) | Parameter intentionally unused by the loop | Explicit `(void)source_name;` with a comment — not a suppression flag |
| 8 | Design review before tests: continuation threshold sat exactly on the steady-trend score baseline; normal spread inflated the score | `w_extension = 0.35 = threshold`; no spread baseline | Threshold → 0.45; half-reference spread baseline introduced (§6) |
| 9 | Test-design errors found by reasoning before the first run: ack/fill timing assertions used timestamps before the ack was due; partial-fill loop would never reach 100 %; account test double-counted slippage; `CancelPending → PartiallyFilled` is legal so that assertion would be false | simulation semantics | Reordered event timestamps (40 ms → 99.999 ms → 100 ms); replaced with an accumulate-until-terminal loop; set `slippage = 0` for the accounting test; asserted `CancelPending → Acknowledged` instead |
| 10 | **Report defect:** "Estimated spread cost" printed `0.00` on a run with 53 fills | `apply_local_fill` never updated `stats_.spread_cost_total` and `handle_order_events` discarded `bid/ask` (`(void)bid;`) | Threaded `half_spread` from the market event into `apply_local_fill`; rebuilt, retested, re-ran replay → **10.99** |
| 11 | `.gitignore` excluded `reports/`, which README links as a deliverable | blanket ignore of generated reports | Removed `reports/` from ignore (kept `*.log`), verified with `git check-ignore` (exit 1 = not ignored) |
| 12 | README showed only the Windows test-binary path | platform accuracy | Added the Linux `./build/vectra_tests` form |

Verification commands and **actual exit statuses**:

```text
cmake -S . -B build                                → configure OK (VS 18 2026, MSVC 19.50.35718)
cmake --build build --config Release               → 0 errors, 0 warnings; produced
                                                     vectra_core.lib, vectra.exe, vectra_tests.exe
./build/Release/vectra_tests.exe                   → "tests run: 105, passed: 105, failed: 0", exit 0
ctest --test-dir build -C Release --output-on-failure
                                                    → 100% tests passed, 0 failed out of 2, exit 0
cmake -S . -B build-verify (clean tree) + build    → grep -c "error|warning" = 0
ctest --test-dir build-verify -C Release           → 100% tests passed (2/2), exit 0
vectra --help                                      → usage, exit 0
vectra --data nope.csv                             → error, exit 2
vectra --bogus                                     → error, exit 2
vectra --config ... --data ... --quiet             → one-line summary, exit 0
vectra (normal run with --report)                  → report written, exit 0
git check-ignore reports/sample_report.md          → exit 1 (not ignored)
```

After the last source repair (#10) a full rebuild **and** `ctest` **and** a
clean-directory build were rerun; only documentation/ignore-file edits
followed, which do not affect compilation.

---

## 17. Step 9 — Full sample replay: actual results

Command:

```bash
./build/Release/vectra.exe --config config/default.json \
    --data examples/sample_ticks.csv --report reports/sample_report.md
```

Console output (verbatim values):

```text
VECTRA replay complete
  data:            examples/sample_ticks.csv
  config:          config/default.json
  events valid:    5395 (rejected 604, malformed 1)
  predictions:     5395, usable signals: 2339
  decisions:       40 allowed, 5355 rejected
  orders:          40 submitted, 53 fills
  gross P&L:       3795.68   net P&L: 3791.70
  max drawdown:    899.91 (0.861%)
  reconciliation:  matched
  report:          reports/sample_report.md
```

Detailed report figures:

| Section | Values |
|---|---|
| Market data | 6 000 lines, 5 999 parsed, 5 395 valid, 604 rejected, 1 malformed, 2 stale gaps; reasons: WideSpread=600, Duplicate=2, NegativeSpread=1, WrongSymbol=1; duration 10.16 minutes |
| Predictions | 5 395 total; 2 339 usable (43.355 %); kinds: Continuation=2 333, Neutral=3 056, Reversal=6; rejections: InsufficientEvidence=1 824, NoRuleMatch=758, LowVolatility=344, InsufficientData=128, StaleData=2, None=2 339 |
| Decisions | allowed 40 / rejected 5 355: NoSignal=2 582, InsufficientEdge=1 067, InvalidSignal=474, MaxPosition=684, MaxOrdersPerDay=433, CostTooHigh=115; kill switch inactive; min edge 0.000150; max position 50 000; max daily loss 250; max open orders 3 |
| Execution | 40 submitted, 40 accepted, 0 venue rejects, 0 cancels, 0 duplicates, 53 fills (13 partial), reconciliation **matched** |
| Performance | initial 100 000.00 → final 103 791.70; realized 3 581.47; gross 3 795.68; net 3 791.70; commission 1.99; slippage 1.99; spread cost 10.99; max drawdown 899.91 (0.861 %) |
| Latency (µs) | event 0.2 / 0.3 / 0.4 (n=5 395); signal 0.3 / 1.0 / 1.1 (n=5 395); decision 0.2 / 1.5 / 1.6 (n=5 395); simulated order submit→ack 100 000 / 100 000 / 100 000 (n=40) — the local rows are `steady_clock` and vary per run; the order row is on the venue clock and quantized by the 100 ms quote interval, explained in the report |
| Digest | `bc06ff21b93aecfb` (FNV-1a) |

Interpretation discipline: the P&L comes from a synthetic path and is labelled
a *pipeline smoke test* in both the console and the report.

---

## 18. Step 10 — Documentation deliverables

| File | Lines | Content |
|---|---|---|
| `README.md` | 156 | overview, component table, build/test/run commands, actual results, structure, design rules, next experiment |
| `docs/architecture.md` | 127 | ASCII data-flow diagram, component table, key decisions, ownership/error handling, extension points |
| `docs/mathematical_model.md` | 219 | every formula with symbols, `[U]` markers, decision rule table, calibration status, falsification criteria |
| `docs/data_limitations.md` | 109 | what quotes are/aren't, behavioral-attribution ban, clock domains, simulator blind spots, synthetic-data warning, statistical caveats, scope statement |
| `docs/testing_plan.md` | 102 | principles, coverage map of all 105 tests, run instructions, no-look-ahead & determinism test details, real-data validation plan, explicit non-goals |
| `reports/sample_report.md` | 99 | generated 7-section replay report |
| `docs/full_project_report.md` | this file | complete A-to-Z account of the build, verification and results |
| `docs/audit_v0.1.md` | 264 | evidence-based v0.1 audit: findings by severity, reproducible evidence, fixes and regression tests, verified-facts table, go/no-go decision |
| `config/default.json` | 95 | every threshold with inline `_comment` notes; uncalibrated values called out |

---

## 19. Design decisions and rationale

1. **Integer prices.** Equality/ordering on `int64` removes an entire class of
   floating-point bugs from validation, where a bug would silently admit bad
   data.
2. **Validator state advances only on acceptance.** Otherwise one rejected row
   would corrupt every subsequent ordering decision.
3. **Broker processes the current quote before the prediction at that quote.**
   Pending orders legitimately fill on this quote; a *new* order submitted now
   can only fill after its latency — this ordering is what makes the loop
   look-ahead free while still being realistic.
4. **Prediction and decision are separate types.** `Prediction` knows nothing
   about positions; `Decision` knows nothing about scoring. The risk engine
   can veto for 13 distinct reasons without touching the model.
5. **Gateway interface, one implementation.** A future authorized broker
   adapter slots in without touching risk/backtest code.
6. **Independent local vs broker account bookkeeping.** Both are fed the same
   fill stream by *separate code paths*, so reconciliation catches
   double-counting or lost-event bugs rather than being a tautology.
7. **Header-only JSON and test harness.** Zero dependencies at configure time
   — reproducible on a machine with only CMake and a compiler.
8. **Nearest-rank quantiles with an availability gate.** Never interpolate,
   never fabricate p99 from 12 samples.
9. **Conservative marking** (long at bid, short at ask) so the report can never
   flatter the strategy.
10. **Rejection counters instead of silent skipping.** Every rejected row and
    every refused signal appears in the report.

---

## 20. Known limitations

* Synthetic sample data proves pipeline correctness only — no edge evidence.
* Quotes only: no traded volume, no book, no aggressor side, no positioning
  data; tick count is activity, never volume.
* No behavioral attribution: "forced liquidation", "stop cascades" and "retail
  positioning" are never asserted.
* Simulated venue omits queue position, market impact, depth, partial-fill
  selection bias, latency jitter, swaps/rollover, margin and financing.
* All thresholds/weights are `[U]` uncalibrated starting values; scores are
  not probabilities.
* Windows are tick-counted, so results depend on the source's tick rate.
* Single instrument per run; cross-asset effects untested.
* Clock domains are not synchronized; cross-clock duration math is forbidden
  by design.
* No performance-regression harness, no parser fuzzing yet (both listed as
  planned in `testing_plan.md` §7).

---

## 21. Next experiments (smallest first)

1. **Real-data ingestion dry run:** convert one day of real ticks to CSV and
   inspect only validation statistics (rejects, duplicates, crossed quotes,
   ordering violations) before touching any model.
2. **Chronological split + pre-registered threshold grid:** development /
   validation / out-of-sample, never tuning on the final period.
3. **Baseline comparison:** always-neutral, always-continue, and a random
   signal with the same trade count, with block-bootstrap intervals.
4. **Parser fuzzing:** random-byte corpus for CSV/JSON must never crash or UB.
5. **Calibration study:** only if signals carry information, decide whether
   the scores may ever be called probabilities.

---

## 22. Appendix A — File inventory

```
VECTRA/
├── CMakeLists.txt                     build, warnings, 2 ctest entries
├── README.md  LICENSE  .gitignore
├── config/default.json                all thresholds (95 lines)
├── docs/
│   ├── architecture.md                127
│   ├── mathematical_model.md          219
│   ├── data_limitations.md            109
│   ├── testing_plan.md                102
│   ├── full_project_report.md        this document
│   └── audit_v0.1.md                  264 (audit review + go/no-go)
├── include/vectra/
│   ├── backtest/   replay_engine.hpp (202), report.hpp (17)
│   ├── behavior/   behavior_engine.hpp (136), exhaustion.hpp (48), rolling_stats.hpp (81)
│   ├── config/     json.hpp (69), settings.hpp (30)
│   ├── execution/  execution_gateway.hpp (61), order.hpp (116), simulated_broker.hpp (91)
│   ├── market/     csv_reader.hpp (40), market_event.hpp (104), validator.hpp (62)
│   ├── prediction/ prediction.hpp (84), scoring_predictor.hpp (26)
│   ├── risk/       risk_engine.hpp (127)
│   └── telemetry/  latency.hpp (109)
├── src/
│   ├── backtest/   replay_engine.cpp (405), report.cpp (228)
│   ├── behavior/   behavior_engine.cpp (194), exhaustion.cpp (66)
│   ├── config/     json.cpp (406), settings.cpp (216)
│   ├── execution/  order.cpp (79), simulated_broker.cpp (419)
│   ├── market/     csv_reader.cpp (170), market_event.cpp (116), validator.cpp (129)
│   ├── prediction/ scoring_predictor.cpp (165)
│   ├── risk/       risk_engine.cpp (230)
│   └── main.cpp    (171)
├── tests/          vectra_test.hpp (128), test_main.cpp (77), test_util.hpp (78),
│                   test_market (243), test_behavior (299), test_prediction (184),
│                   test_risk (195), test_execution (275), test_backtest (235),
│                   test_telemetry (83), test_config (140)
├── examples/sample_ticks.csv          6 003 lines (synthetic)
├── reports/sample_report.md           generated replay report
└── scripts/ build.sh (14), build.bat (17), replay_sample.sh (22), gen_sample_ticks.py (111)

C++ total: 6 407 lines across 42 files.
```

---

## 23. Appendix B — Exact commands and exit statuses

```bash
# configure + build (Windows, VS 18 generator)
cmake -S . -B build
cmake --build build --config Release

# unit tests (also runnable directly)
./build/Release/vectra_tests.exe --list
./build/Release/vectra_tests.exe --filter Prediction
ctest --test-dir build -C Release --output-on-failure

# full replay with report
./build/Release/vectra.exe --config config/default.json \
    --data examples/sample_ticks.csv --report reports/sample_report.md

# Linux / macOS equivalents
./scripts/build.sh              # configure + build + ctest
./scripts/replay_sample.sh      # replay into reports/sample_report.md

# regenerate the synthetic dataset (deterministic)
python scripts/gen_sample_ticks.py
```

Exit statuses observed: build `0`; `vectra_tests` `0` (105/105);
`ctest` `0` (2/2); replay `0`; missing `--data` `2`; unknown flag `2`;
reconciliation failure would be `1` (path tested by unit tests, not by
corrupting the shipped CSV).

---

## 24. Appendix C — Full test inventory (105 tests)

**Price (4)** — ParsesStandardForexQuotes · RejectsMalformedInput ·
FormattingRoundTrips · IntegerMidpointIsExact

**Validator (13)** — AcceptsHealthyQuote · RejectsCrossedAndNonPositiveQuotes ·
RejectsWideSpread · RejectsWrongSymbol · RejectsMissingTimestampWhenRequired ·
RejectsDuplicateSequence · RejectsDuplicateTimestampWithoutSequence ·
RejectsOutOfOrderSequence · RejectsBackwardTimestamps ·
RejectsStaleTimestampBeyondThreshold · FlagsForwardGapWithoutRejecting ·
RejectsFutureTimestampAgainstReferenceClock · RejectedEventsDoNotAdvanceStreamState

**Csv (4)** — ParsesValidRowWithSequence · ParsesRowWithoutSequence ·
ReportsMalformedRowsInsteadOfSkipping · SkipsHeaderAndCommentLines

**RollingWindow (4)** — ComputesMeanAndSampleVariance · EvictsOldestValues ·
HandlesConstantInputAndSingleSample · EmptiesCleanly

**Behavior (8)** — FlagsInsufficientHistoryBeforeWindowsFill ·
ComputesDisplacementsWithCorrectSign · NegativeTrendProducesNegativeDisplacement ·
ZeroVolatilityIsFlagged · WalkProducesFiniteBoundedFeatures ·
InvalidEventIsNotIngested · StaleGapFlagPropagates · WideSpreadIsFlagged

**Exhaustion (6)** — InvalidFeaturesProduceInvalidResult ·
ScoreStaysWithinUnitInterval · NormalSpreadContributesNoQualityPenalty ·
StaleGapRaisesQualityPenalty · ZeroWeightsDoNotDivideByZero ·
StaticMoveScoresLowAndStoppingMoveScoresHigh

**Prediction (12)** — InsufficientHistoryIsRejected · InvalidEventFlagIsRejected ·
StaleGapIsRejected · WideSpreadIsRejected · ZeroVolatilityIsRejected ·
PersistentExtendedMoveGivesContinuationCandidate ·
ExhaustedFadingMoveGivesReversalCandidate · UnextendedMoveIsNeutral ·
ScoresInNeutralBandStayNeutral · LowConfidenceSuppressesAnOtherwiseValidSignal ·
DeterministicForIdenticalFeatures · ExpectedMoveIsCapped

**Risk (14)** — AllowsSaneSignalWithFreshData · KillSwitchBlocksEverything ·
DisabledEngineRejects · DailyLossLimitStopsNewRiskAndResetsOnNewDay ·
InvalidAndNeutralPredictionsAreRejected · InsufficientEdgeIsRejected ·
WideSpreadIsRejectedAsCost · MaxPositionLimitIsEnforced ·
MaxExposureLimitIsEnforced · PositionHeadroomCapsOrderSize ·
OpenOrderAndDailyOrderLimitsAreEnforced · HighInvalidRatioMarksDataUnreliable ·
StaleDataStreamMarksDataUnreliable · DataHealthStartsReliable

**OrderStates (3)** — LegalTransitionsAreAllowed · IllegalTransitionsAreRefused ·
ApplyTransitionLeavesOrderUntouchedOnFailure

**Broker (10)** — SubmitCreatesSubmittedOrder · DuplicateClientOrderIdIsProtected ·
InvalidRequestIsRefused · VenueRejectionFollowsLatency ·
FillHappensAfterLatencyAtTheAsk · FillAttemptBeforeAckIsNotDropped ·
PartialFillLeavesOrderWorking ·
RepeatedFillsAccumulateToFullFill · CancelLifecycleWorks ·
InvalidQuotesDoNotDriveFills

**Account (2)** — RealizedPnlUsesAveragePrice · ReconciliationDetectsMismatches

**Replay (9)** — NoLookAheadOnPrefix · DeterministicReplayProducesIdenticalDigest ·
CountsEveryAnomalyClass · KillSwitchBlocksAllOrders ·
CostsDoNotInflateNetPnl · SignalCountsMatchRecords ·
SampleStreamYieldsContinuationSignals · RealizedPnlIsFedToRiskEngine ·
DailyLossLimitBlocksOrdersAfterLoss

**Report (2)** — ContainsAllRequiredSections ·
MarksUnavailableLatencyAsNotApplicable

**Telemetry (6)** — NearestRankQuantilesOnKnownSample ·
SmallSampleIsReportedAsUnavailable · EnoughSamplesBecomeAvailable ·
EmptyRecorderReportsNothing · StopwatchMeasuresWithSteadyClock ·
UnsortedInputIsSortedBeforeQuantiles

**Json (4)** — ParsesNestedObjectsAndTypedValues · MissingKeysFallBackToDefaults ·
RejectsMalformedInputWithPosition · RejectsNonObjectTopLevelForSettings

**Settings (4)** — DefaultsAreSane · OverlayMapsJsonKeysToEngineConfig ·
MissingConfigFileReportsError · DefaultConfigFileParses

---

*End of report. VECTRA v0.1 is a working, reproducible research engine; live
trading and profitability claims remain explicitly out of scope.*
