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
- no custom allocator and no broad allocation-avoidance claim; only the documented reusable L2,
  fixed strategy callback and reserved risk sub-paths are allocation-free after warm-up;
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
  dependency is absent; the checked-in ONNX fixture is tiny and only used when a real ONNX Runtime
  build is available;
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
- historical benchmark trends are only meaningful on the same controlled hardware and are kept out of
  CI performance gates.

These constraints are deliberate. The first version is designed to make correctness, determinism and benchmarking boundaries solid before adding performance-specific complexity.
