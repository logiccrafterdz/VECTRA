# VECTRA mathematical model (v0.1)

This document defines every formula implemented in code. All thresholds and
weights marked **[U]** are *uncalibrated starting values*: they are
configuration, not established constants, and no claim is made that they are
optimal, meaningful, or profitable.

The model is intentionally falsifiable: each quantity is computed from
observable quotes, each rule can fail to fire, and the engine reports negative
results (neutral signals, rejected predictions, losing simulated trades)
without suppression.

---

## 1. Price representation and midpoint

Quotes are stored in fixed-point integer units with scale `S = 1e-7`:

```
bid, ask ∈ ℤ,   value = bid / S
```

The midpoint used for all arithmetic:

```
mid = (bid + ask) / 2        (integer midpoint, exact)
```

Spread: `spread = ask - bid` (exact integer comparison against the configured
maximum; no floating-point equality is ever used for validation).

## 2. Observable log returns

For a horizon `h` measured in **events (ticks)**:

```
r_h(t) = log( mid(t) / mid(t - h) )
```

The engine computes `r_short = r_h1`, `r_mid = r_h2`, `r_long = r_h3` with
`h1 < h2 < h3` (defaults 8 / 32 / 128 events).

Derived observables:

| Name | Formula | Meaning |
|---|---|---|
| `disp_delta` | `r_h1(t) - r_h1(t-1)` | change of short displacement across updates |
| `disp_window_diff` | `r_h1(t) - r_h2(t)` | change of displacement across windows |
| `response` | `r_h / h` | displacement **per observable event (tick)** |
| `response_delta` | `response_h1 - response_h2` | response change across windows |
| `response_fade` | `clamp(1 - |response_h1| / (|response_h2| + ε), 0, 1)` | relative decline of per-tick response |

> ⚠️ `response` divides by **tick count**, not traded volume. Quotes carry no
> volume; tick count is a weak proxy for activity and is labelled as such.

## 3. Volatility and normalized displacement

Per-event log returns `x_i = log(mid_i / mid_{i-1})` are accumulated in a
rolling window of `N` samples (default 256) with Welford's algorithm:

```
variance = M2 / (N - 1)        (sample variance, N ≥ 2)
σ = sqrt(variance)
```

Guards:

* `N < min_vol_samples` (default 32) → `low_volatility` flag.
* `σ ≤ min_volatility` (default 1e-8) → `low_volatility` flag; `z` is set to 0
  and **never** divides by zero.

Volatility-normalized short displacement:

```
z = r_h1 / (σ · sqrt(h1))          if σ > 0 and not flagged, else 0
```

`sqrt(h1)` is the random-walk scaling of a sum of `h1` approximately
independent returns — an assumption, stated here so it can be tested.

## 4. Movement persistence

With `K` recent single-event returns (default 16) and
`s(x) = sign(x) ∈ {-1, 0, +1}`:

```
persistence = (1/K) · Σ s(x_i) · s(r_h1)        ∈ [-1, 1]
```

`persistence ≈ 1` means recent ticks agreed with the direction of the
short-horizon displacement; `≈ -1` means they opposed it. Requires
`K ≥ min_persistence_samples` (default 4) and `r_h1 ≠ 0`, otherwise 0.

## 5. Deceleration

How much the short-horizon movement has slowed **along its own direction**,
in units of the horizon's natural scale:

```
deceleration = clamp( s(r_h1) · (r_h1(t-1) - r_h1(t)) / (σ · sqrt(h1)), -1, 1 )
```

Positive = the move is slowing in the direction it is going; negative =
accelerating. Zero when the previous displacement is unavailable.

## 6. Movement-exhaustion score (research hypothesis)

```
extension      = clamp(|z| / z_norm, 0, 1)                    [U] z_norm = 3
decel_component = clamp(deceleration, 0, 1)
fade_component = clamp(response_fade, 0, 1)

spread_baseline = 0.5 · spread_reference_ratio                [U] 2.5e-4
spread_component = clamp((spread_ratio - spread_baseline) / spread_baseline, 0, 1)
quality = clamp( w_s·spread_component + w_g·stale_gap + w_a·low_activity, 0, 1 )
             [U] w_s = 0.6, w_g = 0.2, w_a = 0.2

exhaustion = ( w_e·extension + w_d·decel_component + w_f·fade_component
             + w_q·quality ) / (w_e + w_d + w_f + w_q)
             [U] 0.35 / 0.30 / 0.20 / 0.15
```

