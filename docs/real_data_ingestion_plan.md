# VECTRA v0.1 - Real Data Ingestion Plan (Preparation only, no execution)

This document is a preparation-only plan. It does not claim that any specific provider has been verified, and it does not propose tuning or optimization.

## 1) Recommended starting point
- Pair: EUR/USD (one pair, start small). 
- Rationale: high liquidity, widely available tick history, standard format.

## 2) Schema and precision requirements
Required CSV columns (case-insensitive header optional):
- symbol (must match config.instrument when set)
- bid, ask (decimal, ask >= bid)
- source_ts_ns (nanoseconds since epoch, int64; if provider gives ms/us, scale without losing precision)
- sequence (optional) if available for duplicate/out-of-order detection

Constraints from codebase: fixed-point Price with kPriceScale=10^7; source_ts_ns required by default; validator uses last_time_ns/last_seq_.

## 3) Timestamp/zone requirements
- source_ts_ns is authoritative; never mix provider time with local steady_clock for ordering.
- TZ must be documented (prefer UTC). Any conversion must be reproducible and lossless in ns precision.

## 4) Provider checklist (verify before committing)
- Bid/Ask present for each tick (not mid-only).
- Timestamps convertible to ns; original precision known.
- License for research/download/retention (not inferred).
- Coverage period and gaps; out-of-order/duplicate prevalence.
- Export format matches above or mappable with deterministic normalization.

## 5) Candidate historical sources (unverified)
- Dukascopy Historical Data (ticks): common; check columns/timezone/precision.
- TrueFX (historical): granularity varies; timestamp format verification needed.
- HistData.com: format varies; may need normalization.
- FXCM historical: verify license/format.
- Exness/Dukascopy exports: normalize to required schema.

## 6) Ingestion-only acceptance test
- Parse-only: count malformed/skipped/valid; classify rejections per validator reasons (Malformed, WrongSymbol, NonPositivePrice, NegativeSpread, WideSpread, Duplicate, OutOfOrder, Stale, FutureTimestamp, StaleGap).
- Quality gates: documented rejection rate bounds by reason; no unclassified failures.
- Small subset first (e.g. 1-2 hours). No profitability, no threshold/model changes.

## 7) Reproducible validation commands
- Inspect: head -5, wc -l, header presence
- Parse/dry run: use existing CSV reader via replay in validation-only mode; log counts per reason
- Replay subset: confirm validator rejections match documented reasons; unchanged thresholds.

## 8) Guardrails
- No fabrication, no tuning/optimization/grid search, no live broker, no real-money trading. Ingestion-only.
