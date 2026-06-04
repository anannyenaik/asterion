# Profiling

Profiling results are hardware, kernel, compiler and power-policy dependent. Keep raw outputs local unless they are clearly labelled with environment metadata.

## Linux perf

Build Release first:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_BUILD_BENCHMARKS=ON
cmake --build build --target asterion_benchmarks
```

Generate a deterministic local corpus and run the standard-vs-pooled comparison:

```bash
python scripts/run_perf_evaluation.py --datasets baseline_1m --warmup 5 --measure 30
```

Then use the Linux-only helper. It writes plain-text output under the git-ignored
`build/perf_profile/` directory and never invents results if `perf` or hardware counters are
unavailable.

```bash
scripts/run_linux_perf_profile.sh --dataset build/perf_corpora/baseline_1m.bin \
    --path pooled --iterations 30 --output-dir build/perf_profile
```

Collect counters directly:

```bash
perf stat -d ./build/asterion_benchmarks --dataset build/perf_corpora/baseline_1m.bin \
    --only-hot-path --hot-path-iterations 30 --no-text
perf stat -e cycles,instructions,branches,branch-misses,cache-references,cache-misses,context-switches,page-faults \
    ./build/asterion_benchmarks --dataset build/perf_corpora/baseline_1m.bin \
    --only-hot-path --hot-path-iterations 30 --no-text
```

The benchmark JSON contains both L3 book variants:

- `hot_path_binary_replay_l3_l2_strategy_risk`
- `hot_path_binary_replay_pooled_l3_l2_strategy_risk`

Compare allocation counters only after the configured warm-up has completed. Latency counters are
local observations and should not be used as CI gates.

Record flamegraph-ready samples:

```bash
perf record -F 99 -g -- ./build/asterion_benchmarks \
    --dataset build/perf_corpora/baseline_1m.bin --only-hot-path --hot-path-iterations 30
perf report
perf script > build/perf_profile/perf.script
```

If Brendan Gregg's FlameGraph scripts are installed:

```bash
stackcollapse-perf.pl build/perf_profile/perf.script > build/perf_profile/out.folded
flamegraph.pl build/perf_profile/out.folded > build/perf_profile/asterion.svg
```

GitHub-hosted runners often do not expose hardware performance counters. The manual
`linux-performance` workflow records that blocker honestly and does not gate on benchmark numbers.

## WSL2 perf run (2026-06-01) — collected

The earlier firmware-virtualization blocker (below) is **resolved**. With BIOS/UEFI
virtualization enabled, WSL2 boots a Linux kernel, the project builds and `ctest`
passes, and the virtualized PMU exposes hardware counters, so a real `perf` pass was
captured:

- Environment: **WSL2** (Microsoft Hyper-V), Ubuntu 24.04.4, kernel
  `6.6.114.1-microsoft-standard-WSL2`, GCC 13.3.0 Release, `perf 6.8.12`
  (`linux-tools-6.8.0-124-generic`; the `/usr/bin/perf` wrapper warns because the
  WSL2 kernel ships no matching `linux-tools`, so the generic binary is run directly).
- `perf stat -d -- true` returns hardware counters; `LLC-loads`/`LLC-load-misses`
  are `<not supported>` on the virtualized PMU and counters are multiplexed.
- CPU governor/turbo are not controllable in WSL2; affinity pinning (`taskset -c 2`)
  works and all `perf` runs are pinned.
- Results: `reports/linux_performance_evaluation_2026_06_01.md` and
  `reports/perf_profile.md` (cycles/IPC/branch/cache counters, hotspots,
  standard-vs-pooled, SPSC and inference-loop interpretation). Representative WSL2
  measurements only — not native/cloud Linux, not portable.

Note: per-run `Full`-validation replay is O(book/event), so the 1M Linux rows use
`--only-steady-state-replay --steady-state-validation-mode light`; correctness stays
covered by `ctest` (Full validation) and end-of-replay checksum parity.

## WSL2 blocker record (historical)

On 2026-05-31, WSL2/Ubuntu was tested from Windows PowerShell on the Windows 10 host. The setup path could install/download components, but the current Windows session could not start WSL until the optional component takes effect after reboot/admin completion.

Commands run:

```powershell
wsl --status
wsl -l -v
wsl --list --online
Get-WindowsOptionalFeature -Online -FeatureName Microsoft-Windows-Subsystem-Linux
Get-WindowsOptionalFeature -Online -FeatureName VirtualMachinePlatform
wsl --install -d Ubuntu --no-launch
wsl --update
wsl --shutdown
wsl -d Ubuntu -- uname -a
```

Observed limitation:

- `Get-WindowsOptionalFeature ...` returned `The requested operation requires elevation.`
- `wsl --install -d Ubuntu --no-launch` reported WSL components and Ubuntu installed, then reported that changes will not be effective until the system is rebooted.
- `wsl --status`, `wsl -l -v`, `wsl --shutdown`, and `wsl -d Ubuntu -- uname -a` failed with `WSL_E_WSL_OPTIONAL_COMPONENT_REQUIRED`.
- Software counters were not probed because Ubuntu could not start.
- Hardware counters were not probed because Ubuntu could not start.
- Native Linux, or a working WSL2 Ubuntu session after the required Windows reboot/admin completion, remains required for final `perf stat -d` hardware-counter evidence.

### 2026-06-01 retry: firmware-virtualization blocker

After the host was rebooted, the WSL2 attempt was retried. The reboot **cleared** the optional-component blocker — `wsl --version` now reports `2.7.3.0` (kernel `6.6.114.1-1`) and `wsl --list --online` lists installable distributions. But a **new, lower-level blocker** now prevents any distribution from booting: hardware virtualization is disabled in the machine firmware.

```powershell
wsl --status            # Default Version: 2  (WSL launches)
wsl --version           # WSL version: 2.7.3.0, Kernel version: 6.6.114.1-1
wsl --list --online     # Ubuntu, Ubuntu-24.04, Debian, ... available
wsl --install -d Ubuntu --no-launch
systeminfo | Select-String -Pattern 'Hyper-V|Virtualization|Firmware|Second Level'
```

- `wsl --install -d Ubuntu --no-launch` downloaded/installed Ubuntu, then failed to create the VM: `HCS_E_HYPERV_NOT_INSTALLED` — "WSL2 is unable to start since virtualization is not enabled on this machine."
- `systeminfo` Hyper-V Requirements: `VM Monitor Mode Extensions: Yes`, `Virtualization Enabled In Firmware: No`, `Second Level Address Translation: Yes`. The CPU supports virtualization; firmware has it switched off.
- The failed VM creation rolled back; a follow-up `wsl -l -v` again showed no registered distribution.
- No Linux build, corpus generation, software-counter probe, hardware-counter probe or flamegraph capture was possible.
- To unblock on this machine: enable Intel VT-x / AMD-V (a.k.a. "Virtualization Technology" / "SVM Mode") in the BIOS/UEFI, reboot, then re-run WSL2 setup. Otherwise run the Linux commands above on a native Linux / cloud Linux host where the PMU is exposed.

## Google Benchmark JSON

```bash
cmake -S . -B build-gbench -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_USE_GOOGLE_BENCHMARK=ON
cmake --build build-gbench --target asterion_google_benchmarks
./build-gbench/asterion_google_benchmarks --benchmark_format=json > build-gbench/google_benchmark.json
```
