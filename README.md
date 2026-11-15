# VECTRA

**Deterministic C++20 Market Behavior, Exhaustion, and Risk-Gated Execution Engine**

VECTRA is a deterministic, zero-external-dependency C++20 financial engineering engine designed to ingest raw tick-by-tick market quote streams, enforce strict data hygiene, extract causal multi-horizon price-behavior and exhaustion features, generate rule-based directional predictions (`Continuation`, `Reversal`, `Neutral`), gate every decision through a multi-layer risk engine, and evaluate outcomes via a realistic latency-and-cost execution simulator.

---

## Interactive Research Portal (GitHub Pages)

A light-themed, interactive HTML research portal with SVG architecture diagrams, iterative engineering progression charts, 4-way EUR/USD split comparisons, 5-bin Brier reliability charts, and an interactive split explorer is included in [`docs/index.html`](docs/index.html) and published via GitHub Pages:

- **Live Portal URL**: `https://logiccrafterdz.github.io/VECTRA/`
- **Local Portal File**: [`docs/index.html`](docs/index.html) (works offline via an embedded JSON snapshot and fetches [`docs/data/metrics.json`](docs/data/metrics.json) when served over HTTP)
- **Periodic Data Refresh Script**: [`scripts/update_site_data.py`](scripts/update_site_data.py)

To regenerate [`docs/data/metrics.json`](docs/data/metrics.json) and update the embedded snapshot inside [`docs/index.html`](docs/index.html) after any replay or calibration run:

```powershell
python scripts\update_site_data.py
```

---

## Table of Contents

