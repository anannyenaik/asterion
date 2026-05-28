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
- strategy examples are deterministic workloads, not profitable trading strategies;
- inference has a deterministic linear backend, measured latency accounting and policy hooks;
  external ONNX Runtime or LibTorch model execution is not integrated yet.

These constraints are deliberate. The first version is designed to make correctness, determinism and benchmarking boundaries solid before adding performance-specific complexity.
