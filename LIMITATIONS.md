# Limitations

Asterion is a deterministic trading systems lab, not a live trading product.

Explicit limitations:

- not a real exchange;
- no real exchange connectivity;
- no live trading;
- simplified network path;
- simplified market impact model;
- no kernel bypass;
- no FPGA path;
- no broad allocation-avoidance claim; only the documented reusable L2, fixed strategy callback,
  reserved risk sub-paths and opt-in pooled L3 benchmark path are shown allocation-free after
  warm-up under their scoped tests;
- deterministic single-thread replay is the default. There is exactly one opt-in concurrency
  boundary: a bounded single-producer/single-consumer (SPSC) replay pipeline (`run_spsc_replay`)
  for systems evaluation. It is not production networking, not live exchange connectivity, not a
  production-HFT or lock-free trading architecture, and not a latency guarantee. The default
  backpressure policy is lossless blocking (the producer waits when the bounded queue is full, so no
  event is dropped); an opt-in `DropNewestOnFull` policy exists for overload-shedding experiments
  only and is not correctness-preserving for order-book streams (dropping events creates sequence
  gaps and unknown-order references that deterministically halt replay). The SPSC consumer reuses the
  exact single-thread `ReplayEngine` processing path, so book/execution/diagnostics checksums are
  bit-identical to the single-thread path regardless of thread timing; only backpressure count and
  max queue depth are timing-dependent, and those are never part of any checksum. The pooled-book
  SPSC variant is not implemented because `ReplayEngine` is not templated on the book type;
  `PooledOrderBook` is exercised by the existing single-thread hot-path benchmark rows instead;
- steady-state SPSC replay evaluation (`run_spsc_replay_steady_state`) and
  `ReplayValidationMode::Light` are opt-in benchmark/evaluation tools. `Light` avoids repeated full
  invariant walks during each event by keeping cheap per-event top-of-book checks and running full
  validation at end-of-replay. It is for throughput measurement on large corpora, not a replacement
  for the default correctness-first `Full` validation mode;
- benchmark results are hardware-dependent; generic dumps are not checked in;
- the checked-in benchmark report is representative local evidence for one laptop and one optimized
  path, not a portable latency claim;
- benchmark regression comparison results are machine-dependent and are only meaningful when both
  JSON files come from the same controlled hardware; the checked-in `sample_benchmark_*.json` files
  are synthetic tooling fixtures, not measurements;
- latency-budget observed nanoseconds are machine-dependent; only the configuration checksum and the
  accounting logic are deterministic, and there are no built-in latency targets;
- strategy examples are deterministic workloads, not profitable trading strategies;
- inference defaults to a deterministic linear backend with measured latency accounting and policy
  hooks; an optional ONNX Runtime backend exists behind the `ASTERION_USE_ONNXRUNTIME` CMake flag but
  is only exercised by a manual CI toggle, and ONNX requests fall back to `LinearModel` when the
  dependency is absent; two checked-in ChronosLOB ONNX artefacts exercise model-plumbing only when a
  real ONNX Runtime build is available — a tiny deterministic hand-written fixture, and a tiny
  **real** ChronosLOB `DeepLOBModel` trained on synthetic toy data and exported from ChronosLOB
  (`reports/chronoslob_real_model_bridge_report_2026_06_01.md`). The real artefact is trained but on
  synthetic toy data with a reduced 4-feature, single-timestep simplification; it carries **no**
  predictive-quality, profitability, alpha, live-trading or production-serving claim, and the
  Asterion-side score is a deterministic plumbing value only;
- inference benchmarks measure plumbing cost only (feature extraction, model scoring, policy
  accounting) and make no predictive-quality, signal-value or profitability claim; per-call latency
  percentiles for sub-microsecond operations are dominated by the timer resolution and are
  representative-local, not portable; the vector-returning feature extraction convenience path still
  allocates one vector per call, while the caller-owned-buffer path is only claimed allocation-free
  in its scoped warmed tests/benchmark rows; ONNX inference is not claimed to be allocation-free;
- the timeout/late-signal policy can disable the model after repeated late signals only when
  explicitly configured; by default the model is never disabled and the gate merely abstains on
  individual late or timed-out signals;
- snapshot loading reconstructs the L3 book from framed single-order Snapshot records; it cannot
  represent an aggregated L2-only image whose levels lack per-order detail;
- the new risk controls (open-order exposure, message-rate limiting, self-trade prevention,
  cancel-on-disconnect cancellation) are opt-in and disabled by default; sliding-window rate
  limiting is available but stores per-client timestamps while enabled;
