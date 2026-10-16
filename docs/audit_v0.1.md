# VECTRA v0.1 — Audit and go/no-go review

**Scope:** the VECTRA v0.1 research engine at commit `d840640` (branch `master`),
reviewed against `docs/full_project_report.md`, `docs/testing_plan.md`,
`docs/architecture.md`, `docs/data_limitations.md`, `docs/mathematical_model.md`,
`README.md`, `reports/sample_report.md` and the shipped source and tests.

**Method:** evidence-first. Every finding below was reproduced from a concrete
build, test run or replay run; the exact command and exit status are given. No
finding is asserted from reading alone without a failing test that demonstrates
it. Fixes were written only after a regression test failed for the right reason
(fail-first).

**Severity:** **High** = invalidates a documented behavioural guarantee or a
safety control; **Medium** = wrong/lost behaviour under a legal configuration;
**Low** = inaccurate or dead code/telemetry, no incorrect trading behaviour;
**Info** = documentation/consistency only.

---

## 1. Executive summary

VECTRA v0.1 is a coherent, dependency-free C++20 prototype whose internals
match the architecture it documents. The audit found **four defects** (one
High, one Medium, two Low) and **one documentation-drift item**, all now fixed
and covered by regression tests. The engine builds warning-free, all tests pass
(before and after the fixes), and the sample replay's deterministic digest and
headline P&L are unchanged by the fixes — i.e. the fixes correct gate/telemetry
behaviour without perturbing the documented trade path.

**Decision: conditional go** for the intended next phase — *offline research on
real historical tick data* — provided the model-threshold and edge-calibration
work in §6 is treated as mandatory before any capital is committed. **No-go**
for live/real-money trading, which was already out of scope for v0.1.

---

## 2. Verification (post-fix)

Clean tree, fresh build directory, MSVC Release:

| Step | Command | Result |
|---|---|---|
| Configure | `cmake -S . -B build-audit` | exit `0` |
| Build | `cmake --build build-audit --config Release` | exit `0`, **0 warnings, 0 errors** |
| Unit tests | `build-audit\Release\vectra_tests.exe` | `tests run: 105, passed: 105, failed: 0`, exit `0` |
| ctest | `ctest --test-dir build-audit -C Release` | `100% tests passed, 0 tests failed out of 2`, exit `0` |
| Sample replay | `vectra.exe --config config/default.json --data examples/sample_ticks.csv --report reports/sample_report.md` | exit `0`; digest `bc06ff21b93aecfb`; net P&L 3791.70; reconciliation matched |

The build directory was removed afterwards (`build-audit` is not committed).

**Digest invariance.** `reports/sample_report.md`'s run digest
(`bc06ff21b93aecfb`) and the headline numbers (5 395 valid, 2 339 signals,
40 allowed, 53 fills, net 3791.70, max drawdown 899.91) are **identical before
and after the fixes**. The deterministic digest is computed over signals,
decisions, fills, net P&L and max drawdown, so this proves the fixes did not
change which orders were placed or filled on the sample path — they only
corrected the risk gate's inputs and the report's labelling.

---

## 3. Findings

| # | Severity | Title | Status |
|---|---|---|---|
| F1 | **High** | Realized P&L never fed to the risk engine's daily-loss gate during replay | Fixed + regression test |
| F2 | **Medium** | Simulated broker silently dropped a fill attempt scheduled before acknowledgement (`fill_latency < ack_latency`) | Fixed + regression test |
| F3 | Low | `ReplayStats::peak_equity` was never assigned (always 0) | Fixed |
| F4 | Low | Report §6 was labelled "Processing latency (steady_clock…)" but includes a venue-model-time row; `data_limitations.md` contradicted itself | Fixed |
| F5 | Info | Documentation drift: test counts (102) and pinned latency sample values were stale | Fixed |

### F1 — High — Realized P&L never reached the daily-loss gate

`RiskEngine::record_realized_pnl` (`include/vectra/risk/risk_engine.hpp:99`,
`src/risk/risk_engine.cpp:88`: `daily_pnl_ += pnl_delta;`) is the only writer of
the value the daily-loss gate reads (`risk_engine.cpp:108`, checked *first*, so
it applies to neutral predictions too). A HEAD-tree search shows it had **no
caller anywhere in the engine**:

```
$ git grep -n "record_realized_pnl" HEAD -- src include tests
HEAD:include/vectra/risk/risk_engine.hpp:99:  void record_realized_pnl(double pnl_delta);
HEAD:src/risk/risk_engine.cpp:88:void RiskEngine::record_realized_pnl(double pnl_delta) { ... }
HEAD:tests/test_risk.cpp:78:  risk.record_realized_pnl(-250.0);
```

