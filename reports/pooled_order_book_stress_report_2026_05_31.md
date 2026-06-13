# Pooled Order Book Stress Report - 2026-05-31

This report evaluates the opt-in pooled path under deterministic stress corpora
on the disclosed local machine. The correctness-first `OrderBook` remains the
default; measurements are environment-specific.

## Evaluation Objective

The first pooled-book benchmark proved the allocation-reduction idea on the checked-in hot-path
sample. This pass extends validation to deterministic generated corpora with larger and more
adversarial lifecycle shapes so the allocation-free steady-state result is tested beyond one compact
fixture while keeping CI free of machine-specific latency gates.

## Deterministic Validation

The C++ test suite now generates small deterministic corpora in memory for:

- baseline balanced flow;
- high cancellation rate;
- replace-heavy flow;
- deep book;
- wide price range;
- bursty flow;
- long-running same-symbol replay;
- multi-symbol-style input, grouped per symbol for pooled validation;
- adversarial but valid lifecycle sequences.

For each CI-safe corpus, tests compare `OrderBook` and `PooledOrderBook` on final book checksum, L2
view, best bid/ask, total displayed depth, event count, execution/report checksum, diagnostics
checksum where applicable, guard checksum and deterministic repeated replay. The adversarial
lifecycle coverage includes repeated add/cancel reuse, replace increasing quantity, replace
decreasing quantity, replace changing price, cancel after replace, replace after partial fill, full
fill then ID reuse, unknown cancel/reduce rejection and zero/invalid quantity rejection paths.

## Local Stress Corpora

Generated under ignored paths:

- `data/generated/pooled_order_book_stress/`
- `benchmarks/results/pooled_order_book_stress/`

The generated `multi_symbol_style` corpus has 4,000 events and is retained as a compatibility
fixture. It is not run through the single-symbol hot-path benchmark because that benchmark is not a
multi-symbol router.

## Allocation And Correctness Results

Warm-up iterations: 5. Measured iterations: 25.

| dataset | events | standard allocs | standard bytes | pooled allocs | pooled bytes | guards match | pooled steady state |
|---|---:|---:|---:|---:|---:|---|---|
| baseline_balanced | 4,000 | 140,625 | 7,885,200 | 0 | 0 | yes | allocation-free |
| high_cancellation_rate | 4,000 | 146,950 | 9,242,000 | 0 | 0 | yes | allocation-free |
| replace_heavy | 4,000 | 171,300 | 9,606,000 | 0 | 0 | yes | allocation-free |
| deep_book | 5,000 | 231,025 | 13,082,000 | 0 | 0 | yes | allocation-free |
| wide_price_range | 5,000 | 215,625 | 13,036,800 | 0 | 0 | yes | allocation-free |
| bursty_flow | 4,000 | 141,375 | 7,927,200 | 0 | 0 | yes | allocation-free |
| long_running_same_symbol | 8,000 | 259,475 | 14,546,800 | 0 | 0 | yes | allocation-free |
| adversarial_lifecycle | 4,000 | 150,000 | 9,600,000 | 0 | 0 | yes | allocation-free |

No remaining pooled allocation source was observed in these warmed/reserved stress runs. The
standard path continued to allocate node-based book/index storage, as expected.

## Latency Summary

Latency was measured only to accompany the allocation experiment. It is not a CI gate and is not a
portable performance claim.

| dataset | standard avg ns/event | standard p95 | pooled avg ns/event | pooled p95 |
|---|---:|---:|---:|---:|
| baseline_balanced | 4,288 | 4,900 | 2,292 | 3,100 |
| high_cancellation_rate | 714 | 1,200 | 578 | 900 |
| replace_heavy | 1,126 | 1,500 | 516 | 800 |
| deep_book | 1,809 | 2,000 | 1,009 | 1,200 |
| wide_price_range | 1,521 | 2,000 | 1,336 | 1,900 |
| bursty_flow | 1,131 | 1,400 | 926 | 1,100 |
| long_running_same_symbol | 1,410 | 1,700 | 1,003 | 1,300 |
| adversarial_lifecycle | 739 | 1,100 | 501 | 700 |

## Reproduction Commands

```powershell
& .\scripts\configure_release.ps1 -BuildDir build-pool-stress
$env:PATH='C:\msys64\ucrt64\bin;' + $env:PATH
cmake --build build-pool-stress --target asterion_tests asterion_benchmarks
ctest --test-dir build-pool-stress --output-on-failure
python scripts\run_pooled_stress_benchmarks.py `
  --build-dir build-pool-stress `
  --hot-path-iterations 25 `
  --warmup-iterations 5
```

## Limitations

- The pooled path is opt-in and scoped to measured replay experiments.
- The default `OrderBook` is still the correctness-first implementation.
- The zero-allocation result depends on explicit warm-up and reservation.
- CI checks deterministic correctness and stable allocation behaviour on small fixtures; it does not
  assert latency numbers.
- The multi-symbol-style corpus is generated and validated by grouping per symbol; the pooled
  hot-path benchmark remains single-symbol.
