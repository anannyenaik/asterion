#!/usr/bin/env bash
set -euo pipefail

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target asterion_replay
./build/asterion_replay --input "${1:-data/samples/sample_replay.csv}" "${@:2}"