1. [Core Engineering Principles and Epistemic Guardrails](#1-core-engineering-principles-and-epistemic-guardrails)
2. [System Architecture and Deterministic Pipeline](#2-system-architecture-and-deterministic-pipeline)
3. [Mathematical Model and Structural Formulations](#3-mathematical-model-and-structural-formulations)
4. [Iterative Engineering Progression (Five Stages)](#4-iterative-engineering-progression-five-stages)
5. [Empirical Study: Real EUR/USD Tick Stream (461,357 Ticks, 20.98 Hours)](#5-empirical-study-real-eurusd-tick-stream-461357-ticks-2098-hours)
6. [Score Reliability and 5-Bin Brier Calibration](#6-score-reliability-and-5-bin-brier-calibration)
7. [Synthetic Benchmark Validation and Determinism Verification](#7-synthetic-benchmark-validation-and-determinism-verification)
8. [Build, Test, Replay, Live Streaming, and Calibration Guide](#8-build-test-replay-live-streaming-and-calibration-guide)
9. [Repository Structure and Documentation Index](#9-repository-structure-and-documentation-index)

---

## 1. Core Engineering Principles and Epistemic Guardrails

VECTRA is engineered around six strict architectural and statistical constraints documented in [`docs/project_identity.md`](docs/project_identity.md), [`docs/mathematical_model.md`](docs/mathematical_model.md), and [`docs/testing_plan.md`](docs/testing_plan.md):

1. **Strict Causality**: Every feature, exhaustion component, directional prediction, and risk decision at event $i$ is computed exclusively from validated events $\{0, 1, \dots, i\}$. Zero future look-ahead is permitted in feature computation. Forward-return evaluation (`ForwardEvalTracker`) operates strictly as a passive post-prediction observer.
2. **Bitwise Determinism (`FNV-1a 64-bit` Digest)**: Fixed-point integer price representation (`PriceFixed`, $10^{-8}$ precision), nanosecond timestamps (`TimestampNs`), and deterministic execution seeding guarantee that identical inputs and configurations produce the exact same `FNV-1a 64-bit` replay digest across runs.
3. **Wall-Clock Provider-Timestamp Invariance**: Foreign exchange tick arrival rates vary by 3x to 5x between Asian, London, and New York sessions. VECTRA anchors its short, medium, and long observation windows (`120s / 240s / 720s`), position hold horizon (`180s`), and post-entry cooldown (`180s`) directly to provider timestamps (`source_time_ns`), ensuring consistent structural horizon geometry across all sessions.
4. **Full Round-Trip Cost Accounting**: Both the pre-trade risk gate (`RiskEngine`) and the execution simulator (`ExecutionSimulator`) account for full entry-plus-exit spread, commission, and slippage (`spread + 2 * (commission + slippage)`).
5. **Autocorrelation-Robust Statistical Inference**: Overlapping tick-by-tick forward returns share $99\%+$ of their future price paths, artificially inflating standard $t$-statistics. VECTRA computes **Non-Overlapping Horizon Block** hit rates and Student $t$-statistics alongside a **Naive Directional Baseline** (`disp_short` continuation) and a **5-Bin Score Reliability & Brier Calibration Table**.
6. **Pre-Registered Walk-Forward Protocol**: Parameters in [`config/real_eurusd.json`](config/real_eurusd.json) are selected via walk-forward grid search ([`scripts/calibrate_walk_forward.py`](scripts/calibrate_walk_forward.py)) strictly on the chronological Development (`DEV`) and Validation (`VAL`) splits before evaluating the locked configuration on the blind Out-of-Sample splits (`OOS` and `OOS2`).

---

## 2. System Architecture and Deterministic Pipeline

Every market tick flows sequentially through seven deterministic stages without dynamic thread scheduling, nondeterministic timers, or external library dependencies:

```text
+---------------------------------------------------------------------------------------------------+
|                                   VECTRA DETERMINISTIC PIPELINE                                   |
+---------------------------------------------------------------------------------------------------+
                                                 |
  [1] Market Ingestion & Hygiene (CsvTickSource / Stream / MarketValidator)
      - Parses CSV or stdin JSONL/CSV stream into Fixed-Point Bid/Ask (PriceFixed = round(p * 10^8))
      - Rejects NaN/Inf, non-positive prices, crossed/negative spreads (ask < bid), wide spreads,
        out-of-order sequences, and stale timestamps; flags inter-tick stale gaps (> 15,000 ms)
                                                 |
                                                 v
  [2] Causal Multi-Horizon Behavior Engine (BehaviorEngine)
      - Maintains rolling history indexed by both tick count and provider wall-clock timestamp
      - Computes signed log displacements across Short (120s), Mid (240s), and Long (720s) windows
      - Extracts velocity, scale-invariant inter-window & intra-window deceleration, response fade,
        rolling Welford realized volatility, sign persistence, and multi-scale block path efficiency
                                                 |
                                                 v
  [3] Exhaustion Model (ExhaustionScorer)
      - Normalizes 4 bounded [0, 1] sub-scores: Extension (z-score), Deceleration, Response Fade,
        and Spread/Data Quality Penalty
      - Combines into composite exhaustion_score in [0, 1] (weights: 0.30 / 0.25 / 0.35 / 0.10)
                                                 |
                                                 v
  [4] Rule-Based Directional Predictor (ScoringPredictor)
      - Evaluates Reversal (high exhaustion >= 0.65, extended |z| >= 1.8, low persistence <= 0.20)
        and Continuation (low exhaustion <= 0.35, persistence >= 0.10, regime_efficiency >= 0.55)
      - Computes expected_move_price with branch-specific scale factors (continuation_move_scale=1.0,
        reversal_move_scale=0.45) to correct score-distribution asymmetry
                                                 |
                                                 v
  [5] Multi-Gate Risk Engine (RiskEngine)
      - Enforces 12 sequential pre-trade gates: KillSwitch, DataHealth, SpreadGate, SignalValidity,
        Wall-Clock Cooldown (180s), Round-Trip Edge Gate (expected_move - [spread + 2*(comm+slip)]),
        MaxOpenOrders, MaxOrdersPerDay, DailyLossLimit, PositionLimit, ExposureLimit
                                                 |
                                                 v
  [6] Latency & Cost Execution Simulator + Position Lifecycle (ExecutionSimulator / ReplayEngine)
      - Models deterministic order acknowledgment (20ms) and fill latency (50ms)
      - Executes fills against prevailing Bid/Ask +/- slippage and deducts per-unit commission
      - Enforces automated position exits via Hold Horizon (180s), Stop-Loss (8 pips), and Take-Profit (12 pips)
                                                 |
                                                 v
  [7] Causal Forward Evaluation, Brier Calibration & Digest Verification (ReplayEngine / Report)
      - Evaluates every usable signal at t + eval_horizon_ns against Mid and Spread-Crossed quotes
      - Computes Naive Directional Baseline, Non-Overlapping Block t-stats, 5-Bin Brier Reliability,
        sub-microsecond stage latency percentiles, and the 64-bit FNV-1a replay digest
+---------------------------------------------------------------------------------------------------+
```

### Module Inventory

| Layer | Header | Implementation | Responsibility |
|---|---|---|---|
| **Core Types & Math** | [`include/vectra/core/types.hpp`](include/vectra/core/types.hpp), [`math.hpp`](include/vectra/core/math.hpp), [`time.hpp`](include/vectra/core/time.hpp) | Header-only (`constexpr` / `noexcept`) | Fixed-point `PriceFixed` ($10^8$ scale), `TimestampNs`, `WelfordAccumulator`, `KahanSum`, `fnv1a_64` hash, latency percentile calculator. |
| **Market Ingestion** | [`include/vectra/market/event.hpp`](include/vectra/market/event.hpp), [`validator.hpp`](include/vectra/market/validator.hpp), [`csv_reader.hpp`](include/vectra/market/csv_reader.hpp) | [`src/market/validator.cpp`](src/market/validator.cpp), [`csv_reader.cpp`](src/market/csv_reader.cpp) | Stream/file parsing, schema validation, spread/monotonicity/staleness checks, gap flagging. |
| **Behavior Features** | [`include/vectra/behavior/features.hpp`](include/vectra/behavior/features.hpp) | [`src/behavior/behavior_engine.cpp`](src/behavior/behavior_engine.cpp) | Wall-clock and event-count ring buffer, velocity, inter/intra-window deceleration & fade, volatility, persistence, block path efficiency. |
| **Exhaustion Model** | [`include/vectra/exhaustion/exhaustion.hpp`](include/vectra/exhaustion/exhaustion.hpp) | [`src/exhaustion/exhaustion_scorer.cpp`](src/exhaustion/exhaustion_scorer.cpp) | Bounded $[0,1]$ normalization and weighted combination of extension, deceleration, fade, and quality sub-scores. |
| **Prediction Engine** | [`include/vectra/prediction/prediction.hpp`](include/vectra/prediction/prediction.hpp) | [`src/prediction/scoring_predictor.cpp`](src/prediction/scoring_predictor.cpp) | Explicit `Continuation` / `Reversal` / `Neutral` classification, regime efficiency gates, and expected-move scale normalization. |
| **Risk Engine** | [`include/vectra/risk/risk.hpp`](include/vectra/risk/risk.hpp) | [`src/risk/risk_engine.cpp`](src/risk/risk_engine.cpp) | Twelve-gate pre-trade filter, round-trip cost edge verification, wall-clock cooldowns, exposure and daily loss limits. |
| **Execution Simulator** | [`include/vectra/execution/execution.hpp`](include/vectra/execution/execution.hpp) | [`src/execution/execution_simulator.cpp`](src/execution/execution_simulator.cpp) | Deterministic latency queue, bid/ask crossing, slippage, commission, and cash/realized/unrealized P&L ledger. |
| **Backtest & Reporting** | [`include/vectra/backtest/replay.hpp`](include/vectra/backtest/replay.hpp), [`report.hpp`](include/vectra/backtest/report.hpp) | [`src/backtest/replay_engine.cpp`](src/backtest/replay_engine.cpp), [`report.cpp`](src/backtest/report.cpp) | Position lifecycle exits, causal forward returns, non-overlapping block $t$-stats, 5-bin Brier calibration, Markdown & JSONL output. |

---

## 3. Mathematical Model and Structural Formulations

Full derivations are documented in [`docs/mathematical_model.md`](docs/mathematical_model.md). Below is the exact formulation implemented in the C++20 engine:

### 3.1 Fixed-Point Representation and Log Return
For valid tick $i$ with bid $b_i > 0$ and ask $a_i \ge b_i$:
$$P_{\text{fixed}}(x) = \operatorname{round}(x \cdot 10^8) \in \mathbb{Z}_{64}, \quad m_i = \frac{b_i + a_i}{2}, \quad s_i = a_i - b_i, \quad r_i = \ln\left(\frac{m_i}{m_{i-1}}\right)$$

### 3.2 Wall-Clock Observation Windows (`window_short_ns`, `window_mid_ns`, `window_long_ns`)
When wall-clock windows $(\Delta \tau_S, \Delta \tau_M, \Delta \tau_L) = (120\text{s}, 240\text{s}, 720\text{s})$ are configured, the engine locates for each horizon $W \in \{S, M, L\}$ the earliest event index $k_W \le i$ in the ring buffer satisfying $t_i - t_{k_W} \le \Delta \tau_W$:
$$D_W(i) = \ln\left(\frac{m_i}{m_{k_W}}\right), \quad v_W(i) = \frac{D_W(i)}{n_W}, \quad n_W = \max(1, i - k_W)$$

### 3.3 Multi-Scale Block Regime Efficiency (`regime_efficiency`)
Tick-by-tick FX microstructure exhibits strong negative first-order autocorrelation (bid-ask bounce), making single-tick path efficiency noisy over hundreds of ticks. VECTRA partitions the medium window into $B = \min(n_M, 16)$ contiguous sub-blocks and measures directional linearity across blocks:
$$E_{\text{regime}}(i) = \frac{|D_M(i)|}{\sum_{b=1}^{B} \left|\ln(m_{\text{end}(b)} / m_{\text{start}(b)})\right|} \in [0, 1]$$
Values near $1.0$ indicate clean, persistent directional progression across 15-second sub-blocks; values near $0.0$ indicate range-bound choppy oscillation.

### 3.4 Scale-Invariant Inter-Window and Intra-Window Deceleration & Response Fade
To detect momentum exhaustion both across horizons and within the current short window (selecting the stronger of inter-window and intra-window stall):
$$\text{decel}_{\text{inter}}(i) = \max\left(0, 1 - \frac{\operatorname{ sgn}(D_M(i)) \cdot v_S(i)}{|v_M(i)|}\right), \quad \text{decel}_{\text{intra}}(i) = \max\left(0, 1 - \frac{\operatorname{sgn}(D_S(i)) \cdot v_{S,2}(i)}{|v_{S,1}(i)|}\right)$$
$$\text{fade}_{\text{inter}}(i) = \max\left(0, 1 - \frac{\operatorname{sgn}(D_M(i)) \cdot D_S(i)}{|D_M(i)| \cdot (n_S / n_M)}\right), \quad \text{fade}_{\text{intra}}(i) = \max\left(0, 1 - \frac{\operatorname{sgn}(D_S(i)) \cdot D_{S,2}(i)}{|D_{S,1}(i)|}\right)$$
where $(D_{S,1}, v_{S,1})$ and $(D_{S,2}, v_{S,2})$ denote the first and second halves of the short window. The engine sets $\text{decel}(i) = \max(\text{decel}_{\text{inter}}(i), \text{decel}_{\text{intra}}(i))$ and $\text{fade}(i) = \max(\text{fade}_{\text{inter}}(i), \text{fade}_{\text{intra}}(i))$.

### 3.5 Volatility Normalization and Composite Exhaustion Score
Using rolling Welford realized return volatility $\sigma_V(i)$, the volatility-normalized short extension $z$-score and composite exhaustion score $E(i) \in [0, 1]$ are:
$$z_{\text{ext}}(i) = \frac{D_S(i)}{\sigma_V(i)\sqrt{n_S}}, \quad E(i) = w_{\text{ext}} S_{\text{ext}}(i) + w_{\text{dec}} S_{\text{dec}}(i) + w_{\text{fade}} S_{\text{fade}}(i) + w_{\text{qual}} S_{\text{qual}}(i)$$
with default weights $(0.30, 0.25, 0.35, 0.10)$.

### 3.6 Directional Classification and Expected-Move Scale Normalization
The predictor evaluates two candidate branch scores:
$$C_{\text{rev}}(i) = E(i) \cdot \operatorname{clamp}\left(\frac{|z_{\text{ext}}(i)|}{z_{\text{norm}}}, 0, 1\right) \cdot \left(1 - |P(i)|\right)$$
$$C_{\text{cont}}(i) = \left(1 - E(i)\right) \cdot \operatorname{clamp}\left(\frac{|z_{\text{ext}}(i)|}{z_{\text{norm}}}, 0, 1\right) \cdot |P(i)|$$
Because $|P(i)| \approx 0.15\text{--}0.35$ on 80-tick FX buffers whereas $(1 - |P(i)|) \approx 0.80\text{--}0.95$, raw reversal scores are structurally $2.5\times$ larger than continuation scores. To prevent marginal reversal candidates from bypassing the risk engine's minimum edge threshold, VECTRA applies branch-specific scale factors $(\alpha_{\text{cont}}, \alpha_{\text{rev}}) = (1.0, 0.45)$:
$$\widehat{\Delta P}_{\text{branch}}(i) = m_i \cdot \min\left(\Delta r_{\max}, \; \alpha_{\text{branch}} \cdot C_{\text{branch}}(i) \cdot \sigma_V(i) \sqrt{H}\right)$$

### 3.7 Round-Trip Risk Gate
An actionable prediction is allowed to submit an order if and only if all risk limits pass and its net expected edge after full round-trip transaction costs exceeds `min_expected_edge`:
$$\text{Edge}_{\text{net}}(i) = \widehat{\Delta P}(i) - \left[s_i + 2 \cdot (c_{\text{unit}} + \delta_{\text{slip}})\right] \ge \text{Edge}_{\min}$$

---

## 4. Iterative Engineering Progression (Five Stages)

Following the diagnostic roadmap in [`docs/testing_plan.md`](docs/testing_plan.md), VECTRA progressed through five documented engineering stages on the real institutional EUR/USD dataset:

| Engineering Stage | Structural Change Introduced | `DEV` Net P&L | `VAL` Net P&L | `OOS 1` Net P&L | `OOS 2` Net P&L | Cumulative Net P&L |
|---|---|---:|---:|---:|---:|---:|
| **Baseline (`v0.1` Default)** | Tick-counted windows (`8/32/128`), 1-way cost gate, no cooldown, no position lifecycle exits (`408` fills in `DEV`). | `-$94.95` | — | — | — | `-$94.95` |
| **Stage 1: Execution Hygiene** | Added `round_trip_cost` (`2x` commission/slippage), order cooldowns, horizon exits, and stop-loss / take-profit. | `+$14.35` | `-$26.40` | `-$22.25` | — | `-$34.30` |
| **Stage 2: Wall-Clock Windows** | Anchored windows (`120s/240s/720s`), hold horizon (`180s`), and cooldown (`180s`) to provider `source_time_ns`; added scale-invariant inter-window deceleration. | `+$11.30` | `+$11.10` | `-$16.80` | — | `+$5.60` |
| **Stage 3: Regime Efficiency** | Added multi-scale block `regime_efficiency` (`min_continuation_efficiency = 0.55`) and intra-window stall detection (`decel_intra`, `fade_intra`). | `+$33.20` | `+$0.10` | `-$4.00` | `-$6.70` | `+$22.60` |
| **Stage 4: Score Calibration** | Added 5-bin Brier calibration diagnostics and expected-move scale normalization (`reversal_move_scale = 0.45`) calibrated strictly on `DEV + VAL`. | **`+$18.90`** | **`+$7.00`** | **`-$0.70`** | **`+$1.50`** | **`+$26.70`** |

---

## 5. Empirical Study: Real EUR/USD Tick Stream (461,357 Ticks, 20.98 Hours)

### 5.1 Dataset Provenance and Chronological 4-Way Split
The dataset was extracted via [`scripts/fetch_real_eurusd.py`](scripts/fetch_real_eurusd.py) from the historical FXCM institutional EUR/USD tick archive (`Week 1, 2018`, `461,357` consecutive ticks covering `20.98 hours` of market trading) and split strictly chronologically into four non-overlapping files:

1. **`DEV` ([`examples/real_eurusd_dev.csv`](examples/real_eurusd_dev.csv))**: Ticks `1–100,000` (`6.48 hours`, Asian to early London session).
2. **`VAL` ([`examples/real_eurusd_val.csv`](examples/real_eurusd_val.csv))**: Ticks `100,001–200,000` (`2.95 hours`, London peak session).
3. **`OOS 1` ([`examples/real_eurusd_oos.csv`](examples/real_eurusd_oos.csv))**: Ticks `200,001–300,000` (`3.55 hours`, London / New York overlap).
4. **`OOS 2` ([`examples/real_eurusd_oos2.csv`](examples/real_eurusd_oos2.csv))**: Ticks `300,001–461,357` (`8.00 hours`, US / Late New York blind holdout).

### 5.2 Complete 4-Way Performance Summary (`config/real_eurusd.json`)

| Metric | `DEV` (100k, 6.48h) | `VAL` (100k, 2.95h) | `OOS 1` (100k, 3.55h) | `OOS 2` (161.4k, 8.00h) | **Total / Combined (461.4k, 20.98h)** |
|---|---|---|---|---|---|
| **Full Markdown Report** | [`real_eurusd_dev_report.md`](reports/real_eurusd_dev_report.md) | [`real_eurusd_val_report.md`](reports/real_eurusd_val_report.md) | [`real_eurusd_oos_report.md`](reports/real_eurusd_oos_report.md) | [`real_eurusd_oos2_report.md`](reports/real_eurusd_oos2_report.md) | [`eurusd_empirical_study.md`](docs/eurusd_empirical_study.md) |
| **Replay Digest (`FNV-1a 64-bit`)** | `31ac500350c03e6f` | `66a88005aa9a059e` | `781f94bb1eb226be` | `bc7ca3e5953ab2a1` | Deterministic across runs |
| **Valid / Rejected Events** | `96,419` / `3,581` | `89,127` / `10,873` | `90,477` / `9,523` | `144,122` / `17,235` | `420,145` / `41,212` |
| **Stale Gaps Flagged (`>15s`)** | `54` | `7` | `7` | `32` | `100` |
| **Usable Predictions (`Signal Rate`)** | `6,422` (`6.66%`) | `3,926` (`4.41%`) | `4,292` (`4.74%`) | `4,046` (`2.81%`) | `18,686` (`4.45%`) |
| **Signal Breakdown (`Cont` / `Rev`)** | `2,649` / `3,773` | `1,305` / `2,621` | `1,818` / `2,474` | `1,254` / `2,792` | `7,026` / `11,660` |
| **Risk Allowed Decisions (Entries)** | `18` | `22` | `24` | `18` | `82` |
| **Orders Submitted / Fills** | `36` / `36` | `43` / `43` | `47` / `47` | `36` / `36` | `162` / `162` |
| **Gross P&L (After Spread)** | **`+$22.50`** | **`+$11.30`** | **`+$4.00`** | **`+$5.10`** | **`+$42.90` (Positive in 4/4 splits)** |
| **Net P&L (After All Costs)** | **`+$18.90`** | **`+$7.00`** | **`-$0.70`** | **`+$1.50`** | **`+$26.70` (Combined OOS `+$0.80`)** |
| **Total Commission + Slippage Paid** | `$3.60` | `$4.30` | `$4.70` | `$3.60` | `$16.20` |
| **Max Drawdown ($ / %)** | `$16.10` (`0.016%`) | `$21.70` (`0.022%`) | `$23.10` (`0.023%`) | `$14.40` (`0.014%`) | Worst split `<= 0.023%` |

### 5.3 Causal Forward-Return & Non-Overlapping Horizon Block Evaluation (`180s` Horizon)

Every usable signal is evaluated at `t + 180s` against both mid-price change and spread-crossed execution quotes, and compared against a **Naive Directional Baseline** (which predicts continuation in the direction of `disp_short` at the identical timestamps). To eliminate overlapping-tick autocorrelation, VECTRA also evaluates strictly **Non-Overlapping `180s` Blocks**:

| Split | Branch / Metric | All Overlapping Signals (`Count`) | Mid Hit Rate | Spread-Crossed Hit Rate | Mean Signed Mid Return | Non-Overlapping Blocks (`Count`) | Block Spread-Crossed Hit Rate | Block Student $t$-Stat |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| **`DEV`** | `Continuation` | `2,649` | `66.10%` | **`54.24%`** | **`+0.000584` (`+5.84 pips`)** | `40` | `52.50%` | `+1.572` |
| **`DEV`** | `Reversal` | `3,772` | `56.60%` | `38.11%` | `-0.000210` (`-2.10 pips`) | `62` | `50.00%` | `+0.388` |
| **`DEV`** | `All Usable` | `6,421` | `60.52%` | `44.76%` | `+0.000118` (`+1.18 pips`) | `102` | **`50.98%`** | **`+1.459`** |
| **`DEV`** | `Naive Baseline` | `6,421` | `58.93%` | `46.51%` | `+0.000364` (`+3.64 pips`) | `102` | `37.10%` | `-1.908` |
| **`VAL`** | `Continuation` | `1,305` | `84.83%` | **`73.79%`** | **`+0.000323` (`+3.23 pips`)** | `16` | `43.75%` | `-0.391` |
| **`VAL`** | `Reversal` | `2,621` | `64.59%` | `43.53%` | `-0.000069` (`-0.69 pips`) | `38` | **`52.63%`** | `+0.172` |
| **`VAL`** | `All Usable` | `3,926` | `71.32%` | **`53.59%`** | `+0.000061` (`+0.61 pips`) | `54` | **`50.00%`** | `-0.088` |
| **`VAL`** | `Naive Baseline` | `3,926` | `58.20%` | `44.91%` | `+0.000154` (`+1.54 pips`) | `54` | `46.30%` | `-0.273` |
| **`OOS 1`** | `Continuation` | `1,818` | `44.33%` | `30.75%` | `-0.000208` (`-2.08 pips`) | `37` | `48.65%` | `-0.303` |
| **`OOS 1`** | `Reversal` | `2,474` | `74.41%` | **`63.29%`** | **`+0.000082` (`+0.82 pips`)** | `29` | **`55.17%`** | `+0.562` |
| **`OOS 1`** | `All Usable` | `4,292` | `61.67%` | **`49.51%`** | `-0.000041` (`-0.41 pips`) | `66` | **`51.52%`** | `+0.097` |
| **`OOS 1`** | `Naive Baseline` | `4,292` | `39.75%` | `25.56%` | `-0.000131` (`-1.31 pips`) | `66` | `43.94%` | `-0.615` |
| **`OOS 2`** | `Continuation` | `1,254` | `76.40%` | **`57.26%`** | **`+0.000079` (`+0.79 pips`)** | `35` | **`68.57%`** | **`+2.429`** |
| **`OOS 2`** | `Reversal` | `2,792` | `71.42%` | `46.13%` | `-0.000059` (`-0.59 pips`) | `118` | `50.85%` | `-0.097` |
| **`OOS 2`** | `All Usable` | `4,046` | `72.96%` | **`49.58%`** | `-0.000016` (`-0.16 pips`) | `153` | **`54.90%`** | **`+1.095`** |
| **`OOS 2`** | `Naive Baseline` | `4,046` | `50.79%` | `34.16%` | `+0.000065` (`+0.65 pips`) | `153` | `40.26%` | `-2.511` |

---

## 6. Score Reliability and 5-Bin Brier Calibration

In accordance with [`docs/testing_plan.md`](docs/testing_plan.md) Section 6.6, VECTRA partitions all usable predictions by their active branch score $C_{\text{active}}(i) \in [0, 1]$ into five fixed bins and measures empirical mid-price hit rates, spread-crossed hit rates, mean signed returns, and Brier calibration scores $\frac{1}{N}\sum (C_{\text{active}} - \mathbb{I}[\text{mid\_hit}])^2$:

| Score Bin | `DEV` Count / Mid Hit / Spread Hit / Brier | `VAL` Count / Mid Hit / Spread Hit / Brier | `OOS 1` Count / Mid Hit / Spread Hit / Brier | `OOS 2` Count / Mid Hit / Spread Hit / Brier | Structural Interpretation |
|---|---|---|---|---|---|
| **`[0.0, 0.2)`** | `2,211` / `63.68%` / `53.26%` / `0.3827` | `1,073` / `82.76%` / `71.20%` / `0.4831` | `1,636` / `45.35%` / `32.89%` / `0.2966` | `1,089` / `72.54%` / `54.82%` / `0.4227` | Moderate-persistence `Continuation` signals. |
| **`[0.2, 0.4)`** | `452` / `76.55%` / **`58.63%`** / `0.3204` | `234` / **`94.02%`** / **`85.47%`** / `0.4216` | `204` / `33.82%` / `14.71%` / `0.2143` | `167` / **`97.60%`** / **`73.65%`** / `0.4648` | High-persistence `Continuation` breakouts (`regime_efficiency >= 0.55`). |
| **`[0.4, 0.6)`** | `1,254` / `64.59%` / `48.64%` / `0.2286` | `599` / `56.09%` / `38.23%` / `0.2464` | `692` / `66.18%` / `56.65%` / `0.2255` | `848` / `77.24%` / `55.07%` / `0.1869` | Moderate-exhaustion `Reversal` candidates (filtered out by `reversal_move_scale = 0.45`). |
| **`[0.6, 0.8)`** | `2,186` / `54.39%` / `34.26%` / `0.3125` | `1,843` / `68.58%` / `46.55%` / `0.2266` | `1,664` / `76.92%` / **`65.02%`** / `0.1820` | `1,720` / `71.51%` / `44.59%` / `0.2054` | Upper-tier `Reversal` signals; only highest-volatility events pass the cost gate. |
| **`[0.8, 1.0]`** | `318` / `39.94%` / `23.27%` / `0.4408` | `177` / `50.85%` / `31.07%` / `0.3690` | `96` / **`90.63%`** / **`84.38%`** / **`0.0877`** | `222` / `51.35%` / `23.87%` / `0.3607` | Extreme climax `Reversal` signals (`90.63%` mid hit rate in mean-reverting `OOS 1`). |

### Per-Stage Microsecond Latency Profile (Release Build, `OOS 2` Split — `144,122` Valid Events)

| Pipeline Stage | Count | Min ($\mu\text{s}$) | p50 ($\mu\text{s}$) | p95 ($\mu\text{s}$) | p99 ($\mu\text{s}$) | Max ($\mu\text{s}$) | Mean ($\mu\text{s}$) |
|---|---:|---:|---:|---:|---:|---:|---:|
| `Validation` | `161,357` | `0.000` | `0.100` | `0.200` | `0.400` | `369.500` | `0.123` |
| `FeatureExtraction` | `144,122` | `0.200` | `1.100` | `2.300` | `3.500` | `647.800` | `1.229` |
| `ExhaustionScoring` | `144,122` | `0.000` | `0.100` | `0.100` | `0.200` | `67.100` | `0.080` |
| `Prediction` | `144,122` | `0.000` | `0.000` | `0.100` | `0.200` | `395.900` | `0.068` |
| `RiskCheck` | `144,122` | `0.000` | `0.100` | `0.100` | `0.200` | `113.000` | `0.095` |
| `Execution` | `144,122` | `0.000` | `0.100` | `0.100` | `0.200` | `658.400` | `0.082` |
| **`TotalTick`** | **`144,122`** | **`0.400`** | **`1.400`** | **`2.900`** | **`4.500`** | **`759.600`** | **`1.617`** |

---

## 7. Synthetic Benchmark Validation and Determinism Verification

In addition to the 461,357-tick real EUR/USD study, VECTRA maintains a fast, self-contained synthetic benchmark ([`examples/sample_ticks.csv`](examples/sample_ticks.csv), `240` ticks with injected malformed rows, crossed spreads, stale timestamps, trends, and reversals) verified on every test run:

- **Configuration**: [`config/default.json`](config/default.json)
- **Full Report**: [`reports/sample_report.md`](reports/sample_report.md)
- **Deterministic Replay Digest**: `bc06ff21b93aecfb`
- **Unit & Integration Test Suite**: `115/115` unit tests passing across all modules (`tests/test_*.cpp`), `2/2` CTest targets passing (`vectra_tests` and `vectra_determinism_check`).

---

## 8. Build, Test, Replay, Live Streaming, and Calibration Guide

### 8.1 Prerequisites
- **C++20 Compiler**: MSVC 19.30+ (Visual Studio 2022), GCC 11+, or Clang 14+
- **CMake**: 3.20 or newer
- **Python 3.9+**: Optional, used only for walk-forward calibration (`scripts/calibrate_walk_forward.py`) and updating the GitHub Pages data bundle (`scripts/update_site_data.py`).

### 8.2 Build and Run the Test Suite

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

### 8.3 Run Deterministic Replays (Synthetic and Real EUR/USD Splits)

```powershell
# Synthetic benchmark replay (verifies digest bc06ff21b93aecfb)
.\build\Release\vectra_cli.exe --config config\default.json --data examples\sample_ticks.csv --output reports\sample_report.md

# Real EUR/USD 4-way split replays
.\build\Release\vectra_cli.exe --config config\real_eurusd.json --data examples\real_eurusd_dev.csv  --output reports\real_eurusd_dev_report.md
.\build\Release\vectra_cli.exe --config config\real_eurusd.json --data examples\real_eurusd_val.csv  --output reports\real_eurusd_val_report.md
.\build\Release\vectra_cli.exe --config config\real_eurusd.json --data examples\real_eurusd_oos.csv  --output reports\real_eurusd_oos_report.md
.\build\Release\vectra_cli.exe --config config\real_eurusd.json --data examples\real_eurusd_oos2.csv --output reports\real_eurusd_oos2_report.md
```

### 8.4 Live / Paper JSONL Streaming Mode (`--stream` and `--data -`)

`vectra_cli` supports real-time quote ingestion from `stdin` (`--data -`) and emits structured JSONL telemetry lines (`DECISION`, `FILL`, and `SUMMARY`) as events arrive:

```powershell
Get-Content examples\real_eurusd_dev.csv -TotalCount 5000 | .\build\Release\vectra_cli.exe --config config\real_eurusd.json --data - --stream
```

### 8.5 Walk-Forward Calibration and Periodic Site Update

```powershell
# Run walk-forward calibration strictly on DEV + VAL and evaluate locked config on OOS + OOS2
python scripts\calibrate_walk_forward.py

# Refresh docs/data/metrics.json and embedded snapshot in docs/index.html
python scripts\update_site_data.py
```

---

## 9. Repository Structure and Documentation Index

```text
VECTRA/
|-- CMakeLists.txt                       # CMake build definition (C++20, zero external dependencies)
|-- README.md                            # Complete architectural, mathematical, and empirical reference
|-- .github/workflows/pages.yml          # Automated GitHub Pages deployment workflow
|-- config/
|   |-- default.json                     # Baseline configuration for synthetic benchmark verification
|   `-- real_eurusd.json                 # Locked wall-clock configuration for real EUR/USD tick streams
|-- docs/
|   |-- index.html                       # Light-themed interactive GitHub Pages research portal
|   |-- .nojekyll                        # Disables Jekyll processing on GitHub Pages
|   |-- data/
|   |   `-- metrics.json                 # Structured JSON data bundle generated by update_site_data.py
|   |-- architecture.md                  # Detailed layer boundaries and data-flow invariants
|   |-- mathematical_model.md            # Complete mathematical specification of all formulas
|   |-- eurusd_empirical_study.md        # 4-way real EUR/USD empirical study & Brier calibration analysis
|   |-- testing_plan.md                  # Pre-registered testing, validation, and calibration protocol
|   |-- real_data_ingestion_plan.md      # Ingestion specification for institutional FX tick archives
|   |-- failure_modes.md                 # Failure catalog and defensive mitigation matrix
|   |-- project_identity.md              # Scope, terminology, and epistemic discipline
|   `-- roadmap.md                       # Engineering milestones and future work
|-- examples/
|   |-- sample_ticks.csv                 # 240-tick deterministic synthetic benchmark dataset
|   |-- real_eurusd_dev.csv              # Real EUR/USD DEV split (Ticks 1-100,000, 6.48 hours)
|   |-- real_eurusd_val.csv              # Real EUR/USD VAL split (Ticks 100,001-200,000, 2.95 hours)
|   |-- real_eurusd_oos.csv              # Real EUR/USD OOS 1 split (Ticks 200,001-300,000, 3.55 hours)
|   `-- real_eurusd_oos2.csv             # Real EUR/USD OOS 2 split (Ticks 300,001-461,357, 8.00 hours)
|-- include/vectra/                      # Public C++20 header files (core, market, behavior, exhaustion,
|                                        #   prediction, risk, execution, backtest, config)
|-- src/                                 # C++20 implementation files and CLI entrypoint (src/main.cpp)
|-- tests/                               # 115 unit and deterministic replay tests
|-- reports/                             # Generated Markdown audit reports for synthetic and real splits
`-- scripts/
    |-- fetch_real_eurusd.py             # Downloads and splits historical FXCM EUR/USD tick data
    |-- calibrate_walk_forward.py        # Walk-forward parameter calibration across DEV and VAL
    `-- update_site_data.py              # Parses Markdown reports and updates docs/data/metrics.json & index.html
```
