# Native / Cloud Linux Performance Runbook

This runbook contains the procedure for collecting native or PMU-capable cloud
Linux performance-counter and latency-distribution evidence. It contains no
measurements.

**Status:** deferred pending access to a distinct native or PMU-capable Linux
host. As of 2026-06-04, the available Linux environment is WSL2, already measured
at commit `216f473` and documented in
[reports/linux_performance_evaluation_2026_06_01.md](../reports/linux_performance_evaluation_2026_06_01.md).
Those results must not be relabelled as native Linux.

The procedure evaluates deterministic replay systems cost under disclosed host
conditions. It does not evaluate live trading, predictive quality or portable
latency.

## What the native pass adds over the existing WSL2 pass

The WSL2 pass already produced real cycles/instructions/IPC/branch/cache counters
and `perf record` hotspots, but with documented environment limits this pass is meant to
remove:

| WSL2 limitation (commit 216f473) | What native/bare-metal Linux fixes |
| --- | --- |
| Virtualised PMU: `LLC-loads`/`LLC-load-misses` report `<not supported>` | Real PMU exposes LLC events |
| Counter time-multiplexing (≈49–75% scaling) | More native counters → less/no multiplexing |
| No `cpufreq` governor, no `intel_pstate` → frequency uncontrolled (~2.4–2.9 GHz) | `governor=performance` + turbo disabled → stable frequency (disclosed) |
| `-O3` frame-pointer omission → partial call graphs | `-fno-omit-frame-pointer` profiling build / DWARF call graphs → fuller flamegraphs |
| GCC only | GCC **and** Clang Release, compared |
| ONNX not compiled | optionally `-DASTERION_USE_ONNXRUNTIME=ON` to measure the ONNX replay loop |

## Host Requirements

Most generic shared cloud VMs virtualise or disable the PMU and reproduce the
same `<not supported>` LLC result observed under WSL2. GitHub-hosted runners also
expose no PMU.

You need **one of**:

1. A **physical Linux machine** (your own box, dual-boot, or a spare server) — the
   cleanest PMU and the cheapest.
2. A **bare-metal cloud instance** (full hardware PMU passthrough).

| Option | PMU | Notes |
| --- | --- | --- |
| Own physical Linux box / dual-boot | full | best value; you control BIOS turbo + governor |
| AWS EC2 `*.metal` (e.g. `c7i.metal-24xl`, `m7i.metal-24xl`, older `c6i.metal`) | full | on-demand, pricey/hr — **tear down when done** |
| Equinix Metal | full | hourly bare metal |
| Hetzner **dedicated** (Robot) / OVH / Latitude.sh / Vultr Bare Metal | full | dedicated, not the shared "Cloud" tiers |
| Hetzner **Cloud** / generic shared VM | usually virtualised/none | likely reproduces the WSL2 PMU limit — avoid for the PMU pass |
| GitHub Actions runner | none | perf reports `<not supported>`; not a path |

> **Cost & safety:** bare-metal cloud is billed by the hour and is outward-facing.
> Provisioning/teardown is your call and your credentials — I have no cloud CLI or
> SSH host configured here, so I cannot provision it. Remember to destroy the
> instance after copying results back.

Recommended distro for the commands below: **Ubuntu 24.04 LTS** (matches the WSL2
toolchain: GCC 13, CMake 3.28, perf 6.8). Other distros work; adjust package
names.

---

## Step 0 — Capture The Environment

Run and save the output; these become the report's "Environment" table.

```bash
uname -a
cat /etc/os-release | head -5
lscpu | grep -E 'Model name|Socket|Core|Thread|CPU\(s\)|MHz|cache'
grep -m1 'model name' /proc/cpuinfo
free -h
lsblk -d -o NAME,ROTA,SIZE,MODEL          # ROTA=0 => SSD/NVMe
gcc --version ; clang --version
cmake --version ; ninja --version
perf --version
# host class: bare metal vs VM vs cloud
systemd-detect-virt || true               # "none" => bare metal; else hypervisor name
# CPU control availability:
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo "no cpufreq governor"
cat /sys/devices/system/cpu/intel_pstate/no_turbo 2>/dev/null || echo "no intel_pstate no_turbo"
cat /proc/sys/kernel/perf_event_paranoid
```

