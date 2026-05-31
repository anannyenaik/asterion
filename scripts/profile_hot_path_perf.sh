#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-"$ROOT/build-perf"}"
DATASET="${DATASET:-"$ROOT/data/samples/sample_hot_path_replay.bin"}"
HOT_PATH_ITERATIONS="${HOT_PATH_ITERATIONS:-10000}"
PERF_REPEAT="${PERF_REPEAT:-5}"
PERF_FREQ="${PERF_FREQ:-999}"

cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DASTERION_BUILD_TESTS=OFF \
  -DASTERION_BUILD_BENCHMARKS=ON \
  -DASTERION_BUILD_PYTHON=OFF
cmake --build "$BUILD_DIR" --target asterion_benchmarks

perf stat -d -r "$PERF_REPEAT" \
  "$BUILD_DIR/asterion_benchmarks" \
  --dataset "$DATASET" \
  --hot-path-iterations "$HOT_PATH_ITERATIONS" \
  --json "$BUILD_DIR/hot_path_benchmark.json" \
  --no-text

perf record -F "$PERF_FREQ" -g -- \
  "$BUILD_DIR/asterion_benchmarks" \
  --dataset "$DATASET" \
  --hot-path-iterations "$HOT_PATH_ITERATIONS" \
  --no-text

if command -v stackcollapse-perf.pl >/dev/null 2>&1 && command -v flamegraph.pl >/dev/null 2>&1; then
  perf script | stackcollapse-perf.pl > "$BUILD_DIR/hot_path.folded"
  flamegraph.pl "$BUILD_DIR/hot_path.folded" > "$BUILD_DIR/hot_path_flamegraph.svg"
  echo "flamegraph=$BUILD_DIR/hot_path_flamegraph.svg"
else
  echo "FlameGraph scripts not found. To render manually:"
  echo "  perf script | stackcollapse-perf.pl > $BUILD_DIR/hot_path.folded"
  echo "  flamegraph.pl $BUILD_DIR/hot_path.folded > $BUILD_DIR/hot_path_flamegraph.svg"
fi
