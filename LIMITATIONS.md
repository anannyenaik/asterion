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
  is not exercised by CI, and ONNX requests fall back to `LinearModel` when the dependency is absent;
- snapshot loading reconstructs the L3 book from framed single-order Snapshot records; it cannot
  represent an aggregated L2-only image whose levels lack per-order detail;
- the new risk controls (open-order exposure, message-rate limiting, self-trade prevention) are
  opt-in and disabled by default; message-rate limiting is a fixed-window, not sliding-window, limiter;
- `MultiSymbolBookSet` is structural groundwork that routes events to per-symbol books; it is not a
  cross-symbol matching engine and does not replace the grouped single-symbol replay path;
- historical benchmark trends are only meaningful on the same controlled hardware and are kept out of
  CI performance gates.

These constraints are deliberate. The first version is designed to make correctness, determinism and benchmarking boundaries solid before adding performance-specific complexity.
