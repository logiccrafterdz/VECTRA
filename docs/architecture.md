# VECTRA architecture (v0.1)

VECTRA is a single-process, single-threaded, event-driven research engine. There
are no threads, no lock-free structures and no shared mutable state: every
component is owned by the replay loop and mutated explicitly. This is a
deliberate starting point — parallelism is deferred until measurements justify
it.

## Data flow

```
                 ┌──────────────────────────────────────────────────────────┐
 CSV ticks ────▶ │ market: parse ─▶ validate ─▶ MarketEvent (fixed-point)   │
 (or a feed)     └───────────────┬──────────────────────────────────────────┘
                                 │ valid events only
        ┌────────────────────────▼───────────────────────────────────────┐
        │ 1. execution.SimulatedBroker.on_market(event)                  │
        │    drives simulated time: acks, rejects, fills, cancellations  │
        └────────────────────────┬───────────────────────────────────────┘
                                 │
        ┌────────────────────────▼───────────────────────────────────────┐
        │ 2. behavior.BehaviorEngine.update(event)                       │
        │    rolling state ─▶ BehaviorFeatures (observables only)        │
        └────────────────────────┬───────────────────────────────────────┘
                                 │
        ┌────────────────────────▼───────────────────────────────────────┐
        │ 3. prediction.ScoringPredictor.predict(features)               │
        │    data gates ─▶ exhaustion score ─▶ kind + scores + validity  │
        └────────────────────────┬───────────────────────────────────────┘
                                 │
        ┌────────────────────────▼───────────────────────────────────────┐
        │ 4. risk.RiskEngine.evaluate(prediction, event, now)            │
        │    kill switch, data health, cost/edge, position/exposure,     │
        │    daily loss, order limits ─▶ Decision (Allow/Reject + size)  │
        └────────────────────────┬───────────────────────────────────────┘
                                 │ Allow only
        ┌────────────────────────▼───────────────────────────────────────┐
        │ 5. execution.IExecutionGateway.submit(request)                 │
        │    dedupe by client_order_id ─▶ simulated venue with latency   │
        └────────────────────────┬───────────────────────────────────────┘
                                 │
                    telemetry counters + latency recorders + records
                                 │
                          backtest report (markdown)
```

Step 1 intentionally runs **before** the prediction at the same timestamp: an
order submitted earlier is legitimately fillable on this quote, while an order
submitted *at* this quote can only fill after its configured latency (never on
the same event), which is what keeps the loop free of look-ahead.

## Components

| Namespace | Responsibility | Key files |
|---|---|---|
| `vectra::market` | Normalized events, fixed-point prices, validation, CSV | `include/vectra/market/*` |
| `vectra::behavior` | Rolling windows, observable features, exhaustion score | `include/vectra/behavior/*` |
| `vectra::prediction` | Data gates, scoring rules, signal/rejection | `include/vectra/prediction/*` |
| `vectra::risk` | Limits, kill switch, data-health gate, sizing | `include/vectra/risk/*` |
| `vectra::execution` | Order model, gateway interface, simulated broker | `include/vectra/execution/*` |
| `vectra::backtest` | Replay loop, statistics, report writer | `include/vectra/backtest/*` |
| `vectra::telemetry` | steady_clock timing, quantile statistics | `include/vectra/telemetry/*` |
| `vectra::config` | Strict JSON reader, typed settings overlay | `include/vectra/config/*` |

## Key design decisions

**Fixed-point prices.** `Price` is `int64_t` scaled by 1e-7. Comparisons
(`ask >= bid`, equality, ordering, spread) are exact integers; doubles only
appear where a transcendental (log, exp) is required. Parsers reject quotes
with more precision than the representation holds instead of rounding them.

**Validation is a gate, not a suggestion.** `EventValidator` keeps the last
accepted timestamp/sequence and only advances that state for accepted events,
so a rejected row can never poison ordering checks. Invalid events are counted
by reason and never reach the behavioral engine; the engine additionally
refuses to ingest any event whose validity is not `Ok`.

**Prediction and trading are separate objects.** `Prediction` (scores,
validity, rejection reason) knows nothing about positions or limits;
`Decision` (allow/reject, size, edge, cost) knows nothing about how scores are
computed. A prediction can be perfectly valid and still be rejected for a
dozen independent risk reasons.

**Execution behind an interface.** `IExecutionGateway` is what the risk and
backtest layers talk to. `SimulatedBroker` is the only implementation in
v0.1; a live broker adapter can be added later without touching any other
component (it must implement submit/cancel/on_market/drain_events/account).

**Determinism.** No wall-clock dependency enters the decision path: the
simulated venue uses a seeded splitmix64 PRNG, event order defines PRNG draw
order, and P&L is marked on observed quotes. Two runs of the same input
produce the same FNV-1a digest (asserted by a test).

## Ownership and error handling

- Value semantics everywhere; `std::vector`/`std::deque` own their storage; no
  raw `new`/`delete`.
- Parsing returns explicit results (`ParsedLine.ok`, `std::optional<Json>` +
  error string); nothing throws across module boundaries in the hot path.
- Test assertions throw a local type caught by the runner; a failing test
  always makes the process exit non-zero.

## Build layout

```
CMakeLists.txt        single top-level file, no external dependencies
include/vectra/...    public headers, one directory per component
src/...               implementations mirroring include/
tests/                self-contained harness + one file per component
examples/             synthetic sample dataset (clearly labelled)
config/default.json   every threshold, UNCALIBRATED values marked
scripts/              build.sh, build.bat, replay_sample.sh, gen_sample_ticks.py
docs/                 architecture, model, limitations, testing plan
```

## Extension points (deliberately left open)

1. **Live broker adapter** implementing `IExecutionGateway` (requires
   authorization; out of scope for v0.1).
2. **Alternative predictors** implementing the same
   `predict(const BehaviorFeatures&) -> Prediction` shape; the risk engine
   consumes only the `Prediction` struct.
3. **Real data ingestion**: the replay loop consumes any `std::istream` of CSV
   ticks, so new data sources need only a converter.
4. **Calibration layer**: if scores are ever statistically calibrated against
   held-out data, the naming in `prediction.hpp` can change from *score* to
   *probability* — and only then.
