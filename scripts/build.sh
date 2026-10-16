#!/usr/bin/env bash
# VECTRA: configure, build and test on Linux/macOS.
#   ./scripts/build.sh [Release|Debug]
set -euo pipefail

BUILD_TYPE="${1:-Release}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$ROOT/build" --parallel

echo
echo "== running tests =="
ctest --test-dir "$ROOT/build" --output-on-failure
