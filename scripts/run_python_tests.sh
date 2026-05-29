#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-"$ROOT/build"}"
PYTHON="${PYTHON:-python3}"

export PYTHONPATH="$BUILD_DIR/python${PYTHONPATH:+:$PYTHONPATH}"
"$PYTHON" -m pytest "$ROOT/python/tests"
