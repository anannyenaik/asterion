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
- no custom allocator or allocation-avoidance claim;
- no concurrency model in the first implementation;
- benchmark results are hardware-dependent and not checked in;
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
- replace-order risk checks apply to tracked resting simulated orders after an execution report has
  bound the exchange order ID; they are not a full broker order-management system;
- persistent risk audit logging, rotation and verification are opt-in and append-only, but signing,
  retention policy and tamper-evident storage are outside this repository;
- `MultiSymbolBookSet` powers an opt-in shared replay path, but it is not a cross-symbol matching
  engine and does not replace the default grouped single-symbol replay path;
- historical benchmark trends are only meaningful on the same controlled hardware and are kept out of
  CI performance gates.

These constraints are deliberate. The first version is designed to make correctness, determinism and benchmarking boundaries solid before adding performance-specific complexity.