**Confirm `systemd-detect-virt` reports `none` (bare metal) — if it names a
hypervisor, verify the PMU works in Step 2 before trusting counters, and label the
host class precisely in the report.** If it is a virtualised guest with a
virtualised PMU, stop and pick a bare-metal host instead; do not present a second
virtualised pass as native.

## Step 1 — Toolchain

```bash
sudo apt-get update
sudo apt-get install -y build-essential clang cmake ninja-build git python3 \
  linux-tools-common "linux-tools-$(uname -r)"
# optional, for flamegraphs:
git clone https://github.com/brendangregg/FlameGraph ~/FlameGraph
export PATH="$HOME/FlameGraph:$PATH"   # provides stackcollapse-perf.pl, flamegraph.pl
```

Clone Asterion into the **native filesystem** (not a network/9p mount) at HEAD
`216f473` or later:

```bash
git clone <asterion-remote> ~/asterion && cd ~/asterion
git log --oneline -1     # expect 216f473 "Add Linux performance evidence" or later
git diff --check         # expect clean
```

## Step 2 — Confirm Perf Counters

```bash
perf stat -d -- true
```

Record which of these are **counted** vs `<not supported>` / `<not counted>`:
`cycles, instructions, IPC, branches, branch-misses, cache-references,
cache-misses, L1-dcache-loads, L1-dcache-load-misses, LLC-loads, LLC-load-misses,
context-switches, cpu-migrations, page-faults`.

On native hardware the WSL2 `<not supported>` LLC rows should now populate. If
`perf` is blocked by `perf_event_paranoid`, either run perf as root or relax it
for the session (disclose it): `sudo sysctl kernel.perf_event_paranoid=1` (or
`-1` for full access). **Do not invent or substitute any counter that perf marks
unsupported — keep the verbatim line.**

## Step 3 — Record CPU Controls

Where available and safe (skip silently if unavailable; document either way):

```bash
# stable frequency:
sudo cpupower frequency-set -g performance        # governor=performance
# disable turbo (Intel intel_pstate):
echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo
# (AMD acpi-cpufreq alternative: echo 0 | sudo tee /sys/devices/system/cpu/cpufreq/boost)
# pick an isolated logical CPU for pinning, e.g. CPU 2:
PINCPU=2
```

Record governor, turbo state, and the pinned CPU. If governor/turbo cannot be
controlled, **state that clearly** — do not pretend they were pinned. Advanced
(optional): boot-time `isolcpus=`/`nohz_full=` for deeper isolation; document if
used.

## Step 4 — Build And Test With GCC And Clang

```bash
# GCC Release
cmake -S . -B build-gcc-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-gcc-release
ctest --test-dir build-gcc-release --output-on-failure        # expect all pass

# Clang Release
CC=clang CXX=clang++ cmake -S . -B build-clang-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-clang-release
ctest --test-dir build-clang-release --output-on-failure
```

Record each compiler version, the Release flags CMake shows (`-O3 -DNDEBUG`), and
the test results. If Clang is unavailable, record that status and continue with
GCC.

Optional profiling build for fuller flamegraphs (Step 8):

```bash
cmake -S . -B build-gcc-fp -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer"
cmake --build build-gcc-fp --target asterion_benchmarks
```

Optional ONNX-enabled build (only if you have ONNX Runtime; otherwise the ONNX row
stays `skipped`, not measured):

```bash
cmake -S . -B build-gcc-onnx -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_USE_ONNXRUNTIME=ON
cmake --build build-gcc-onnx --target asterion_benchmarks
```

## Step 5 — Generate Deterministic Corpora

The orchestrator generates all corpora into the git-ignored `build/perf_corpora/`
and records a manifest with seeds, event counts and SHA-256 checksums. The SHA-256
values should **match the WSL2 manifest** (deterministic cross-platform
generation), confirming the workload *shape* reproduces even though timings do
not.

```bash
python3 scripts/run_perf_evaluation.py --list      # show the corpus matrix + seeds
# generate corpora (and the standard/pooled hot-path matrix) into ignored dirs:
python3 scripts/run_perf_evaluation.py --skip-existing --build-dir build-gcc-release
```

Corpus matrix (mode / seed / events): `baseline_100k` balanced/2001/100k,
`baseline_1m` balanced/2002, `high_cancellation_1m` high-cancel/2003,
`replace_heavy_1m` replace-heavy/2004, `deep_book_1m` deep-book/2005, `bursty_1m`
bursty/2006, `wide_price_range_1m` wide-price-range/2007,
`multi_symbol_grouped_1m` multi-symbol/2008 (8 symbols, generated-only — the
single-symbol hot path is not a multi-symbol router).

