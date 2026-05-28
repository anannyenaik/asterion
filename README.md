# Asterion

**Asterion: Deterministic Low-Latency Trading Systems Lab**

CV title: **C++20 Low-Latency Trading Engine with Market Replay, Risk Gateway and Real-Time ML Inference**

Asterion is a Linux-first C++20 trading systems lab focused on deterministic replay, L3 order book reconstruction, price-time-priority matching, pre-trade risk checks, execution reports, latency instrumentation and correctness testing. It is intentionally built as a serious foundation rather than a toy exchange simulator.

It does **not** claim to be a real exchange, a live trading system, or a true production HFT stack. The goal is to make the important engineering properties visible: deterministic behavior, testability, clean boundaries and benchmarkability.

## What Asterion Proves

- Integer tick prices in the hot path; no floating-point prices in matching or book state.
- L3 book reconstruction with order-ID lookup, FIFO queues per price level and deterministic checksums.
- Price-time-priority matching for limit, market, cancel and replace flows.
- Structured execution reports with deterministic report checksums.
- Pre-trade risk gateway with quantity, notional, position, exposure, price-band, stale-data, duplicate-ID and kill-switch checks.
- Golden trace tests and randomized invariant tests.
- A clean placeholder path for measured in-loop inference through `Model`, `LinearModel` and `FeatureExtractor`.

## Architecture

```text
CSV / synthetic events
        |
        v
 Market replay + sequence validation
        |
        v
   L3 order book  ---> L2 view ---> strategies / feature extraction / model score
        ^
        |
 Risk gateway ---> matching engine ---> execution reports ---> report checksum
        |
        v
  telemetry + benchmark runner
```

## Implemented Features

- C++20 CMake project with Ninja-compatible builds.
- Core types for timestamps, tick prices, quantities, symbols and order IDs.
- Fixed market-data event schema: Add, Cancel, Replace, Execute, Trade, Snapshot and Heartbeat.
- Correctness-first L3 book using `std::unordered_map`, `std::map` and FIFO lists.
- Book invariant checks and deterministic final checksums.
- Matching engine with partial fills, full fills and resting-price execution.
- Execution report schema with status, execution type, fill fields and reject reason.
- Risk gateway and kill switch.
- Deterministic CSV replay and sample replay data.
- Catch2 tests for unit, golden and randomized property-style coverage.
- Simple chrono benchmark executable.
- Strategy interface with market-maker and imbalance examples.
- Deterministic linear inference placeholder.
- GitHub Actions CI for Linux build and test.

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Debug build with sanitizers:

```bash
cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASTERION_ENABLE_SANITIZERS=ON
cmake --build build-debug
```

Catch2 v3 is used for tests. CMake will use a system package if available or fetch Catch2 during configure.

## Test

```bash
ctest --test-dir build --output-on-failure
```

Convenience script:

```bash
./scripts/run_tests.sh
```

## Benchmark

Benchmarks are generated locally. No benchmark numbers are checked into this repository.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target asterion_benchmarks
./build/asterion_benchmarks
```

The current runner measures add order, cancel order, simple match and sample replay paths using `std::chrono`. Google Benchmark can be added later without changing the public module boundaries.

## Honesty And Limitations

Asterion is a deterministic systems lab. It is not connected to any exchange, does not trade live, does not implement kernel bypass, does not claim true HFT production performance and does not include fabricated benchmark results. See [LIMITATIONS.md](LIMITATIONS.md) for the full scope statement.

## Example CV Bullet

Built **Asterion**, a C++20 deterministic trading systems lab implementing L3 order book reconstruction, price-time-priority matching, pre-trade risk checks, market replay, execution-report checksums, correctness tests and latency benchmark scaffolding.
