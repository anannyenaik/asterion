# Benchmarks

Asterion includes a simple deterministic benchmark runner. It is not a substitute for a full latency lab, but it provides a clean place to measure changes without inventing numbers.

## Methodology

- Build in Release mode.
- Run on an otherwise quiet machine.
- Record compiler, CPU, OS, governor/power settings and commit hash.
- Run multiple iterations and compare distributions, not isolated best cases.
- Do not compare numbers casually across different machines.

## Commands

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target asterion_benchmarks
./build/asterion_benchmarks
```

The benchmark runner currently reports:

- add order;
- cancel order;
- simple match;
- replay of `data/samples/sample_replay.csv`.

## Environment Template

```text
Commit:
CPU:
RAM:
OS:
Kernel:
Compiler:
CMake:
Build type:
CPU governor / power mode:
Notes:
```

## Results Template

```text
add_order:
cancel_order:
simple_match:
sample_replay:
```

No benchmark results are committed because they are hardware-dependent and easy to misrepresent. Generate them locally and attach the environment fields when sharing results.
