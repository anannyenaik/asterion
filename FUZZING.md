# Fuzzing

**Fuzz-driven robustness testing for deterministic replay, parser and matching
surfaces.**

Asterion provides opt-in libFuzzer targets for bounded mutation of event-log
parsers, replay inputs, matching requests and audit-manifest text. They are
robustness evidence: expected parse failures, replay diagnostics and matching
rejects are normal outcomes. Fuzzing complements deterministic unit, golden and
property tests plus the independent Python reference matcher; it does not replace
them.

The fuzzing scope covers bounded parser, replay, matching and manifest
robustness. It does not establish real-exchange correctness, security
certification or production safety.

## Targets

| Target | Covers | Deliberate boundary |
| --- | --- | --- |
| `fuzz_binary_event_log` | Arbitrary bounded bytes through the existing binary event-log reader; deterministic parse outcome and checksum consistency. | File-format robustness only; no exchange-specific feed decoder. |
| `fuzz_csv_event_log` | Bounded text/UTF-8-ish bytes through the CSV event-log reader; malformed headers/fields and deterministic parse outcome. | Caps input, line count and line size; it is not a general CSV implementation. |
| `fuzz_replay_engine` | Bounded parsed logs or byte-mapped event sequences through full-validation replay; deterministic results, diagnostics and book invariants. | Single-symbol `ReplayEngine`; expected diagnostics are accepted. |
| `fuzz_matching_requests` | Bounded limit/market/IOC/FOK/post-only/cancel/replace requests; report quantities, deterministic checksums, book invariants, uncrossed resting book and attributed STP trade pairs. | Does not embed Python or model venue-specific matching features outside Asterion's documented contract. |
| `fuzz_audit_manifest` | Bounded arbitrary text through `parse_audit_manifest`; deterministic parse outcome and stable serialize/parse round trips. | Parser robustness only; it does not fuzz filesystem custody, key management or compliance controls. |

All harnesses cap input or generated operation counts to avoid treating
pathological memory growth as useful fuzzing. Parser harnesses use bounded
temporary files because the current event-log parser API is file-based.

## Build

Fuzzers are off by default and have no effect on normal builds or default CI.
Use Clang with libFuzzer support:

```bash
CC=clang CXX=clang++ cmake -S . -B build-fuzz -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DASTERION_BUILD_TESTS=OFF \
  -DASTERION_BUILD_BENCHMARKS=OFF \
  -DASTERION_BUILD_FUZZERS=ON \
  -DASTERION_ENABLE_SANITIZERS=ON
cmake --build build-fuzz --target \
  fuzz_binary_event_log fuzz_csv_event_log fuzz_replay_engine \
  fuzz_matching_requests fuzz_audit_manifest
```

`ASTERION_BUILD_FUZZERS=ON` fails with a useful configure error when the compiler
is not Clang or does not expose libFuzzer instrumentation. No AFL++ installation
or external fuzzing dependency is required.

## Run

Run one target with its checked-in seed corpus:

```bash
mkdir -p build-fuzz/corpus/{binary_event_log,csv_event_log,replay_engine,matching_requests,audit_manifest}
./build-fuzz/fuzz_binary_event_log build-fuzz/corpus/binary_event_log cpp/fuzz/corpus/binary_event_log -runs=1000
./build-fuzz/fuzz_csv_event_log build-fuzz/corpus/csv_event_log cpp/fuzz/corpus/csv_event_log -max_total_time=10
./build-fuzz/fuzz_replay_engine build-fuzz/corpus/replay_engine cpp/fuzz/corpus/replay_engine -max_total_time=10
./build-fuzz/fuzz_matching_requests build-fuzz/corpus/matching_requests cpp/fuzz/corpus/matching_requests -max_total_time=10
./build-fuzz/fuzz_audit_manifest build-fuzz/corpus/audit_manifest cpp/fuzz/corpus/audit_manifest -max_total_time=10
```

The first corpus directory is writable and ignored; the second is the curated
checked-in seed corpus. Do not point libFuzzer at only the checked-in directory,
because it writes newly discovered inputs into its first corpus directory.

The manual [`fuzz-smoke`](.github/workflows/fuzz-smoke.yml) GitHub Actions
workflow builds all targets with Clang + ASan/UBSan and runs each seed corpus for
`1000` bounded runs. It is `workflow_dispatch` only and does not gate default CI.

For a longer local campaign, put generated corpus and artifacts under an ignored
build directory:

```bash
mkdir -p build-fuzz/corpus/matching build-fuzz/artifacts
./build-fuzz/fuzz_matching_requests \
  build-fuzz/corpus/matching cpp/fuzz/corpus/matching_requests \
  -max_total_time=600 \
  -artifact_prefix=build-fuzz/artifacts/matching-
```

## Reproduce And Minimize

Reproduce a libFuzzer artifact directly:

```bash
./build-fuzz/fuzz_matching_requests build-fuzz/artifacts/matching-crash-...
```

Minimize it to a reviewable reproducer:

```bash
./build-fuzz/fuzz_matching_requests \
  -minimize_crash=1 \
  -exact_artifact_path=build-fuzz/artifacts/matching-minimized \
  build-fuzz/artifacts/matching-crash-...
```

Add only small, intentional regression seeds under
`cpp/fuzz/corpus/<target>/`. Keep names descriptive and explain any non-obvious
encoding in the change that adds them.

Do not commit generated corpora, `crash-*`/`timeout-*`/`oom-*` artifacts, build
directories, profiler output, benchmark JSON, downloaded dependencies, secrets
or `.env` files.

## Seed Corpus

The checked-in corpus is intentionally tiny:

- binary: valid zero-event v1 header, truncated header and invalid header;
- CSV: valid minimal heartbeat and malformed schema/field input;
- replay: valid minimal log, sequence gap and timestamp reversal;
- matching: IOC, FOK, post-only, STP, cancel and replace byte patterns;
- audit manifest: valid minimal and malformed JSONL-like manifest text.

The corpus anchors meaningful parser and policy shapes while libFuzzer mutates
around them. It is not a generated coverage corpus.

The matching corpus uses the harness's compact eight-byte operation records:
command, side, price, quantity, client-order id, exchange-order id, owner and
policy. Command bytes `C`, `R`, `M`, `I`, `F` and `P` select cancel, replace,
market, IOC, FOK and post-only shapes; other command bytes map to ordinary new
orders or bounded variants. The named ASCII seeds make those policy patterns
reviewable without requiring a generated corpus.

## Relationship To Other Correctness Evidence

Deterministic tests pin named behavior and exact checksums. Property tests cover
fixed-seed randomized streams. The independent Python reference matcher provides
a second specification for documented matching semantics. Fuzzing adds mutation
breadth and sanitizer-backed robustness checks around those surfaces.

Future work may translate minimized `fuzz_matching_requests` inputs into the
existing Python cross-check operation format for differential fuzzing. Python is
not embedded in the libFuzzer target today, keeping the harness small and the
mutation loop native.