* A perfectly steady, extended move scores ≈ `w_e = 0.35`.
* A stop with `deceleration = fade = 1` scores ≈ `0.85`.
* Normal 1-pip spread contributes **zero** quality penalty (baseline
  subtraction); only genuinely elevated spreads, stale gaps or low activity do.

**This is a hypothesis, not a measurement of trader behavior.** The score is
composed exclusively of quote observables. It never asserts that a move was
caused by forced liquidation, stops or retail positioning — those are latent
variables the data cannot reveal.

## 7. Scoring predictor

Data gates (each returns `valid = false` and a rejection reason): invalid
event, insufficient history, stale gap, wide spread, low volatility.

If the gates pass:

```
gate         = clamp(|z| / min_extension_z, 0, 1)             [U] 1.5
p⁺           = clamp(persistence, 0, 1)

continuation_score = (1 - exhaustion) · gate · p⁺              ∈ [0,1]
reversal_score     =  exhaustion     · gate · (1 - p⁺)         ∈ [0,1]
```

Confidence (uncalibrated data-quality composite):

```
quality_c = clamp(1 - 0.25·spread_ratio/spread_reference - 0.20·low_activity, 0, 1)   [U]
sample    = clamp(vol_samples / 64, 0, 1)                                            [U]
confidence = quality_c · sample · gate                                                ∈ [0,1]
```

### Decision rule

With `d = s(r_h1)`:

| Condition | Outcome |
|---|---|
| `|z| < min_extension_z` or `r_h1 = 0` | **Neutral**, `InsufficientEvidence` |
| `exhaustion ≥ rev_thr` AND `persistence ≤ max_persist_rev` | **Reversal**, direction `-d` |
| `exhaustion ≤ cont_thr` AND `persistence ≥ min_persist` | **Continuation**, direction `+d` |
| neither rule fires | **Neutral**, `NoRuleMatch` |
| rule fires but `confidence < min_confidence` | **Neutral**, `LowConfidence` |

`[U] rev_thr = 0.65, cont_thr = 0.45, min_extension_z = 1.5,
min_persist = 0.25, max_persist_rev = 0.50, min_confidence = 0.30`

The band between `cont_thr` and `rev_thr` is the explicit **no-trade zone**;
neutral is a first-class outcome, not an error.

### Expected movement over the horizon

```
strength  = continuation_score  (Continuation) or reversal_score (Reversal), else 0
magnitude = clamp(|r_h1|, 0, max_expected_move_log)            [U] 0.004
expected_move_log = direction · strength · magnitude
```

A heuristic shrunk by conviction and capped — **not** a forecast distribution.

## 8. Calibration status

`continuation_score`, `reversal_score` and `confidence` are **scores in
[0,1]**, not probabilities. No calibration (reliability diagram, Brier score,
isotonic/Platt mapping) has been performed. Per the project rules they are
called *scores* until a calibration study on held-out data says otherwise.

## 9. Cost and edge model

For a quote with spread `S` (in price units):

```
cost          = S/2 + commission + slippage                 (per side)   [U]
expected_move = expm1(|expected_move_log|) · mid
edge          = expected_move - cost
allow iff edge ≥ min_expected_edge                                        [U] 1.5e-4
```

`expm1` converts the log return to a price move exactly (the linear
approximation drifts on larger moves).

## 10. What would falsify this model

1. **Continuation/reversal scores carry no information**: on held-out data,
   the hit rate of `Continuation` candidates is indistinguishable from the
   base rate of same-direction moves (binomial test / bootstrap CI), for
   every threshold in a pre-registered grid.
2. **Exhaustion does not lead**: `exhaustion` at time `t` shows no measurable
   association with `r_h(t+h)` sign change beyond what `z` alone explains.
3. **Costs dominate**: the edge filter admits trades whose post-cost outcome
   is negative on average — net P&L ≤ 0 across the whole out-of-sample period.
4. **Sensitivity**: small threshold perturbations (±10%) flip the sign of
   results, indicating the configuration is fitting noise.

A negative result on any of these is a *successful* experiment: the engine is
designed to report it.

