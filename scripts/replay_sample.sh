#!/usr/bin/env bash
# VECTRA: run the sample replay and write reports/sample_report.md
#   ./scripts/replay.sh [path/to/ticks.csv]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATA="${1:-$ROOT/examples/sample_ticks.csv}"
OUT_DIR="$ROOT/reports"
mkdir -p "$OUT_DIR"

BIN="$ROOT/build/Release/vectra"
if [[ ! -x "$BIN" ]]; then
  BIN="$ROOT/build/vectra"
fi
if [[ ! -x "$BIN" ]]; then
  echo "error: vectra binary not found; run scripts/build.sh first" >&2
  exit 1
fi

"$BIN" --config "$ROOT/config/default.json" \
       --data "$DATA" \
       --report "$OUT_DIR/sample_report.md"
