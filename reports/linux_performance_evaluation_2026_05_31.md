# Linux Performance Evaluation - 2026-05-31

> Representative measurements for the disclosed machine and environment.

This report adds a larger deterministic replay evaluation path: generated replay
corpora, standard-vs-pooled L3 hot-path comparisons, steady-state allocation
accounting and a Linux `perf` profiling helper.

**Follow-up status:** Linux `perf` evidence was later collected in WSL2
([2026-06-01 report](linux_performance_evaluation_2026_06_01.md)) and on Durham
Hamilton8 HPC ([2026-06-04 report](durham_hpc_performance_evaluation_2026_06_04.md)).
This 2026-05-31 report remains the Windows/MSYS2 larger-corpus evaluation and
perf-helper setup note, not the latest Linux counter evidence.

## Scope

- Asterion is not production-HFT infrastructure.
- These benchmarks do not show live trading capability, authenticated exchange connectivity, order placement, profitability or alpha.
- The pooled path is opt-in; the correctness-first `OrderBook` remains available.
- Large generated corpora are deterministic stress workloads, not market-alpha evidence.
- No portable latency guarantees or HFT claims are made from these numbers.

## Environment

| field | value |
| --- | --- |
| OS | Windows 10 10.0.19045, measured through MSYS2/MinGW-w64 UCRT64 |
| CPU | Intel(R) Core(TM) i7-7700HQ CPU @ 2.80GHz, 4 cores / 8 logical processors |
| Benchmark CPU string | Intel64 Family 6 Model 158 Stepping 9, GenuineIntel |
| Compiler | GCC 16.1.0 (`g++.exe (Rev5, Built by MSYS2 project) 16.1.0`) |
| Build type | Release |
| Compiler flags | `-O3 -DNDEBUG` |
| CMake / Ninja | CMake 4.3.3, Ninja 1.13.2 |
| Python | 3.14.5 (`C:/msys64/ucrt64/bin/python.exe`) |
| Host platform | Windows-10-10.0.19045-SP0 |
| Benchmark commit field | `e1cfeb9177a2` |

