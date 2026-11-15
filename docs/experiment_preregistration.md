# VECTRA Large-Scale Empirical Pre-Registration & Falsification Protocol (v1.3)

**Status**: Pre-Registered & Locked Prior to Data Acquisition  
**Git Tag**: `prereg-v1.3`  
**Target Evaluation Period**: 2023-01-01 00:00:00 UTC to 2023-03-31 23:59:59 UTC (Q1 2023, 3 full calendar months)  
**Target Instruments**: `EURUSD`, `GBPUSD`, `USDJPY`  
**Test Harness**: [`scripts/run_experiment_suite.py`](../scripts/run_experiment_suite.py)

---

## 1. Primary Research Question & Single Confirmatory Hypothesis

### 1.1 The Primary Hypothesis ($H_1$)
On continuous, untouched tick data for `EURUSD` during Q1 2023, the full composite VECTRA model (combining multi-horizon displacement, deceleration, response fade, and block regime efficiency) generates a positive mean net return per independent non-overlapping 180-second horizon block after deducting full entry and exit spread plus baseline round-trip friction of 0.30 pips:

$$H_0: \mu_{\text{net}} \le 0 \quad \text{versus} \quad H_1: \mu_{\text{net}} > 0$$

where $\mu_{\text{net}}$ is the expected net return across independent (non-overlapping) 180 s signal blocks.

### 1.2 Formal Test of the Primary Hypothesis
The primary statistical test is a **one-sample Student $t$-test against zero** on the sequence of non-overlapping block net returns $R_{\text{net}, b}$:

$$t = \frac{\bar{R}_{\text{net}}}{\text{SE}(\bar{R}_{\text{net}})} = \frac{\frac{1}{B}\sum_{b=1}^B R_{\text{net}, b}}{\sqrt{\frac{1}{B(B-1)}\sum_{b=1}^B (R_{\text{net}, b} - \bar{R}_{\text{net}})^2}}$$

- **Sample Population for Primary Test**: Independent non-overlapping signal blocks ($B$). Signals are evaluated at $t + 180\text{ s}$ under spread and friction.
- **Two-Tailed Significance Threshold**: $\alpha = 0.05 \implies t > 1.96$.
- **Economic Threshold**: Mean block net return $\bar{R}_{\text{net}} \ge 0.15\text{ pips}$ ($0.15 \times \text{pip\_size}$).
- **Secondary Feasibility Condition**: Realized cumulative Net P&L of executed trades $> 0$.

### 1.3 Secondary Paired Test Against Independent Pseudo-Random Baseline
On identical non-overlapping blocks where the model emits a usable signal, an independent pseudo-random directional baseline enters at the exact same quote:
$$d_{\text{rand}, b} \in \{-1, +1\} \quad \text{via deterministic hash of } t_b$$
Both the model and the random baseline pay the identical spread and friction. The paired difference $d_b = R_{\text{model}, b} - R_{\text{rand}, b}$ is evaluated via a **paired Student $t$-test**:

$$t_{\text{paired}} = \frac{\bar{d}}{\text{SE}(\bar{d})} > 1.96 \quad (\alpha = 0.05, \text{two-tailed})$$

### 1.4 Secondary & Exploratory Families (Holm-Bonferroni Corrected)
1. **Family A (Instrument Exploratory Ablations)**: The 8 secondary exploratory tests per instrument (4 LOO + 4 Single-feature) are adjusted via Holm-Bonferroni step-down correction at $\alpha = 0.05$.
2. **Family B (Cross-Instrument Replication)**: Confirmatory replication on secondary instruments (`GBPUSD` and `USDJPY`) is evaluated with Holm-Bonferroni correction across the 3 instruments.

---

## 2. Pre-Committed Configuration Files & Scaling Rules

To prevent post-hoc parameter adjustments across currency pairs with different price conventions, configuration files are explicitly committed prior to data ingestion:

