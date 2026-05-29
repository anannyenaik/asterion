#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-"$ROOT/build-debug"}"

cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DASTERION_ENABLE_WARNINGS=ON \
  -DASTERION_ENABLE_SANITIZERS=ON \
  -DASTERION_BUILD_PYTHON=ON
