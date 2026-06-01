# Perf Profile

Representative local measurements on this machine, not portable performance claims.

## Local Status

Linux `perf` was not run in the current environment.

Blocker (updated 2026-06-01):

- The active shell is Windows PowerShell; `perf` is not available on `PATH`.
- WSL2 itself now launches on this host (the earlier `WSL_E_WSL_OPTIONAL_COMPONENT_REQUIRED`
  blocker was cleared by a reboot): `wsl --version` reports `2.7.3.0` / kernel `6.6.114.1-1`.
- However, no Linux distribution can start, because hardware virtualization is **disabled in the
  machine firmware (BIOS/UEFI)**. `wsl --install -d Ubuntu --no-launch` downloaded and installed
  Ubuntu but failed to create the VM with `HCS_E_HYPERV_NOT_INSTALLED`
  ("virtualization is not enabled on this machine"). `systeminfo` confirms
  `Virtualization Enabled In Firmware: No` (with `VM Monitor Mode Extensions: Yes` and
  `Second Level Address Translation: Yes`, i.e. the CPU supports it but firmware has it switched off).
- To unblock here: enable Intel VT-x / AMD-V in the BIOS/UEFI, reboot, then re-run WSL2 setup; or run
  the Linux commands below on a native Linux / cloud Linux host where the PMU is exposed.

No `perf stat` counters or flamegraph samples are fabricated in this report.

## Linux Commands

The helper script below builds the benchmark target, runs `perf stat -d`, records call stacks and
renders a flamegraph when Brendan Gregg's FlameGraph scripts are installed:

```bash
BUILD_DIR=build-perf \
HOT_PATH_ITERATIONS=10000 \
DATASET="$PWD/data/samples/sample_hot_path_replay.bin" \
./scripts/profile_hot_path_perf.sh
```

Equivalent manual commands:

```bash
cmake -S . -B build-perf -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DASTERION_BUILD_TESTS=OFF \
  -DASTERION_BUILD_BENCHMARKS=ON \
  -DASTERION_BUILD_PYTHON=OFF
cmake --build build-perf --target asterion_benchmarks

perf stat -d -r 5 \
  ./build-perf/asterion_benchmarks \
  --dataset data/samples/sample_hot_path_replay.bin \
  --hot-path-iterations 10000 \
  --json build-perf/hot_path_benchmark.json \
  --no-text

perf record -F 999 -g -- \
  ./build-perf/asterion_benchmarks \
  --dataset data/samples/sample_hot_path_replay.bin \
  --hot-path-iterations 10000 \
  --no-text
perf script | stackcollapse-perf.pl > build-perf/hot_path.folded
flamegraph.pl build-perf/hot_path.folded > build-perf/hot_path_flamegraph.svg
```

Keep raw `perf.data`, folded stacks and SVGs local unless they are intentionally curated with
environment metadata.