The replay engine's `apply_local_fill` (`src/backtest/replay_engine.cpp:96`)
updated its own `local_account_.realized_pnl` but never notified the risk
engine. Two consequences:

1. `max_daily_loss` was **inert during any replay** — the control the docs
   advertise as active could never fire from live P&L (only a direct unit-level
   call could trigger it).
2. `docs/full_project_report.md` §8 asserted the risk state is "mutated
   exclusively by replay-driven `record_*` calls (… `record_realized_pnl` …)".
   That was **false** before the fix and is true now.

**Evidence (fail-first):** with the two behavioural fixes temporarily reverted
(`git stash`) and the new tests present, the suite reported
`tests run: 105, passed: 102, failed: 3` (exit `1`), including
`Replay.RealizedPnlIsFedToRiskEngine` failing at `tests/test_backtest.cpp:196`
(`daily_realized_pnl()` was 0 while `stats.realized_pnl` was non-zero).

**Fix:** `apply_local_fill` now captures `local_account_.realized_pnl` before
the fill, computes the delta after, and calls `risk_.record_realized_pnl(delta)`
when non-zero (`src/backtest/replay_engine.cpp:112`).

**Regression tests:**
`Replay.RealizedPnlIsFedToRiskEngine` (asserts the scenario realizes P&L, then
`engine.risk().daily_realized_pnl()` equals `stats.realized_pnl` to 1e-6) and
`Replay.DailyLossLimitBlocksOrdersAfterLoss` (`max_daily_loss = 1e-6`, asserts
the `DailyLossLimit` reason is recorded). A read-only `ReplayEngine::risk()`
accessor was added for this (`include/vectra/backtest/replay_engine.hpp`).

### F2 — Medium — Fill attempt dropped when scheduled before ack

At HEAD, `submit` scheduled the ack at `now + ack_latency_ns` and the fill at
`now + fill_latency_ns` independently (`src/execution/simulated_broker.cpp:151`
and `:152`). When `fill_latency_ns < ack_latency_ns` (a legal configuration),
the `FillAttempt` becomes due first; processing it hits the guard

```
if (order->state == OrderState::Submitted) { return; }  // :333-334
```

and **returns without rescheduling**, so the order is stuck: acknowledged at
`now + ack_latency_ns` but with no further fill attempt — never filled.

`default.json` ships `fill_latency_ms (100) > ack_latency_ms (50)`, which is
why the sample replay never exposed it, and the existing broker tests only
exercised the default ordering.

**Evidence (fail-first):** `Broker.FillAttemptBeforeAckIsNotDropped` failed at
`tests/test_execution.cpp:191` (`state` was `Acknowledged`, expected `Filled`).

**Fix:** the fill action is now scheduled at
`now + std::max(fill_latency_ns, ack_latency_ns)`
(`src/execution/simulated_broker.cpp:155-158`), guaranteeing
`Ack ≤ FillAttempt` ordering. No look-ahead is introduced: both latencies are
still ≥ 0, so an order still cannot fill on its own event.

### F3 — Low — `peak_equity` was dead

`update_equity` tracked an internal `equity_peak_` (used for drawdown) but
never wrote it to `stats_.peak_equity` (`replay_engine.cpp:161-185` at HEAD;
the field is declared at `replay_engine.hpp:122` and was therefore always
`0.0`). No report field reads it, so there was no wrong output — it is a dead/
misleading stat.

**Fix:** `stats_.peak_equity = equity_peak_;` is now assigned in
`update_equity` (`src/backtest/replay_engine.cpp:189`). No reference in the
report changed (the report does not print peak equity), confirmed by the
unchanged digest and report diff.

### F4 — Low — Latency section mislabelled and self-contradictory

`reports/sample_report.md` §6 was headed
`## 6. Processing latency (steady_clock, microseconds)` and its trailing note
said the numbers were "measured exclusively with `std::chrono::steady_clock`",
yet the last row is the **venue model-time** submit→ack latency (driven by
market-event timestamps, `replay_engine.cpp:137`), not a `steady_clock`
duration. `docs/data_limitations.md:55-56` repeated the "all latency numbers …
`steady_clock`" claim and then contradicted it one sentence later.

This is a presentation/measurement-domain error, not an algorithmic one, but it
could mislead a reader into comparing the 100 ms venue row with real wall-clock
latencies.

**Fix:** the section is now `## 6. Latency (microseconds)`; the row is
`Simulated order (submit to ack, venue clock)`; the note distinguishes the
three `steady_clock` rows from the venue-clock row
(`src/backtest/report.cpp:186-195`). `docs/data_limitations.md` was reworded to
match. The header-only comment in `replay_engine.hpp:127` was corrected too.

### F5 — Info — Documentation drift