| Instrument | Configuration File | Pip Size | Absolute Price Parameters | Dimensionless Relative Ratios |
|---|---|---:|---|---|
| **EURUSD** | [`config/experiment_eurusd.json`](../config/experiment_eurusd.json) | `0.0001` | `max_spread=0.00025`, `edge=1e-4`, `SL=8e-4`, `TP=12e-4`, `comm=5e-6`, `slip=1e-5` | `spread_warn=0.00025`, `spread_ref=0.00020` |
| **GBPUSD** | [`config/experiment_gbpusd.json`](../config/experiment_gbpusd.json) | `0.0001` | `max_spread=0.00030`, `edge=1e-4`, `SL=8e-4`, `TP=12e-4`, `comm=5e-6`, `slip=1e-5` | `spread_warn=0.00030`, `spread_ref=0.00020` |
| **USDJPY** | [`config/experiment_usdjpy.json`](../config/experiment_usdjpy.json) | `0.01` | `max_spread=0.025`, `edge=0.01`, `SL=0.08`, `TP=0.12`, `comm=0.0005`, `slip=0.0010` | `spread_warn=0.00025`, `spread_ref=0.00020` |

### Explicit Parameter Scaling Distinction
1. **Absolute Price Parameters**: Fields measured in price units (`max_spread`, `commission`, `slippage`, `min_expected_edge`, `stop_loss_price`, `take_profit_price`) scale proportionally with `pip_size`:
   $$\text{Parameter}_{\text{pair}} = \text{Parameter}_{\text{base (EURUSD)}} \times \left(\frac{\text{pip\_size}_{\text{pair}}}{0.0001}\right)$$
2. **Dimensionless Relative Ratios**: Fields representing spread as a fraction of mid-price (`spread_warn_ratio = 0.00025`, `spread_reference_ratio = 0.00020`) in `behavior_engine.cpp`:
   $$f.\text{spread\_ratio} = \frac{\text{spread}}{\text{mid}}$$
   These are pure basis-point fractions (2.5 bps and 2.0 bps) and **must NOT be scaled by `pip_size`**. Scaling them by 100 on USD/JPY would set the warning spread threshold to 330 pips, effectively disabling spread filtering.

---

## 3. Explicit Three-Tier Outcome Taxonomy (Resolving the Gray Zone)

To prevent ambiguous interpretations or post-hoc justifications of weak results, the outcome of the primary evaluation is partitioned into three mutually exclusive tiers:

| Outcome Tier | Statistical & Economic Criteria | Scientific Conclusion & Mandatory Next Step |
|---|---|---|
| **Tier 1: Confirmed Edge (Success)** | Primary $t > 1.96$ ($p < 0.05$) **AND** block mean net return $\bar{R}_{\text{net}} \ge 0.15\text{ pips}$ **AND** cumulative Net P&L $> 0$. | The microstructural exhaustion hypothesis is confirmed on untouched data. The model advances to multi-year and live paper testing. |
| **Tier 2: Definitive Falsification (Failure)** | Primary $t \le 0$ **OR** block mean net return $\bar{R}_{\text{net}} \le 0$ **OR** cumulative Net P&L $\le 0$ at baseline friction ($0.30\text{ pips}$) across the 3-month sample. | The hypothesis is formally **falsified**. No parameters will be re-tuned on this dataset. The result is published as a definitive negative finding. |
| **Tier 3: Inconclusive (Gray Zone)** | $0 < t \le 1.96$, **OR** Net P&L $> 0$ with $\bar{R}_{\text{net}} < 0.15\text{ pips}$ (insufficient statistical power or economic margin). | The result is declared **inconclusive**. The dataset is frozen and archived. **No re-tuning or re-running is permitted on this data.** Any further investigation requires a freshly pre-registered multi-year sample. |

---

## 4. Statistical Power and Sample Size Requirements

