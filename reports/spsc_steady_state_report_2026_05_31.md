# Steady-State SPSC Replay Evaluation (2026-05-31)

Measurements are representative of the disclosed local environment.

This report covers the opt-in steady-state SPSC replay evaluation added after the first SPSC replay
pipeline. Deterministic single-thread replay remains the default. Full per-event validation remains
available and remains the default. The lighter validation mode described here is opt-in and intended
for throughput evaluation on large replay corpora, not as a replacement for correctness testing.

The evaluation covers a bounded replay concurrency path. Production networking,
live exchange connectivity, portable latency, profitability and alpha are
outside scope.

## Why The Previous Benchmark Was Limited

The previous SPSC benchmark row, `spsc_replay_l3_diagnostics`, calls `run_spsc_replay` once per
measured replay iteration. `run_spsc_replay` creates and joins a producer `std::thread` on each call,
so tiny repeated runs are dominated by thread lifecycle rather than queue transfer.

The previous larger-corpus runs were also dominated by validation. In the default correctness-first
mode, `ReplayEngine::validate_book_after_apply` calls `OrderBook::check_invariants()` after every
applied event and then checks the best bid/ask. `check_invariants()` walks the book, so replaying a
growing book pays an O(book size) validation cost per event and can become roughly O(n^2) over the
whole corpus. That is the right default for correctness tests, but it hides true per-event SPSC
overhead in larger throughput measurements.

Correctness-critical checks kept in the replay path include schema/value validation, symbol match,
sequence/timestamp validation, duplicate/unknown order detection, quantity checks, apply failure
diagnostics, deterministic checksums, and crossed-book detection. Benchmark-only instrumentation
includes p50/p95/p99 samples, queue depth, backpressure counts and allocation counters; those values
are never part of replay checksums.

## Design Summary

Two additions were made:

- `ReplayValidationMode::Light`: keeps cheap per-event top-of-book crossed-book validation and
  defers the full invariant walk to `finalize_stream`. `ReplayValidationMode::Full` remains the
  default and preserves the existing per-event invariant behavior.
- `run_spsc_replay_steady_state`: creates producer and consumer threads once, waits until both are
  ready, starts the measurement gate, streams the preloaded corpus through the SPSC queue, emits one
  end-of-stream marker on clean streams, joins both threads, then finalizes replay.

The benchmark runner now emits distinct lifecycle and validation fields:

- old single-thread replay row: `replay_l3_diagnostics_single_thread`,
  `thread_lifecycle_mode=single_thread`, `validation_mode=full`;
- old SPSC row: `spsc_replay_l3_diagnostics`, `thread_lifecycle_mode=per_replay`,
  `validation_mode=full`;
- new single-thread steady row: `single_thread_replay_steady_state_l3_diagnostics`,
  `thread_lifecycle_mode=single_thread`, `validation_mode=light` by default;
- new SPSC steady row: `spsc_replay_steady_state_l3_diagnostics`,
  `thread_lifecycle_mode=steady_state`, `validation_mode=light` by default.

For large steady-state runs, aggregate elapsed time and throughput are the meaningful timing fields.
Per-run p50/p95/p99/p99.9 are reported as `n/a` for these rows because each row is one whole-corpus
steady-state run.

## Local Environment

- OS: Windows
- Compiler: GCC 16.1.0
- Build type: Release
- Compiler flags reported by benchmark JSON: `-O3 -DNDEBUG`
- CPU string reported by benchmark JSON: `Intel64 Family 6 Model 158 Stepping 9, GenuineIntel`
- Validation mode for local throughput runs: `light`

## Corpus Generation

Generated corpora were kept under ignored `build/spsc_steady_state_eval/`.

| corpus | command | events | seed | sha256 |
|---|---|---:|---:|---|
| balanced 10k | `python scripts/generate_synthetic_events.py --mode balanced --events 10000 --seed 20260531 --output build/spsc_steady_state_eval/balanced_10k_seed_20260531.bin --format binary` | 10000 | 20260531 | `58D240D0251BF1423379F49FB773816D4F2C5CBA5FBD638F14BEDFC4CAE8CA4C` |
| balanced 100k | `python scripts/generate_synthetic_events.py --mode balanced --events 100000 --seed 20260531 --output build/spsc_steady_state_eval/balanced_100k_seed_20260531.bin --format binary` | 100000 | 20260531 | `DED2047CE6C446E892566E8C98B8866B032015474A927BE0A60AF482514B32A9` |
| replace-heavy 100k | `python scripts/generate_synthetic_events.py --mode replace-heavy --events 100000 --seed 20260531 --output build/spsc_steady_state_eval/replace_heavy_100k_seed_20260531.bin --format binary` | 100000 | 20260531 | `C0C1F41FE1367AC958796F9E752711C25C3B0729D85EB53ADEAB85BF75B7E5EA` |
| balanced 1M | `python scripts/generate_synthetic_events.py --mode balanced --events 1000000 --seed 20260531 --output build/spsc_steady_state_eval/balanced_1m_seed_20260531.bin --format binary` | 1000000 | 20260531 | `D4A26361816F9939C7D3A0B7B8DE749E304213AA0533EDEEAF18E71B79B18D61` |
| replace-heavy 1M | `python scripts/generate_synthetic_events.py --mode replace-heavy --events 1000000 --seed 20260531 --output build/spsc_steady_state_eval/replace_heavy_1m_seed_20260531.bin --format binary` | 1000000 | 20260531 | `8E447FEEC60F7892EF1DA5161403922D39A416D48A4E0B1AEE1DECBF03508F20` |
| deep-book 1M | `python scripts/generate_synthetic_events.py --mode deep-book --events 1000000 --seed 20260531 --output build/spsc_steady_state_eval/deep_book_1m_seed_20260531.bin --format binary` | 1000000 | 20260531 | `AADD353A8D2ACCF42E83D686D4B8AA7F5E332C8F4ECB8CD322AD0D0133694C98` |