## 11. Wall-clock mode and later extensions (as implemented)

Sections 1-10 describe the original v0.1 model. The configuration used for the
EUR/USD study ([`config/real_eurusd.json`](../config/real_eurusd.json)) enables
the extensions below. Each formula is transcribed from the source file named
in its heading; if code and this document disagree, the code is authoritative.

### 11.1 Wall-clock windows (`src/behavior/behavior_engine.cpp`)

With `window_short_ms / window_mid_ms / window_long_ms = 120000 / 240000 / 720000`,
for each window `W` the engine finds the oldest buffered event `k_W` with
`t_i - t_{k_W} <= tau_W` (provider timestamps, `source_time_ns`):

```
D_W  = ln(m_i / m_{k_W})              signed log displacement over W
n_W  = max(1, i - k_W)                ticks inside W
v_W  = D_W / n_W                      response: displacement per tick
z_W  = D_W / (sigma * sqrt(n_W))      sigma = rolling Welford std of tick log returns
```

### 11.2 Deceleration (wall-clock mode)

```
vol_scale    = sigma * sqrt(n_S)
D_prev       = D_M - D_S                        displacement in the 120 s before the short window
decel_inter  = sgn(D_S) * (D_prev - D_S) / (vol_scale * sqrt(2))
D_recent     = ln(m_i / m_{k_half})             last half of the short window (60 s)
D_older      = D_S - D_recent
decel_intra  = sgn(D_S) * (D_older - D_recent) / vol_scale
deceleration = clamp(max(decel_inter, decel_intra), -1, 1)
```

### 11.3 Response fade

```
fade_inter    = clamp(1 - |v_S| / |v_M|, 0, 1)
fade_intra    = clamp(1 - |D_recent / n_half| / |v_S|, 0, 1)    (wall-clock mode only)
response_fade = max(fade_inter, fade_intra)
```

### 11.4 Block regime efficiency

The long window is cut into `K = clamp(n_L / n_S, 2, 16)` (and `K <= n_L`)
contiguous blocks of equal tick count:

```
regime_efficiency = clamp(|D_L| / sum_k |ln(m_end(k) / m_start(k))|, 0, 1)
```

Values near 1 indicate a steady one-directional path; values near 0 indicate
oscillation. Continuation requires `regime_efficiency >= min_continuation_efficiency`
(0.55 in the study config); reversal requires `<= max_reversal_efficiency` (1.0, i.e. off).

### 11.5 Exhaustion score (`src/behavior/exhaustion.cpp`)

```
extension     = clamp(|z_S| / z_normalization, 0, 1)
deceleration  = clamp(deceleration, 0, 1)
response_fade = clamp(response_fade, 0, 1)
quality       = clamp(w_spread * spread_excess + w_stale * stale + w_activity * low_activity, 0, 1)
E = (0.30 * extension + 0.25 * deceleration + 0.35 * response_fade + 0.10 * quality) / (sum of weights)
```

### 11.6 Branch scores and expected move (`src/prediction/scoring_predictor.cpp`)

```
gate        = clamp(|z_S| / min_extension_z, 0, 1)
P+          = clamp(persistence, 0, 1)
C_cont      = (1 - E) * gate * P+
C_rev       = E * gate * (1 - P+)
strength    = C_branch * scale_branch          scale_cont = 1.00, scale_rev = 0.45
expected_move_log = direction * strength * clamp(|D_S|, 0, max_expected_move_log)
```

`scale_rev` compensates for `C_rev` being structurally larger than `C_cont`
(persistence is usually small, so `1 - P+` is close to 1). The expected move is
a heuristic magnitude, not a forecast distribution.

### 11.7 Round-trip cost gate (`src/risk/risk_engine.cpp`)

With `round_trip_cost = true`:

```
cost  = 2 * (spread / 2 + commission + slippage) = spread + 2 * (commission + slippage)
edge  = expm1(|expected_move_log|) * mid - cost
allow iff edge >= min_expected_edge (1e-4 in the study config) and all other gates pass
```

The other gates are: kill switch, risk disabled, invalid signal, no signal,
data health (invalid-tick ratio and staleness), spread ceiling, max position,
max exposure, daily loss limit, max open orders, max orders per day, and the
wall-clock cooldown (180 s).
