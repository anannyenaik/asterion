#!/usr/bin/env bash
set -euo pipefail

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASTERION_ENABLE_SANITIZERS=ON
cmake --build build
ctest --test-dir build --output-on-failure