`docs/full_project_report.md` and `README.md` pinned "102 tests" (now 105) and
a specific latency sample (e.g. `event 0.4 / 0.6 / 0.7`), which is wall-clock
run-to-run variable. Updated to 105 tests, the new test names added to the
coverage map and Appendix C, and the latency row refreshed with an explicit
"varies per run" note. The document line-count columns were verified unchanged.

---

## 4. Verified facts vs. claims

| Claim (docs/report) | Verdict |
|---|---|
| No look-ahead: an order submitted at event *t* cannot fill at *t* | **Holds** (`Replay.NoLookAheadOnPrefix`; unchanged by F2 fix) |
| Deterministic replay digest | **Holds** — `bc06ff21b93aecfb`, reproduced |
| `net ≤ gross` | **Holds** (`Replay.CostsDoNotInflateNetPnl`) |
| Daily-loss gate active during replay | **Was false (F1)**; now holds and is regression-tested |
| "Risk state mutated by replay-driven `record_*` calls" incl. `record_realized_pnl` | **Was false (F1)**; now holds |
| Reconciliation is independent local vs broker bookkeeping | **Partially holds** — two code paths fed the same fill stream; see §7 |
| Latency table is `steady_clock` | **Was false for one row (F4)**; corrected |
| Sample replay figures (5 395 / 2 339 / 40 / 53, net 3791.70) | **Holds**, reproduced verbatim |

---

## 5. Changes made (all uncommitted)

| File | Change |
|---|---|
| `src/execution/simulated_broker.cpp` | Fill scheduled at `max(fill_latency, ack_latency)` (F2) |
| `src/backtest/replay_engine.cpp` | Feed realized-P&L delta to `risk_.record_realized_pnl` (F1); assign `stats_.peak_equity` (F3) |
| `include/vectra/backtest/replay_engine.hpp` | Read-only `risk()` accessor; corrected latency comment |
| `src/backtest/report.cpp` | §6 header/row/footnote corrected (F4) |
| `tests/test_execution.cpp` | `Broker.FillAttemptBeforeAckIsNotDropped` (F2) |
| `tests/test_backtest.cpp` | `Replay.RealizedPnlIsFedToRiskEngine`, `Replay.DailyLossLimitBlocksOrdersAfterLoss` (F1); `v_walk` loss scenario; §6 header assertion (F4) |
| `README.md`, `docs/full_project_report.md`, `docs/testing_plan.md`, `docs/data_limitations.md` | Test counts 102→105, new test names, latency wording/values (F5) |
| `reports/sample_report.md` | Regenerated (only §6 wording/values changed; digest unchanged) |

No model thresholds, no scoring rules, and no no-look-ahead-ordering guarantees
were altered. The deterministic digest is unchanged, which is the evidence for
that statement.

---

## 6. Go/no-go

**Conditional go** for the next phase the project itself scopes: offline
research on **real** historical tick data, with the following mandatory gates
before interpreting any P&L:

1. Ingest real ticks and validate rejection statistics first (the engine's
   value begins with rejecting bad rows correctly).
2. Chronological development/validation/out-of-sample split; lock the test
   period; pre-register the six `[U]` thresholds and evaluate the whole grid on
   development only.
3. Compare against the required baselines (always-neutral, always-continue,
   cost-free random at equal trade count); apply block-bootstrap, a binomial
   hit-rate test and multiple-comparison correction.
4. Regenerate the report on real data and re-freeze the digest for that
   dataset.

**No-go** for live or real-money trading in v0.1: it remains out of scope by
design (single instrument, quotes-only, simulated venue, uncalibrated scores,
no margin/financing/swaps). The current P&L is a synthetic pipeline smoke test
and is labelled as such in the console and the report.

---

## 7. Observations (not defects) and residual limits

* **Reconciliation independence (Low, informational).** The replay's local
  account and the broker's account are separate implementations fed the same
  fill stream (`docs/full_project_report.md` §19.6). They catch
  double-counting/lost-event bugs but are not an external oracle; a shared
  misreading of the fill protocol would not be caught. This was already
  acknowledged in the docs and left unchanged.
* **Latency values are not deterministic.** The first three report rows are
  `steady_clock` durations and vary per run; only the venue row is
  event-quantized. Documentation now says so explicitly.
* **`peak_equity` is now correct but unused.** It is populated for completeness;
  if a future report surfaces it, a test should pin it.
* **No performance/regression benchmark, no parser fuzzing, no live-broker
  adapter** — explicitly listed as non-goals in `docs/testing_plan.md` §7 and
  out of scope for this audit.

---

*Prepared as part of the VECTRA v0.1 audit. Findings and fixes are recorded
above with reproducible evidence; nothing here is a claim of real-market
profitability.*