- cancel-on-kill and cancel-on-disconnect release tracked simulated working exposure inside the risk
  gateway; they do not send live exchange/broker cancels;
- `SimulatedBrokerSession` is an in-process deterministic lifecycle model; it is not a broker
  adapter, does not maintain real sessions and never sends network messages;
- replace-order risk checks apply to tracked resting simulated orders after an execution report has
  bound the exchange order ID; they are not a full broker order-management system;
- `PortfolioRiskMonitor` is a simulated accounting gate over caller-supplied marks and positions;
  it does not provide live portfolio management, market-risk data or cross-symbol matching;
- persistent risk audit logging, rotation, verification and audit manifests are opt-in and
  append-only; optional HMAC-SHA256 manifest signing uses caller-managed local keys and is not a
  retention policy, custody system, compliance guarantee or tamper-proof storage layer;
- `MultiSymbolBookSet` powers an opt-in shared replay path, but it is not a cross-symbol matching
  engine and does not replace the default grouped single-symbol replay path; grouped replay remains
  the default unless shared replay parity is exhaustively validated and documented;
- `PooledOrderBook` is an opt-in allocation experiment for measured L3 replay. It is not production
  HFT infrastructure, not a general allocator framework, and not a replacement for the
  correctness-first `OrderBook`; its zero-allocation result applies only after explicit warm-up and
  reservation in the disclosed benchmark/test paths. Current pooled validation is single-symbol at
  the book/benchmark layer; generated multi-symbol-style streams are grouped per symbol for parity
  checks and are not run through the single-symbol hot-path benchmark;
- historical benchmark trends are only meaningful on the same controlled hardware and are kept out of
  CI performance gates;
- the larger-corpus Linux performance-evaluation path is manual/non-blocking and produces
  representative measurements for the stated machine/environment only; if `perf` counters are
  unavailable locally, on HPC allocations or on GitHub-hosted runners, the blocker is documented and
  no counter values are fabricated. The **primary** performance context is the Durham Hamilton8 HPC
  pass collected on 2026-06-04 (`reports/durham_hpc_performance_evaluation_2026_06_04.md`): it is
  **one shared Slurm compute-node allocation** (Rocky Linux/GCC, no `LLC` events, no root control
  over governor/turbo, GCC evidence only, one inference hotspot report OOM-killed) - a representative
  shared-HPC measurement only, not portable, not production-HFT. The earlier WSL2 `perf` pass
  collected on 2026-06-01 (`reports/linux_performance_evaluation_2026_06_01.md`) and the original
  Windows/MSYS2 laptop run are retained as **historical / local development** baselines: the WSL2 run
  is **one laptop, a virtualized PMU** (no `LLC` cache events, time-multiplexed counters, uncontrolled
  CPU turbo), representative WSL2 measurements only, not native/cloud Linux and not portable. Durham
  supersedes the laptop/WSL context only for the paths it actually measured; not every old result was
  re-run on Hamilton and cross-machine comparison is not meaningful. Because per-run
  `Full`-validation replay is O(resting-book size)
  per event, the 1M Linux rows use `--steady-state-validation-mode light`, a throughput-evaluation
  mode; correctness stays covered by the default `Full` validation in `ctest` and by end-of-replay
  checksum parity. The optional ONNX Runtime backend was not built in those Linux runs, so the ONNX
  replay-loop row is recorded as skipped, not measured;
- event-log schema v1 is stable for the checked-in fixtures and guarded by manifest/tests, but
  Asterion does not yet ship a general multi-version migration framework. Breaking schema changes
  must bump the version, update `docs/event_log_schema.md`, regenerate affected fixtures and
  document how old logs should be converted;
- the recorded Binance public depth case study is a recorded public market-data engineering demo: it
  is not live trading, not authenticated exchange connectivity, and not evidence of equities-market
  realism. Capture uses only the public REST `/api/v3/depth` endpoint with no API keys, performs no
  order placement and is opt-in/manual (never run in CI). Binance depth is L2 price-level data, so the
  normaliser models each price level as a single synthetic-order with deterministic synthetic order
  IDs and level-replacement semantics; it does not provide real L3 order identity, per-level FIFO
  depth, individual order sizes or true order lifetimes, and makes no profitability claim. The
  checked-in fixture is tiny and hand-curated for deterministic CI; large local captures are
  git-ignored.

These constraints are deliberate. The first version is designed to make correctness, determinism and benchmarking boundaries solid before adding performance-specific complexity.