Assuming a typical 180 s foreign exchange trade return standard deviation $\sigma \approx 3.5\text{ pips}$, the sample size $N$ required to detect an economically viable edge of $\mu = 0.20\text{ pips}$ with $80\%$ statistical power ($1 - \beta = 0.80$) at a two-tailed significance level $\alpha = 0.05$ is:

$$N_{\text{min}} = \left(\frac{z_{1-\alpha/2} + z_{1-\beta}}{\mu / \sigma}\right)^2 = \left(\frac{1.96 + 0.84}{0.20 / 3.50}\right)^2 = (2.80 \times 17.5)^2 \approx 2,401\text{ independent blocks}$$

Across 3 calendar months (approx. 65 trading days), foreign exchange markets produce continuous tick streams yielding thousands of candidate events. The engine records the exact number of independent, non-overlapping 180 s blocks in every report to verify statistical power.

---

## 5. Ablation and Diagnostic Protocol

Composite momentum exhaustion is hypothesized to be a **synergistic interaction** of extension, deceleration, response fade, and block regime efficiency. To measure the marginal contribution of each component without destroying signal generation, two complementary diagnostic batteries are pre-registered and implemented directly in `src/prediction/scoring_predictor.cpp`:

### 5.1 Leave-One-Out (LOO) Diagnostic Battery
Evaluates the full model with exactly one component disabled:

| LOO Configuration | Modification to Full Model | Diagnostic Purpose |
|---|---|---|
| **LOO 1 (`loo_no_extension`)** | $w_{\text{ext}} = 0.0$, remove $|z_{\text{ext}}| \ge 1.8$ hard gate | Measures necessity of statistical price stretch. |
| **LOO 2 (`loo_no_deceleration`)** | $w_{\text{dec}} = 0.0$, inter/intra deceleration ignored | Measures necessity of velocity stall detection. |
| **LOO 3 (`loo_no_fade`)** | $w_{\text{fade}} = 0.0$, response fade ignored | Measures necessity of impulse exhaustion detection. |
| **LOO 4 (`loo_no_efficiency`)** | `min_continuation_efficiency` = 0.0 | Measures necessity of multi-block trend linearity. |

### 5.2 Single-Feature Isolation Rules (Exploratory)
To test whether any individual feature possesses standalone alpha, each feature is tested with an isolated decision rule with identical 180 s hold and 180 s cooldown.
> **Note on Threshold Derivation & Confidence Bypass**: The thresholds (0.50 for deceleration and fade, 0.60 for regime efficiency) are **heuristic round thresholds chosen manually** based on theoretical feature bounds to represent significant stall (half the normalized scale) and high directional persistence (60% path linearity). In single-feature mode, `confidence = 1.0` is assigned to isolate raw directional alpha from composite data-quality penalties:
- **Single 1 (`single_extension`)**: Enter Reversal when $|z_{\text{ext}}| \ge 1.8$, direction $= -\operatorname{sgn}(z_{\text{ext}})$.
- **Single 2 (`single_deceleration`)**: Enter Reversal when $\text{decel} \ge 0.50$, direction $= -\operatorname{sgn}(D_S)$.
- **Single 3 (`single_fade`)**: Enter Reversal when $\text{fade} \ge 0.50$, direction $= -\operatorname{sgn}(D_S)$.
- **Single 4 (`single_efficiency`)**: Enter Continuation when $E_{\text{regime}} \ge 0.60$, direction $= +\operatorname{sgn}(D_M)$.

---

## 6. Friction Definition & Cost Sensitivity Protocol

### 6.1 Transaction Cost Formulation
Dukascopy tick data records raw historical bid and ask quotes, preserving true market spreads ($s_i = a_i - b_i$). 

The parameter $\delta \in [0.1, 1.0]\text{ pips}$ denotes **additional round-trip friction** (broker commission plus execution slippage):

$$\text{Round-Trip Cost}_i = s_i + \delta = (a_i - b_i) + 2 \times (\text{commission} + \text{slippage})$$