## Reproduction Commands

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
.\scripts\configure_release.ps1 -BuildDir build
cmake --build build --target asterion_benchmarks

.\build\asterion_benchmarks.exe --only-steady-state-replay `
  --dataset build\spsc_steady_state_eval\balanced_10k_seed_20260531.bin `
  --warmup-iterations 0 --spsc-queue-capacity 4096 `
  --steady-state-validation-mode light `
  --json build\spsc_steady_state_eval\balanced_10k.json --no-text

.\build\asterion_benchmarks.exe --only-steady-state-replay `
  --dataset build\spsc_steady_state_eval\balanced_100k_seed_20260531.bin `
  --warmup-iterations 0 --spsc-queue-capacity 4096 `
  --steady-state-validation-mode light `
  --json build\spsc_steady_state_eval\balanced_100k.json --no-text

.\build\asterion_benchmarks.exe --only-steady-state-replay `
  --dataset build\spsc_steady_state_eval\replace_heavy_100k_seed_20260531.bin `
  --warmup-iterations 0 --spsc-queue-capacity 4096 `
  --steady-state-validation-mode light `
  --json build\spsc_steady_state_eval\replace_heavy_100k.json --no-text
```

The optional 1M runs used the same command shape with `--spsc-queue-capacity 8192`.

## Local Results

Throughput ratio is `SPSC throughput / single-thread throughput`. Values above or below 1.0 are local
observations only; they are not portable speedup or latency claims.

| corpus | queue | single ev/s | steady SPSC ev/s | ratio | backpressure | max depth | dropped | parity |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| balanced 10k | 4096 | 1,600,614.64 | 1,540,167.57 | 0.962 | 4,184 | 4,096 | 0 | true |
| balanced 100k | 4096 | 1,343,289.80 | 1,442,605.92 | 1.074 | 42,331 | 4,096 | 0 | true |
| replace-heavy 100k | 4096 | 968,086.07 | 1,460,668.58 | 1.509 | 48,482 | 4,096 | 0 | true |
| balanced 1M | 8192 | 1,063,306.17 | 1,058,126.15 | 0.995 | 685,331 | 8,192 | 0 | true |
| replace-heavy 1M | 8192 | 494,143.00 | 373,149.85 | 0.755 | 620,989 | 8,192 | 0 | true |
| deep-book 1M | 8192 | 820,319.44 | 824,452.33 | 1.005 | 497,176 | 8,192 | 0 | true |

All rows had deterministic checksum parity with the matching single-thread steady-state row.

| corpus | final book checksum | diagnostics checksum |
|---|---:|---:|
| balanced 10k | 18317673466403586335 | 14695981039346656037 |
| balanced 100k | 15409655339973220739 | 14695981039346656037 |
| replace-heavy 100k | 8105113682183830357 | 14695981039346656037 |
| balanced 1M | 1141364419495602438 | 14695981039346656037 |
| replace-heavy 1M | 6219765928563188675 | 14695981039346656037 |
| deep-book 1M | 229479708199243571 | 14695981039346656037 |

## Allocation Behavior

The benchmark allocation counters cover the whole benchmark call. The steady-state elapsed timer
starts after producer and consumer threads are ready, but allocation counters still include harness
setup such as the bounded queue storage and thread objects. That is why the SPSC row has three more
allocations than the single-thread row in these runs. The dominant allocation volume in both rows is
still the correctness-first `OrderBook` storage for resting orders and levels.

| corpus | single allocations / bytes | SPSC allocations / bytes |
|---|---:|---:|
| balanced 10k | 17,588 / 997,152 | 17,591 / 1,292,288 |
| balanced 100k | 174,793 / 9,725,520 | 174,796 / 10,020,656 |
| replace-heavy 100k | 194,596 / 11,237,408 | 194,599 / 11,532,544 |
| balanced 1M | 1,747,791 / 95,070,000 | 1,747,794 / 95,660,048 |
| replace-heavy 1M | 1,949,101 / 110,261,520 | 1,949,104 / 110,851,568 |
| deep-book 1M | 2,548,592 / 158,463,984 | 2,548,595 / 159,054,032 |

## Backpressure And Shutdown

The default policy is lossless blocking. Every measured steady-state run reported
`dropped_events=0` and `checksum_parity=true`. The queue reached its configured capacity in every
large run, so the backpressure counter is a real saturation signal rather than a synthetic stat.
Clean streams produced and consumed exactly one end-of-stream marker. Malformed-stream tests cover
consumer halt, producer wind-down and deterministic diagnostic propagation.

## Test Coverage

The deterministic tests cover:

- checked-in sample replay and Binance-normalised fixture;
- generated balanced, cancel-heavy, replace-heavy and deep-book fixtures;
- repeated-run determinism;
- `Full` versus `Light` validation checksum parity on small fixtures;
- deterministic malformed-input diagnostics;
- unsupported validation mode diagnostics;
- zero queue capacity rejection without deadlock;
- tiny queues, forced backpressure, max-depth bounds, clean shutdown, exactly-once EOS on clean
  streams, and consumer halt propagation.

## Known Limitations

- `ReplayValidationMode::Light` is for throughput evaluation. It is not a replacement for default
  full validation in correctness tests.
- The steady-state timer excludes thread creation by design, but allocation counters still include
  harness setup.
- Backpressure counts and max queue depth are timing-dependent diagnostics and are not checksummed.
- The benchmark still uses the correctness-first `OrderBook`; pooled-book SPSC is not implemented.
- These measurements were run on one local Windows/MSYS2 environment. They must not be presented as
  portable latency, throughput or production-HFT evidence.
