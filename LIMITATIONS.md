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
- no concurrency model in the first implementation;
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
  dependency is absent; the checked-in ChronosLOB-style ONNX artefact is a tiny deterministic
  fixture with metadata, not a trained model, and is used only to exercise model-plumbing when a real
  ONNX Runtime build is available;
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
  unavailable locally or on GitHub-hosted runners, the blocker is documented and no counter values
  are fabricated;
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
