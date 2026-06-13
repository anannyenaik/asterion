# Scope And Limitations

Asterion is a deterministic trading-systems and ML-infrastructure lab for
recorded and simulated workloads. Its evidence covers implemented behavior,
correctness tests and measurements under disclosed environments. Live trading,
authenticated exchange or broker connectivity, order placement and production
HFT infrastructure are outside the project scope.

## Market And Matching Scope

- The matching engine implements a deterministic local contract for limit and
  market orders, IOC, FOK, post-only, cancel, replace and one reject-incoming
  self-trade-prevention policy. It does not model auctions, hidden or iceberg
  orders, pegging, stop orders, venue-specific expiry, trade busts or regulatory
  workflows. See `docs/matching_semantics.md`.
- The project does not implement kernel bypass, FPGA paths, colocated networking,
  a production network stack or a market-impact model.
- Strategy examples are deterministic systems workloads. They carry no
  profitability, alpha or signal-value claim.

## Replay And Concurrency

- Deterministic single-thread replay is the default.
- `run_spsc_replay` is an opt-in bounded single-producer/single-consumer
  concurrency experiment. Its consumer reuses the single-thread `ReplayEngine`
  path, and lossless runs produce bit-identical book, execution-report and
  diagnostics checksums. Backpressure count and maximum queue depth are
  timing-dependent diagnostics and are excluded from checksums.
- The default SPSC backpressure policy blocks the producer when the queue is
  full. The opt-in `DropNewestOnFull` policy is intentionally lossy and is not
  correctness-preserving for order-book streams; dropped events create sequence
  gaps and unknown-order references that halt replay deterministically.
- A pooled-book SPSC variant is not implemented because `ReplayEngine` is not
  templated on the book type. `PooledOrderBook` is evaluated through the
  single-thread hot-path benchmark rows.
- `run_spsc_replay_steady_state` and `ReplayValidationMode::Light` are opt-in
  throughput-evaluation tools. `Light` retains inexpensive per-event top-of-book
  checks and performs full validation at end of replay. Default `Full`
  validation remains the correctness path.
- `MultiSymbolBookSet` provides an opt-in shared replay path. It is not a
  cross-symbol matching engine and does not replace the default grouped
  single-symbol replay path.

## Performance Evidence

- Benchmark and latency observations are specific to the disclosed hardware,
  toolchain, build and workload. Generic benchmark dumps are not committed, and
  cross-machine comparisons are not meaningful.
- Regression comparisons are meaningful only when both JSON files were produced
  on the same controlled hardware. Checked-in `sample_benchmark_*.json` files are
  synthetic tooling fixtures, not measurements.
- Latency-budget observations are machine-dependent. Configuration checksums and
  accounting logic are deterministic; Asterion defines no built-in latency
  targets.
- Allocation claims apply only to documented warmed paths: reusable L2 views,
  fixed strategy callbacks, reserved risk paths, caller-owned inference feature
  buffers and the opt-in pooled L3 benchmark path. ONNX inference and the
  vector-returning feature-extraction convenience path are not allocation-free.
- `PooledOrderBook` is an opt-in allocation experiment, not a general allocator
  framework or replacement for the correctness-first `OrderBook`. Its measured
  zero-allocation result requires explicit warm-up and reservation in the
  disclosed tests and benchmarks. Current pooled validation is single-symbol at
  the book and benchmark layer; multi-symbol-style streams are grouped per
  symbol for parity checks.
- Performance workflows are manual and non-blocking. They record unavailable
  `perf` counters rather than substituting values.

The primary performance context is the Durham Hamilton8 pass collected on
2026-06-04
(`reports/durham_hpc_performance_evaluation_2026_06_04.md`): one shared Slurm
compute-node allocation using Rocky Linux and GCC, with no LLC events, no root
control over governor or turbo, GCC evidence only, and one inference hotspot
report terminated by OOM. The 2026-06-01 WSL2 pass
(`reports/linux_performance_evaluation_2026_06_01.md`) and the original
Windows/MSYS2 laptop run remain local development baselines. The WSL2 run used
one laptop with a virtualized PMU, no LLC cache events, time-multiplexed counters
and uncontrolled CPU turbo.

Durham supersedes the laptop and WSL2 context only for paths measured on
Hamilton8. Not every earlier result was rerun there. The 1M Linux rows use
`--steady-state-validation-mode light` because per-event `Full` validation is
O(resting-book size); default `Full` validation and end-of-replay checksum parity
retain the correctness coverage. The optional ONNX Runtime backend was not built
in those Linux runs, so the ONNX replay-loop row is recorded as skipped.

## Inference And Model Scope

- The default inference backend is deterministic `LinearModel`. ONNX Runtime is
  optional behind `ASTERION_USE_ONNXRUNTIME`, exercised by a manual CI path, and
  falls back to `LinearModel` when unavailable.
- Checked-in ONNX artifacts cover a deterministic hand-written fixture, a tiny
  4-feature single-timestep ChronosLOB `DeepLOBModel` trained on synthetic toy
  data, and a windowed `[1,16,40]` ChronosLOB model trained on recorded public
  Binance crypto L2 depth. They evaluate model contracts, feature integration,
  fallback behavior, allocation accounting and systems cost.
- Inference benchmarks measure feature extraction, model scoring and policy
  accounting. They do not establish predictive quality, profitability, alpha or
  production model-serving capability. Sub-microsecond percentiles are affected
  by timer resolution and remain local observations.
- The timeout and late-signal policy disables a model after repeated late
  signals only when explicitly configured. By default, the model remains enabled
  and the gate abstains on individual late or timed-out signals.

## Risk, Session And Audit Scope

- Open-order exposure, message-rate limiting, self-trade prevention and
  cancel-on-disconnect cancellation are opt-in and disabled by default.
  Sliding-window rate limiting stores per-client timestamps while enabled.
- Cancel-on-kill and cancel-on-disconnect release tracked simulated working
  exposure inside the risk gateway; they do not send exchange or broker cancels.
- `SimulatedBrokerSession` is an in-process deterministic lifecycle model. It
  maintains no real session and sends no network messages.
- Replace-order risk checks apply to tracked resting simulated orders after an
  execution report binds the exchange order ID. They do not form a complete
  broker order-management system.
- `PortfolioRiskMonitor` is a simulated accounting gate over caller-supplied
  marks and positions. It does not provide live portfolio management, market-risk
  data or cross-symbol matching.
- Persistent risk audit logs, rotation, verification and manifests are opt-in
  and append-only. Optional HMAC-SHA256 manifest signing uses caller-managed
  local keys; managed retention, custody, compliance controls and tamper-proof
  storage remain outside scope.

## Data And Schema Scope

- Snapshot loading reconstructs the L3 book from framed single-order `Snapshot`
  records. It cannot represent an aggregated L2-only image without per-order
  detail.
- Event-log schema v1 is stable for checked-in fixtures and guarded by manifests
  and tests. Asterion does not provide a general multi-version migration
  framework. Breaking changes must bump the version, update
  `docs/event_log_schema.md`, regenerate affected fixtures and document
  conversion from older logs.
- The Binance case study uses recorded public REST `/api/v3/depth` data without
  API keys. Capture is manual and never runs in CI. Binance depth is L2
  price-level data, so the normaliser represents each level with deterministic
  synthetic order IDs and level-replacement semantics. It does not recover real
  L3 order identity, per-level FIFO depth, individual order sizes or true order
  lifetimes. The checked-in fixture is compact for deterministic CI; larger
  local captures are git-ignored.
