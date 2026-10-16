
## 5) Real historical data sources (pros/cons; not verified)
Candidates (to investigate, unverified unless explicitly confirmed):
- Dukascopy Historical Data (ticks): common, needs careful extraction; check column order, timezone, precision.
- TrueFX (historical): tick granularity varies; verify timestamp format.
- HistData.com: tick CSVs vary by format; normalization may be required.
- Exness/Dukascopy exports: must be normalized to symbol,bid,ask,source_ts_ns[,sequence].
- FXCM historical: verify license/format; avoid non-compliant reuse.

Note: provider availability/license must be checked; no provider is asserted as suitable here.

## 6) Ingestion validation commands (reproducible)
- Inspect CSV: show header and first 5 lines (for schema check).
- Parse-only dry run: count malformed/skipped and valid using existing CSV reader if exposed; else log via replay with validation-only path (document the exact command).
- Full replay on real subset: verify validator rejections match documented reasons; ensure no changes to config thresholds.

## 7) Guardrails
- No fabrication of real data, no model tuning/optimization/grid search, no live broker, no real-money trading.
- Ingestion-only: no changes to prediction thresholds, behavior, or risk parameters.
