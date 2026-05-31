#!/usr/bin/env bash
# Linux perf profiling for the Asterion hot path.
#
# Manual, Linux-only helper. It runs the asterion_benchmarks hot path under
# `perf stat -d` (and optionally `perf record` + a flamegraph) over a chosen
# replay corpus, writing plain-text output under an ignored local directory.
#
# It NEVER fakes results. If `perf` is missing it explains and exits non-zero
# (so a wrapper can record the blocker). If hardware counters are not exposed
# (common on GitHub-hosted runners and locked-down kernels), perf itself reports
# "<not supported>" / "<not counted>" and those lines are kept verbatim.
#
# These are representative measurements on the machine/environment that runs the
# script, not portable performance claims. This profiles a deterministic replay
# stress workload; it is not live trading and not a production-HFT benchmark.
#
# Usage:
#   scripts/run_linux_perf_profile.sh [options]
#
# Options:
#   --dataset PATH        Replay corpus (binary or CSV). Default:
#                         build/perf_corpora/baseline_1m.bin
#   --path VARIANT        standard | pooled | both  (default: both)
#                         The benchmark emits both hot-path rows in one process;
#                         this selects which row is highlighted in the summary.
#                         perf counters always cover the whole hot-path process.
#   --build-dir DIR       Build directory (default: build)
#   --iterations N        Measured hot-path iterations (default: 30)
#   --warmup N            Warm-up iterations (default: 5)
#   --output-dir DIR      Output directory (default: build/perf_profile)
#   --record              Also run `perf record -F 99 -g` and `perf report --stdio`
#   --flamegraph          With --record, fold stacks into an SVG flamegraph if the
#                         FlameGraph tools (stackcollapse-perf.pl, flamegraph.pl)
#                         are on PATH
#   -h, --help            Show this help
set -euo pipefail

DATASET="build/perf_corpora/baseline_1m.bin"
VARIANT="both"
BUILD_DIR="build"
ITERATIONS=30
WARMUP=5
OUTPUT_DIR="build/perf_profile"
DO_RECORD=0
DO_FLAME=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dataset) DATASET="$2"; shift 2 ;;
    --path) VARIANT="$2"; shift 2 ;;
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --iterations) ITERATIONS="$2"; shift 2 ;;
    --warmup) WARMUP="$2"; shift 2 ;;
    --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
    --record) DO_RECORD=1; shift ;;
    --flamegraph) DO_FLAME=1; shift ;;
    -h|--help) sed -n '2,40p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

case "$VARIANT" in
  standard|pooled|both) ;;
  *) echo "--path must be standard|pooled|both" >&2; exit 2 ;;
esac

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "ERROR: this script targets Linux perf; current OS is $(uname -s)." >&2
  echo "On non-Linux hosts use scripts/run_perf_evaluation.py for portable" >&2
  echo "allocation/latency measurements; perf is not available." >&2
  exit 3
fi

if ! command -v perf >/dev/null 2>&1; then
  echo "ERROR: 'perf' not found on PATH. Install linux-tools for your kernel," >&2
  echo "e.g. 'sudo apt-get install linux-tools-common linux-tools-\$(uname -r)'." >&2
  echo "No results were produced and nothing was faked." >&2
  exit 4
fi

BENCH="$BUILD_DIR/asterion_benchmarks"
if [[ ! -x "$BENCH" ]]; then
  echo "ERROR: $BENCH not found. Build it first:" >&2
  echo "  cmake --build $BUILD_DIR --target asterion_benchmarks" >&2
  exit 5
fi
if [[ ! -f "$DATASET" ]]; then
  echo "ERROR: dataset not found: $DATASET" >&2
  echo "Generate corpora first: python scripts/run_perf_evaluation.py" >&2
  exit 6
fi

mkdir -p "$OUTPUT_DIR"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
BASE="$(basename "$DATASET")"
STAT_OUT="$OUTPUT_DIR/perf_stat_${BASE}_${VARIANT}_${STAMP}.txt"

BENCH_ARGS=(--dataset "$DATASET" --only-hot-path
            --hot-path-warmup "$WARMUP" --hot-path-iterations "$ITERATIONS")

{
  echo "# Asterion Linux perf profile"
  echo "# These are representative measurements on this machine/environment,"
  echo "# not portable performance claims. Not live trading; not production HFT."
  echo "timestamp_utc : $STAMP"
  echo "host          : $(uname -srm)"
  echo "kernel        : $(uname -r)"
  echo "cpu_model     : $(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | sed 's/^ //')"
  echo "dataset       : $DATASET"
  echo "highlight_path: $VARIANT (perf counters cover the whole hot-path process,"
  echo "                which runs both standard and pooled rows in one pass)"
  echo "warmup        : $WARMUP"
  echo "iterations    : $ITERATIONS"
  echo "benchmark     : $BENCH ${BENCH_ARGS[*]}"
  echo
  echo "## perf stat -d"
} > "$STAT_OUT"

EVENTS="cycles,instructions,branches,branch-misses,cache-references,cache-misses,context-switches,page-faults"
# Append perf stat (stderr) verbatim. Do not fail the script if some counters
# are unsupported; perf prints "<not supported>" and we keep it.
set +e
perf stat -d -e "$EVENTS" -- "$BENCH" "${BENCH_ARGS[@]}" >>"$STAT_OUT" 2>>"$STAT_OUT"
PERF_RC=$?
set -e
echo >> "$STAT_OUT"
echo "perf stat exit code: $PERF_RC (non-zero often means PMU counters are not" >> "$STAT_OUT"
echo "exposed on this host/runner; see lines above for <not supported>)." >> "$STAT_OUT"
echo "wrote $STAT_OUT"

if [[ "$DO_RECORD" -eq 1 ]]; then
  DATA="$OUTPUT_DIR/perf_${BASE}_${VARIANT}_${STAMP}.data"
  REPORT_OUT="$OUTPUT_DIR/perf_report_${BASE}_${VARIANT}_${STAMP}.txt"
  set +e
  perf record -F 99 -g -o "$DATA" -- "$BENCH" "${BENCH_ARGS[@]}" >/dev/null 2>&1
  REC_RC=$?
  set -e
  if [[ "$REC_RC" -eq 0 ]]; then
    perf report --stdio -i "$DATA" > "$REPORT_OUT" 2>/dev/null || true
    echo "wrote $REPORT_OUT"
    if [[ "$DO_FLAME" -eq 1 ]]; then
      if command -v stackcollapse-perf.pl >/dev/null 2>&1 \
         && command -v flamegraph.pl >/dev/null 2>&1; then
        FLAME="$OUTPUT_DIR/flame_${BASE}_${VARIANT}_${STAMP}.svg"
        perf script -i "$DATA" | stackcollapse-perf.pl | flamegraph.pl > "$FLAME"
        echo "wrote $FLAME"
      else
        echo "FlameGraph tools (stackcollapse-perf.pl/flamegraph.pl) not on PATH;" >&2
        echo "skipping flamegraph. See https://github.com/brendangregg/FlameGraph" >&2
      fi
    fi
  else
    echo "perf record failed (rc=$REC_RC); skipping report/flamegraph." >&2
  fi
fi

echo "Done. Outputs under $OUTPUT_DIR (git-ignored)."
