#!/usr/bin/env bash
set -euo pipefail

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_BUILD_BENCHMARKS=ON
cmake --build build --target asterion_benchmarks
./build/asterion_benchmarks "$@"
