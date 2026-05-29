#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

rm -rf \
  "$ROOT/build" \
  "$ROOT"/build-* \
  "$ROOT/data/generated" \
  "$ROOT/benchmarks/history" \
  "$ROOT/benchmarks/results" \
  "$ROOT/.pytest_cache"

find "$ROOT" -type d -name __pycache__ -prune -exec rm -rf {} +
