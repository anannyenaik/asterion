#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-"$ROOT/build"}"
PYTHON="${PYTHON:-python3}"
SKIP_BUILD=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    --skip-build)
      SKIP_BUILD=1
      shift
      ;;
    -h|--help)
      echo "Usage: scripts/run_demo.sh [--build-dir build] [--skip-build]"
      exit 0
      ;;
    *)
      echo "unknown option: $1" >&2
      exit 1
      ;;
  esac
done

if [[ "$SKIP_BUILD" -eq 0 ]]; then
  cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DASTERION_ENABLE_WARNINGS=ON \
    -DASTERION_BUILD_PYTHON=ON
  cmake --build "$BUILD_DIR" --target \
    asterion_replay asterion_latency_budget asterion_benchmarks asterion_python_package
fi

export PYTHONPATH="$BUILD_DIR/python${PYTHONPATH:+:$PYTHONPATH}"
OUT_DIR="$BUILD_DIR/demo"
mkdir -p "$OUT_DIR"

tool_path() {
  local name="$1"
  if [[ -x "$BUILD_DIR/$name" ]]; then
    printf '%s\n' "$BUILD_DIR/$name"
  elif [[ -x "$BUILD_DIR/$name.exe" ]]; then
    printf '%s\n' "$BUILD_DIR/$name.exe"
  else
    echo "missing built tool: $name in $BUILD_DIR" >&2
    exit 1
  fi
}

section() {
  printf '\n== %s ==\n' "$1"
}

REPLAY="$(tool_path asterion_replay)"
LATENCY="$(tool_path asterion_latency_budget)"
BENCH="$(tool_path asterion_benchmarks)"
CLI="$ROOT/scripts/asterion_inspect.py"

section "CSV Replay"
"$REPLAY" --input "$ROOT/data/samples/sample_replay.csv" --no-diagnostics

section "Binary Replay"
"$REPLAY" --input "$ROOT/data/samples/sample_replay.bin" --format binary --no-diagnostics

section "Shared Vs Grouped Replay Parity"
"$PYTHON" "$CLI" replay-parity --input "$ROOT/data/samples/sample_replay.csv" --json

section "Diagnostics Summary"
"$PYTHON" "$CLI" diagnostics --input "$ROOT/data/samples/sample_replay.csv"

section "Risk Audit Summary"
"$PYTHON" "$CLI" audit-summary --input "$ROOT/data/samples/sample_risk_audit.jsonl"
"$PYTHON" "$CLI" audit-verify --input "$ROOT/data/samples/sample_risk_audit.jsonl"

section "Audit Manifest Verification"
MANIFEST="$OUT_DIR/sample_risk_audit.manifest.jsonl"
"$PYTHON" "$CLI" audit-manifest \
  --input "$ROOT/data/samples/sample_risk_audit.jsonl" \
  --output "$MANIFEST" \
  --json
"$PYTHON" "$CLI" audit-manifest-verify \
  --manifest "$MANIFEST" \
  --base-dir "$ROOT/data/samples" \
  --json

section "Portfolio Risk Snapshot"
"$PYTHON" "$CLI" portfolio-risk --input "$ROOT/data/samples/sample_portfolio_risk.json"

section "Latency-Budget Summary"
LATENCY_JSON="$OUT_DIR/latency_budget.json"
"$LATENCY" --iterations 200 --json "$LATENCY_JSON" --no-text
"$PYTHON" -m json.tool "$LATENCY_JSON" >/dev/null
"$PYTHON" - "$LATENCY_JSON" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as handle:
    payload = json.load(handle)
print(f"latency_budget_json={sys.argv[1]}")
print(f"stage_count={len(payload.get('stages', []))}")
print(f"exceeded_count={payload.get('exceeded_count', 0)}")
print(f"config_checksum={payload.get('config_checksum', 0)}")
PY

section "Benchmark JSON Generation"
BENCH_JSON="$OUT_DIR/asterion_benchmark.json"
"$BENCH" --json "$BENCH_JSON" --no-text
"$PYTHON" -m json.tool "$BENCH_JSON" >/dev/null
"$PYTHON" - "$BENCH_JSON" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as handle:
    payload = json.load(handle)
print(f"benchmark_json={sys.argv[1]}")
print(f"benchmark_count={len(payload.get('benchmarks', []))}")
print("benchmark_results_committed=false")
PY

section "Demo Complete"
echo "generated_outputs=$OUT_DIR"