The inference loop uses a small-book corpus **not** in that matrix — generate it
directly (balanced, seed 2101, 10k):

```bash
python3 scripts/generate_synthetic_events.py --mode balanced --events 10000 \
  --seed 2101 --symbol 1 --symbols 1 --output build/perf_corpora/balanced_10k.bin --format binary
```

> ⚠️ **Full-validation O(book) gotcha (already in memory `asterion-benchmark-fullvalidation-oN2`).**
> `--only-hot-path` bundles a per-run **Full**-validation replay that is
> O(resting-book size) per event, so it does **not** scale to 1M events on
> book-growing corpora (a balanced-100k `--only-hot-path` pass did not finish in
> 15 min on WSL2). Therefore:
> - 1M **throughput** comes from the steady-state **Light** path (Step 6b), not from `--only-hot-path`.
> - the 1M **standard-vs-pooled hot-path** point uses `high_cancellation_1m` (small book) with only 5 iterations (Step 6a).
> - `run_perf_evaluation.py` at its default 30 iters is safe for `baseline_100k`; cap larger corpora with `--events-cap` if you use it for them.
> `Light` is a **throughput-evaluation** mode only — correctness stays covered by
> `ctest` (Full validation) and the end-of-replay checksum/parity.

## Step 6 — Run Latency And Throughput Benchmarks

```bash
BENCH=build-gcc-release/asterion_benchmarks
CORP=build/perf_corpora
```

**6a. Standard vs pooled hot path, 1M (small-book corpus, 5 iters):**

```bash
$BENCH --dataset $CORP/high_cancellation_1m.bin --only-hot-path \
  --hot-path-warmup 1 --hot-path-iterations 5 --steady-state-validation-mode light \
  --json out_hotpath_highcancel_1m.json --no-text
```
Rows: `hot_path_binary_replay_l3_l2_strategy_risk` (standard) and
`hot_path_binary_replay_pooled_l3_l2_strategy_risk` (pooled). Expect identical
guard checksum (parity) and pooled = 0 steady-state allocations.

**6b. Single-thread vs SPSC steady-state replay, 1M (Light), one per corpus:**

```bash
for c in baseline_1m high_cancellation_1m replace_heavy_1m deep_book_1m bursty_1m wide_price_range_1m; do
  $BENCH --dataset $CORP/$c.bin --only-steady-state-replay \
    --steady-state-validation-mode light --spsc-queue-capacity 4096 \
    --json out_steady_$c.json --no-text
done
```
This emits the single-thread and SPSC steady rows (throughput is the meaningful
figure; `timing_mode=aggregate`). Check 0 dropped events and full checksum/guard
parity per corpus.

**6c. Inference replay loop (LinearModel) + micro-inference, small balanced corpus:**

```bash
$BENCH --dataset $CORP/balanced_10k.bin --hot-path-warmup 5 --hot-path-iterations 50 \
  --steady-state-validation-mode light --json out_inference_balanced_10k.json --no-text
```
Compare `hot_path_binary_replay_l3_l2_strategy_risk` (inference-free) vs
`hot_path_binary_replay_l3_l2_inference_strategy_risk` (inference loop) on the same
corpus/iteration count. The model is **plumbing only** — folded into the checksum
so it is not optimised away; it never alters matching/strategy/risk. No
decisioning/alpha/profitability claim.

**6d. Pooled stress datasets (optional):**

```bash
python3 scripts/run_pooled_stress_benchmarks.py --build-dir build-gcc-release
```

**6e. ONNX replay loop — only if you built `-DASTERION_USE_ONNXRUNTIME=ON`:**

```bash
build-gcc-onnx/asterion_benchmarks --dataset $CORP/balanced_10k.bin \
  --hot-path-warmup 5 --hot-path-iterations 50 --steady-state-validation-mode light \
  --json out_onnx_balanced_10k.json --no-text
```
If ONNX Runtime is not present, leave this **skipped** — do not report ONNX
numbers.

For each measured path record: command, compiler, dataset, validation mode, p50,
p95, p99, p99.9, max, throughput, allocation count + bytes, checksum/parity, and a
environment limit.

