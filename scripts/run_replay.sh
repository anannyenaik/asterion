#!/usr/bin/env bash
set -euo pipefail

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target asterion_benchmarks
./build/asterion_benchmarks "${1:-data/samples/sample_replay.csv}"