This is a Windows/MSYS2 run, not a native Linux run. Linux perf counters were not collected on this host; see [Linux perf status](#linux-perf-status).

## Commands Run

Health gate:

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_BUILD_PYTHON=ON -DPython3_EXECUTABLE=C:/msys64/ucrt64/bin/python.exe
cmake --build build
ctest --test-dir build --output-on-failure
$env:PYTHONPATH = "$PWD\build\python"
python -m pytest python/tests
cmake --build build --target asterion_benchmarks
.\scripts\run_demo.ps1 -SkipBuild
```

Benchmark and profiling checks:

```powershell
.\build\asterion_benchmarks.exe --dataset build\perf_corpora\baseline_100k.bin --only-hot-path --hot-path-warmup 5 --hot-path-iterations 30 --json build\perf_results\baseline_100k.benchmark.json --no-text
python scripts/run_perf_evaluation.py --warmup 5 --measure 30
$env:PATH = "C:\msys64\usr\bin;C:\msys64\ucrt64\bin;$env:PATH"
bash -n scripts/run_linux_perf_profile.sh
bash scripts/run_linux_perf_profile.sh --dataset build/perf_corpora/baseline_1m.bin --path pooled --iterations 30 --output-dir build/perf_profile
Get-Command perf -ErrorAction SilentlyContinue
wsl --list --quiet
```

Health-gate summaries:

- Configure/build: Release build with Python bindings enabled succeeded after pinning `Python3_EXECUTABLE` to the MSYS2 Python used for pytest. An initial pytest attempt with a mismatched stale `cp314` build artifact failed; after reconfigure/rebuild, the final pytest run passed.
- CTest: 1/1 test passed.
- pytest: 83 passed.
- Benchmark target: `asterion_benchmarks` built; after the CLI fix, the reproduced hot-path command exited 0.
- Demo: `scripts/run_demo.ps1 -SkipBuild` completed, including CSV/binary replay parity, shared-vs-grouped replay parity, risk/audit summaries, latency-budget JSON and benchmark JSON generation.

## Corpora

Corpora were generated deterministically with explicit seeds by `scripts/generate_synthetic_events.py` via `scripts/run_perf_evaluation.py`. They are local generated artifacts under `build/perf_corpora/`, which is git-ignored.

| corpus | mode | seed | events | symbols | format | size bytes | SHA-256 |
| --- | --- | ---: | ---: | ---: | --- | ---: | --- |
| `baseline_100k` | balanced | 2001 | 100,000 | 1 | binary | 5,800,016 | `ed3aaf6e917e8d39529d81249d2c0a93eb099189fd628943599c3f2e47494b8b` |
| `baseline_1m` | balanced | 2002 | 1,000,000 | 1 | binary | 58,000,016 | `373458a4b1157f6999c235c782d20b0480fd8501bdf13fc2d1c4addc9e65ed86` |
| `high_cancellation_1m` | high-cancel | 2003 | 1,000,000 | 1 | binary | 58,000,016 | `9f31433839a3708d5723a783228b6e109e81f0925bafaed487ba51adc6e8e325` |
| `replace_heavy_1m` | replace-heavy | 2004 | 1,000,000 | 1 | binary | 58,000,016 | `30fc80e0b7c5808b6a118c3bc05dc8719aa72d4f01ca19cbc75b24ffdfaf2bfe` |
| `deep_book_1m` | deep-book | 2005 | 1,000,000 | 1 | binary | 58,000,016 | `b4f167142d242f9e13291dbda0212e41f0f7e6ca6d7f0d61845d4172b38a277a` |
| `bursty_1m` | bursty | 2006 | 1,000,000 | 1 | binary | 58,000,016 | `8c5ceb658679a9ecdb1ec9eb58121e0a3c4fb5de3a80679344e15c5c5e6e151d` |
| `wide_price_range_1m` | wide-price-range | 2007 | 1,000,000 | 1 | binary | 58,000,016 | `0e657585c01683871aa6713a2366d4f74832071809d2164e25d92ff3f61cd818` |
| `multi_symbol_grouped_1m` | multi-symbol | 2008 | 1,000,000 | 8 | binary | 58,000,016 | `89bb120885f205bdc071781ad0f41bc54f0bd841b8255b3ed66f6634a2e58ca5` |

Example generation command recorded in the manifest:

```bash
C:/msys64/ucrt64/bin/python.exe scripts/generate_synthetic_events.py --mode balanced --events 1000000 --seed 2002 --symbol 1 --symbols 1 --output build/perf_corpora/baseline_1m.bin --format binary
```

The optional multi-symbol-style corpus was generated and checksummed, but not benchmarked:
- `multi_symbol_grouped_1m`: single-symbol hot-path benchmark is not a multi-symbol router.

## Benchmark Results

Each benchmarked corpus was replayed through `asterion_benchmarks --only-hot-path`, which emits the standard `hot_path_binary_replay_l3_l2_strategy_risk` row and the opt-in pooled `hot_path_binary_replay_pooled_l3_l2_strategy_risk` row. Warm-up iterations = 5; measured iterations = 30. Allocation counters are reset after warm-up, so counts below are steady-state measured allocations.

### Latency

| corpus | path | corpus events | measured events | p50 ns | p95 ns | p99 ns | p99.9 ns | max ns | throughput events/s | guard checksum |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `baseline_100k` | standard | 100,000 | 3,000,000 | 1,000 | 3,500 | 5,000 | 16,600 | 33,671,300 | 576,065 | 8145498768548620673 |
| `baseline_100k` | pooled | 100,000 | 3,000,000 | 800 | 2,700 | 3,600 | 10,400 | 16,562,400 | 726,809 | 8145498768548620673 |
| `baseline_1m` | standard | 1,000,000 | 30,000,000 | 1,200 | 4,200 | 6,200 | 20,200 | 125,458,100 | 456,722 | 8348964810417866845 |
| `baseline_1m` | pooled | 1,000,000 | 30,000,000 | 900 | 2,700 | 3,800 | 10,700 | 33,712,400 | 702,237 | 8348964810417866845 |
| `high_cancellation_1m` | standard | 1,000,000 | 30,000,000 | 800 | 2,900 | 4,100 | 10,300 | 33,632,500 | 869,084 | 18381399991751776699 |
| `high_cancellation_1m` | pooled | 1,000,000 | 30,000,000 | 600 | 2,000 | 2,800 | 5,600 | 40,111,100 | 1,163,782 | 18381399991751776699 |
| `replace_heavy_1m` | standard | 1,000,000 | 30,000,000 | 1,500 | 5,100 | 7,000 | 19,900 | 34,821,600 | 418,247 | 2904320911197226938 |
| `replace_heavy_1m` | pooled | 1,000,000 | 30,000,000 | 900 | 2,900 | 3,800 | 11,100 | 31,813,900 | 706,036 | 2904320911197226938 |
| `deep_book_1m` | standard | 1,000,000 | 30,000,000 | 1,200 | 4,300 | 7,400 | 65,000 | 39,476,500 | 353,187 | 562046499412879523 |
| `deep_book_1m` | pooled | 1,000,000 | 30,000,000 | 900 | 2,700 | 4,000 | 10,600 | 129,406,700 | 617,634 | 562046499412879523 |
| `bursty_1m` | standard | 1,000,000 | 30,000,000 | 1,200 | 4,200 | 6,300 | 19,400 | 38,667,600 | 466,373 | 11117827575143151108 |
| `bursty_1m` | pooled | 1,000,000 | 30,000,000 | 900 | 2,800 | 3,900 | 12,000 | 145,709,700 | 668,524 | 11117827575143151108 |
| `wide_price_range_1m` | standard | 1,000,000 | 30,000,000 | 1,400 | 4,900 | 7,500 | 23,200 | 31,751,300 | 411,604 | 18240489581069192929 |
| `wide_price_range_1m` | pooled | 1,000,000 | 30,000,000 | 1,200 | 3,900 | 5,400 | 14,500 | 31,864,600 | 536,826 | 18240489581069192929 |

### Allocations

| corpus | standard allocations | standard bytes | pooled allocations | pooled bytes | checksum parity | pooled steady-state allocation-free |
| --- | ---: | ---: | ---: | ---: | :---: | :---: |
| `baseline_100k` | 4,205,700 | 235,529,280 | 0 | 0 | yes | yes |
| `baseline_1m` | 42,040,560 | 2,354,284,320 | 0 | 0 | yes | yes |
| `high_cancellation_1m` | 44,043,480 | 2,771,575,200 | 0 | 0 | yes | yes |
| `replace_heavy_1m` | 50,975,790 | 2,854,668,000 | 0 | 0 | yes | yes |
| `deep_book_1m` | 54,008,880 | 3,024,670,080 | 0 | 0 | yes | yes |
| `bursty_1m` | 42,004,920 | 2,352,297,120 | 0 | 0 | yes | yes |
| `wide_price_range_1m` | 42,127,410 | 2,360,620,320 | 0 | 0 | yes | yes |

Checksum parity held on 7/7 benchmarked corpora. The pooled path reported 0 allocations and 0 allocated bytes after warm-up on 7/7 benchmarked corpora. The standard path allocations are reported rather than hidden; they come from the correctness-first node/container path during Add/Replace/order lookup activity.

## Linux Perf Status

Status: not collected on this machine. This blocker is documented; no cycles, instructions, branch, cache, context-switch, page-fault or flamegraph data is fabricated.

- `scripts/run_linux_perf_profile.sh` exits on this host because `uname -s` reports `MSYS_NT-10.0-19045`, not Linux.
- `Get-Command perf -ErrorAction SilentlyContinue` found no `perf` executable on PATH.
- `wsl --list --quiet` did not return a usable installed Linux distribution list on this host.
- Therefore hardware counter and flamegraph collection must be run on a Linux host or through the manual workflow where counters are exposed.

### WSL2 Attempt - 2026-05-31

WSL2/Ubuntu setup was attempted from Windows PowerShell on this machine. The install path downloaded/enabled WSL components and Ubuntu, but Windows reported that the changes require a reboot before WSL can start, so no Linux build, corpus generation, `perf stat -d`, software-counter probe, hardware-counter probe or flamegraph capture was run under WSL2.

Commands run:

```powershell
wsl --status
wsl -l -v
wsl --list --online
Get-WindowsOptionalFeature -Online -FeatureName Microsoft-Windows-Subsystem-Linux
Get-WindowsOptionalFeature -Online -FeatureName VirtualMachinePlatform
wsl --install -d Ubuntu --no-launch
wsl --status
wsl -l -v
wsl --update
wsl --shutdown
wsl -d Ubuntu -- uname -a
```

Observed blocker:

- `Get-WindowsOptionalFeature ...` could not be queried from this shell: `The requested operation requires elevation.`
- `wsl --install -d Ubuntu --no-launch` reported `Virtual Machine Platform has been installed`, `Windows Subsystem for Linux has been installed`, `Ubuntu has been installed`, and then `The requested operation is successful. Changes will not be effective until the system is rebooted.`
- After the install attempt, `wsl --status`, `wsl -l -v`, `wsl --shutdown`, and `wsl -d Ubuntu -- uname -a` all failed with `WSL_E_WSL_OPTIONAL_COMPONENT_REQUIRED` and the message that the Windows Subsystem for Linux Optional Component is required and the system may need to be restarted.
- `wsl --update` succeeded with `The most recent version of Windows Subsystem for Linux is already installed.`
- Software counters were not probed because Ubuntu could not start.
- Hardware counters were not probed because Ubuntu could not start.
- Native Linux, or a working WSL2 Ubuntu session after the required Windows reboot/admin completion, remains required for final `perf stat -d` evidence.

### WSL2 Retry - 2026-06-01 (firmware-virtualization blocker)

The WSL2/Ubuntu attempt was retried from Windows PowerShell after the system had been rebooted. The earlier `WSL_E_WSL_OPTIONAL_COMPONENT_REQUIRED` blocker is **resolved**: WSL itself now launches and reports a working install. However, a **new, lower-level blocker** prevents any Linux distribution from starting on this host: hardware virtualization is **disabled in the machine firmware (BIOS/UEFI)**. This is a hardware/firmware setting that cannot be changed from software; no Linux build, corpus generation, software-counter probe, hardware-counter probe, `perf stat -d` or flamegraph capture was run under WSL2.

Commands run:

```powershell
wsl --status
wsl -l -v
wsl --version
wsl --list --online
wsl --install -d Ubuntu --no-launch
systeminfo | Select-String -Pattern 'Hyper-V|Virtualization|Firmware|Second Level'
wsl -l -v
```

Observed blocker:

- `wsl --status` reported `Default Version: 2` (WSL launches; the prior optional-component blocker is gone).
- `wsl --version` reported `WSL version: 2.7.3.0`, `Kernel version: 6.6.114.1-1`, on `Windows version: 10.0.19045.6466`.
- `wsl -l -v` reported `Windows Subsystem for Linux has no installed distributions` (no distro present yet).
- `wsl --list --online` listed installable distributions (Ubuntu, Ubuntu-24.04, Debian, etc.), confirming the WSL service is reachable.
- `wsl --install -d Ubuntu --no-launch` downloaded and installed Ubuntu, then failed when creating the VM: `WSL2 is unable to start since virtualization is not enabled on this machine. Please ensure the "Virtual Machine Platform" optional component is enabled and virtualization is turned on in your computer's firmware settings.` Error code: `Wsl/InstallDistro/Service/RegisterDistro/CreateVm/HCS/HCS_E_HYPERV_NOT_INSTALLED`.
- `systeminfo` Hyper-V Requirements reported: `VM Monitor Mode Extensions: Yes`, `Virtualization Enabled In Firmware: No`, `Second Level Address Translation: Yes`. The CPU supports virtualization, but it is **switched off in firmware**.
- The failed VM creation rolled back: a follow-up `wsl -l -v` again reported no installed distributions, so no partial distro was left registered.
- Software counters were not probed because no distribution could start.
- Hardware counters were not probed because no distribution could start.

Required to unblock on this machine: enable Intel VT-x / AMD-V (often labelled "Virtualization Technology", "Intel VMX", "SVM Mode", or "VT-d") in the BIOS/UEFI firmware, reboot, then re-run the WSL2/Ubuntu setup. Alternatively, run the ready-to-run Linux commands below on a native Linux host or a cloud Linux instance where the PMU is exposed. Until firmware virtualization is enabled, WSL2 cannot boot a Linux kernel on this host and `perf` hardware-counter evidence cannot be collected here.

Ready-to-run Linux commands:

```bash
python scripts/run_perf_evaluation.py --datasets baseline_1m --warmup 5 --measure 30
scripts/run_linux_perf_profile.sh --dataset build/perf_corpora/baseline_1m.bin --path pooled --iterations 30 --output-dir build/perf_profile
perf stat -d ./build/asterion_benchmarks --dataset build/perf_corpora/baseline_1m.bin --only-hot-path --hot-path-iterations 30 --no-text
perf record -F 99 -g -- ./build/asterion_benchmarks --dataset build/perf_corpora/baseline_1m.bin --only-hot-path --hot-path-iterations 30
perf report
```

The manual `.github/workflows/linux-performance.yml` workflow is dispatch-only, non-blocking, has no benchmark-number gates, and uploads only small text artifacts. GitHub-hosted runners may not expose PMU hardware counters reliably; that condition is recorded rather than treated as a performance result.

## Allocator Profiling

The primary allocator signal here is the benchmark target built-in allocation tracker. The warm-up phase is excluded from the reported counts by resetting the counters immediately before measured iterations. This distinguishes warm-up allocations from steady-state measured allocations and separates standard-path allocation behavior from the opt-in pooled path.

Optional deeper Linux allocator tools are intentionally not part of default CI:

```bash
heaptrack ./build/asterion_benchmarks --dataset build/perf_corpora/baseline_1m.bin --only-hot-path --hot-path-iterations 30
heaptrack_print heaptrack.asterion_benchmarks.*.zst | less
valgrind --tool=massif ./build/asterion_benchmarks --dataset build/perf_corpora/baseline_1m.bin --only-hot-path --hot-path-iterations 5
ms_print massif.out.* | less
```

## Limitations

- This run is Windows/MSYS2, not native Linux.
- Linux perf counters and flamegraphs were not collected here.
- The hot-path benchmark is single-symbol; the optional multi-symbol corpus is generated only in this evaluation path.
- The pooled book is an opt-in allocation experiment, not the default replay book.
- Synthetic corpora are deterministic stress workloads, not market data and not market-alpha evidence.
- Latencies depend on this laptop, OS scheduling, compiler, power state and timer behavior.

> These are representative measurements on this machine/environment, not portable performance claims.