- **Baseline Friction**: $\delta = 0.30\text{ pips}$ ($0.15\text{ pips}$ entry $+ 0.15\text{ pips}$ exit, corresponding to commission = $5\times 10^{-6}$ and slippage = $1\times 10^{-5}$ on EURUSD).
- **Friction Allocation Across Grid**: Commission is fixed at $0.05\text{ pips}$ per leg ($0.10\text{ pips}$ round-trip). Additional slippage is allocated as $\text{slippage} = \max(0, (\delta - 0.10) / 2)\text{ pips}$.
- **Risk Gate Invariant**: The pre-trade risk parameter `min_expected_edge` remains **strictly constant at 1.0 pip** across all friction levels.

### 6.2 Dual Friction Curves
Because pre-trade edge gates and cooldown timers dynamically shift the set of executed trades when friction varies, the experiment evaluates two distinct curves side-by-side:
1. **Monotonic Fixed-Execution Curve**: Takes the exact trades executed under baseline friction ($\delta = 0.30\text{ pips}$) and adjusts net profit passively:
   $$\text{Net P\&L}_{\text{fixed}}(\delta) = \text{Net P\&L}_{\text{base}} - (\delta - 0.30) \times \text{pip\_size} \times \text{order\_units} \times \left(\frac{N_{\text{fills}}}{2}\right)$$
   This curve is **strictly, linearly monotonic decreasing** with respect to $\delta$, providing an exact algebraic break-even point:
   $$\delta^*_{\text{fixed}} = 0.30 + \frac{\text{Net P\&L}_{\text{base}}}{\text{pip\_size} \times \text{order\_units} \times (N_{\text{fills}} / 2)}$$
2. **Dynamic Re-Execution Curve**: Re-runs the full engine at each friction level, reporting how the strategy's risk gate and cooldown timers respond end-to-end.

---

## 7. Pre-Flight Verification on Historical & Synthetic Data

Prior to downloading Q1 2023 data, the complete experimental pipeline was verified end-to-end using `scripts/run_experiment_suite.py` on the existing 2018 EUR/USD dataset and synthetic USD/JPY ticks:
- Verified that all 4 LOO modes and all 4 Single-feature rules execute and emit expected order counts without runtime errors.
- Verified that the monotonic fixed-execution curve is strictly decreasing.
- Verified that the USD/JPY pip scaling correctly scales by $100\times$ (`1.0 / 0.01`).
- Verified that all 117 unit tests pass cleanly.

---

## 8. Data Ingestion, Exclusions & Verification Hash Protocol

### 8.1 Data Provenance & Tooling
- **Source**: Public institutional tick history from Dukascopy Bank SA.
- **Ingestion Tool**: Open-source `dukascopy-node` CLI exporting raw ticks (timestamp, bid, ask, volume) in UTC format.
- **Period**: 2023-01-01 00:00:00 UTC to 2023-03-31 23:59:59 UTC.

### 8.2 Pre-Registered Exclusions
1. **Weekend Closures**: Quotes timestamped between Friday 21:00 UTC and Sunday 21:00 UTC are discarded.
2. **Daily Rollover Window**: Quotes between 21:00 UTC and 22:00 UTC every trading day are excluded from trade entry to eliminate artificial rollover spread spikes.
3. **Market Hygiene Rejections**: Crossed spreads ($a_i \le b_i$), non-positive prices, timestamps out of sequence, and stale quote gaps $> 15,000\text{ ms}$ are filtered by `MarketValidator`.

### 8.3 Data Integrity & SHA-256 Manifest
Immediately upon downloading each dataset, a cryptographic manifest [`reports/data_manifest_q1_2023.json`](../reports/data_manifest_q1_2023.json) will be written containing:
- Exact file name, byte size, and tick count.
- `SHA-256` checksum of the raw downloaded file.
- First and last provider timestamp in UTC.
