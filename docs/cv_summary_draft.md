# CV / Recruiter Summary Draft

Draft wording for describing Asterion on a CV, LinkedIn or in interviews. Every bullet is
backed by evidence in this repo (see [claim_audit.md](claim_audit.md) and
[evidence_index.md](evidence_index.md)). Pick the set that matches the role.

**Hard rules** — keep these honest:
- No benchmark numbers unless explicitly labelled "representative local measurement".
- Do not imply production HFT, live trading, real exchange connectivity or order placement.
- Do not imply profitability, alpha or predictive quality.
- Claim tests, replay, risk, inference plumbing, allocation behaviour and SPSC only where evidence exists.

## Project title

**Asterion — Deterministic C++20 Trading Systems Lab** (replay, matching, risk, inference
integration, benchmarking and correctness evaluation).

Alt: *C++20 Deterministic Trading Systems Lab with Market Replay, Risk Gateway and Measured Inference.*

## Conservative bullets (safe everywhere)

- Built Asterion, a deterministic C++20 trading systems lab: market replay, L3 order book,
  price-time-priority matching, pre-trade risk checks and execution reports, validated by a
  Catch2 + pytest suite covering golden traces and randomized invariants.
- Designed a frozen CSV/binary event-log schema (v1) with a JSON contract file and drift tests,
  so replay produces stable, reproducible book/execution/diagnostics checksums.
- Documented honest scope boundaries throughout (claim audit + evidence index + per-report
  limitation notes); no live trading, profitability or portable-latency claims.

## Stronger systems-focused bullets

- Engineered a deterministic tick-to-trade replay path (binary log → L3 update → reusable L2
  view → strategy callback → risk check) with allocation counters and scoped, warmed
  zero-allocation tests on the hot sub-paths.
- Added an opt-in bounded single-producer/single-consumer (SPSC) replay pipeline whose consumer
  reuses the single-thread engine, yielding bit-identical checksums regardless of thread timing,
  with lossless-blocking backpressure and an opt-in overload-shedding policy.
- Built a per-stage latency-budget instrument and a benchmark runner with stable JSON output and
  an opt-in pooled order book demonstrating allocation reduction after warm-up (representative
  local measurements, not portable claims).

## Quant-dev-focused bullets

- Implemented a pre-trade risk gateway (quantity, notional, position, exposure, price-band,
  stale-data, duplicate-ID, kill-switch) with opt-in working-exposure tracking, rate limiting,
  self-trade prevention and a deterministic, append-only risk audit trail.
- Added tamper-evident audit manifests with optional HMAC-SHA256 signing (verified against an RFC
  test vector and tamper/truncation/reorder detection tests).
- Built a recorded **public** Binance depth-stream case study: an honest L2→synthetic-L3
  normaliser feeding the deterministic replay/diagnostics pipeline, with CSV/binary equivalence
  tests and a fixture regeneration guard (recorded-data demo; not live trading).

## AI/ML-systems-focused bullets

- Built a measured inference-integration layer: deterministic `LinearModel` backend, versioned
  feature extraction, per-call latency percentiles and a timeout/late-signal policy gate —
  measuring plumbing cost only, with no predictive-quality or profitability claim.
- Added a caller-owned, zero-allocation feature-buffer path (allocation-free after warm-up in its
  scoped tests) alongside the convenience vector path.
- Integrated an optional ONNX Runtime backend behind a CMake flag with deterministic fallback to
  `LinearModel`, plus a tiny deterministic ChronosLOB-style ONNX fixture bridge (not a trained
  model) to demonstrate research-model → systems integration with honest allocation accounting.

## Wording to avoid

| Avoid | Use instead |
| --- | --- |
| "high-frequency trading system / HFT bot" | "deterministic trading systems lab" |
| "production-grade / production HFT" | "correctness-first, benchmarked locally" |
| "low-latency proven / sub-microsecond" | "latency-instrumented; representative local measurements" |
| "connects to / trades on exchanges" | "replays recorded and simulated market data" |
| "profitable strategy / generates alpha" | "deterministic strategy workloads; no profitability claim" |
| "real-time market data feed" | "recorded public depth case study (opt-in capture)" |
| "ML model serving in production" | "measured inference plumbing with optional ONNX backend" |
| "tamper-proof / compliant audit storage" | "tamper-evident manifests with opt-in local-key signing" |
