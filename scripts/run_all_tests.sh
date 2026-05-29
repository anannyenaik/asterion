#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-"$ROOT/build"}"
PYTHON="${PYTHON:-python3}"

BUILD_DIR="$BUILD_DIR" "$ROOT/scripts/configure_release.sh"
cmake --build "$BUILD_DIR"
ctest --test-dir "$BUILD_DIR" --output-on-failure
BUILD_DIR="$BUILD_DIR" PYTHON="$PYTHON" "$ROOT/scripts/run_python_tests.sh"