## Step 7 — Run Pinned `perf stat -d`

The helper wraps the hot path under `perf stat -d` and keeps `<not supported>`
lines verbatim:

```bash
scripts/run_linux_perf_profile.sh --build-dir build-gcc-release \
  --dataset $CORP/high_cancellation_1m.bin --iterations 5 --warmup 1
```

For the steady-state/SPSC and inference processes (the helper only covers the hot
path), run perf directly, pinned (`-d` adds the L1-dcache/LLC detailed events):

```bash
EV="task-clock,context-switches,cpu-migrations,page-faults,cycles,instructions,branches,branch-misses,cache-references,cache-misses"
# steady-state Light (representative 1M corpora):
for c in baseline_1m replace_heavy_1m deep_book_1m; do
  perf stat -d -e $EV -- taskset -c $PINCPU $BENCH --dataset $CORP/$c.bin \
    --only-steady-state-replay --steady-state-validation-mode light --spsc-queue-capacity 4096 --no-text
done
# inference loop:
perf stat -d -e $EV -- taskset -c $PINCPU $BENCH --dataset $CORP/balanced_10k.bin \
  --hot-path-warmup 5 --hot-path-iterations 50 --steady-state-validation-mode light --no-text
```

Collect per run: task-clock, elapsed, context-switches, cpu-migrations,
page-faults, cycles, GHz, instructions, IPC, branch-miss %, cache-miss % of refs,
**L1-dcache and LLC metrics (should now be available)**. Note multiplexing
fractions if any remain.

## Step 8 — Capture Hotspots And Flamegraphs

Hot path (standard + pooled in one process) with flamegraph, using the
frame-pointer build for fuller call graphs:

```bash
scripts/run_linux_perf_profile.sh --build-dir build-gcc-fp \
  --dataset $CORP/high_cancellation_1m.bin --iterations 5 --warmup 1 --record --flamegraph
```

SPSC steady-state hotspots (direct), pinned, DWARF call graph as a fallback to
frame pointers:

```bash
perf record -F 999 -g --call-graph dwarf -o perf_spsc.data -- \
  taskset -c $PINCPU build-gcc-fp/asterion_benchmarks --dataset $CORP/baseline_1m.bin \
  --only-steady-state-replay --steady-state-validation-mode light --spsc-queue-capacity 4096 --no-text
perf report --stdio -i perf_spsc.data | head -60
```

Note: pinning the SPSC producer **and** consumer to one CPU makes `__sched_yield`
dominate samples (a pinning artifact) — flat per-symbol hotspots are the reliable
signal. Prefer text `perf report --stdio` summaries; **do not commit raw
`perf.data`**.

## Step 9 — Assemble The Result Set

Everything above writes into **git-ignored** locations (`build/perf_corpora/`,
`build/perf_results/`, `build/perf_profile/`, and the `out_*.json` you created).
**None of it gets committed.** To let me (or you) write
`reports/native_linux_performance_evaluation_2026_06_04.md`, copy back as plain
text/markdown:

- the Step 0 environment capture and the Step 2 `perf stat -d -- true` counter list;
- the corpus manifest (`build/perf_results/corpus_manifest.json`) — checksums only;
- the benchmark JSONs / the `run_perf_evaluation.py` summary table;
- the `perf stat` text blocks and `perf report --stdio` hotspot tables;
- the flamegraph SVG(s) if generated (optional);
- the governor/turbo/pinning notes and compiler versions.

Paste those back and the report + doc updates (Tasks 11–15) can be finished and
committed as **`Add native Linux performance evidence`**, with native results kept
in their own tables (environment column explicit), separate from the WSL2 and
Windows/MSYS2 tables.

## Scope Boundaries

Representative local measurements only. **No** portable-latency, production-HFT,
production-model-serving, live-trading, authenticated exchange/broker
connectivity, order-placement, profitability/alpha or predictive-quality claim.
Binance data stays public crypto L2 only; the ChronosLOB toy model stays plumbing
evidence; ONNX Runtime stays optional; the correctness-first default path stays
available.

## Output Hygiene

Do **not** commit: generated corpora, benchmark JSON dumps, raw `perf.data`,
build directories, caches, downloaded deps, or secrets. Commit only the curated,
human-readable report and doc updates. Run `git diff --check` and re-run the GCC
(and Clang) Release build/test after doc edits before committing.
