# Reproducible Development Environment

The checked-in devcontainer provides an Ubuntu 24.04 development path with GCC,
Clang, CMake, Ninja, Python, pytest, git and basic build tools. It deliberately
does **not** install ONNX Runtime, download model-serving dependencies, run
benchmarks or build the project automatically.

Validation status: the definition was added from a Windows development shell
where Docker was not installed, so the image has **not been locally built** in
this change. Its commands mirror the default Linux CI paths. Final release
verification should build the image on a Docker-capable host.

## Open In A Devcontainer

With VS Code, Docker and the Dev Containers extension installed:

1. Open the repository.
2. Run **Dev Containers: Reopen in Container**.
3. Use the integrated terminal for the commands below.

The Dev Container CLI is an alternative:

```bash
devcontainer up --workspace-folder .
devcontainer exec --workspace-folder . bash
```

## Release Build And C++ Tests

This is the dependency-light default path. It may fetch Catch2 during the first
configure when a system Catch2 v3 package is unavailable.

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DASTERION_ENABLE_WARNINGS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Python Bindings And Pytest

```bash
cmake -S . -B build-python -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DASTERION_ENABLE_WARNINGS=ON \
  -DASTERION_BUILD_PYTHON=ON
cmake --build build-python
PYTHONPATH=build-python/python python3 -m pytest python/tests
```

## Demo Smoke Test

The demo uses checked-in sample data and writes ignored outputs under
`build-python/demo/`. It generates a small local benchmark JSON as a smoke test;
it does not run the large performance-evaluation corpora.

```bash
./scripts/run_demo.sh --build-dir build-python --skip-build
```

## Sanitizer Build And Tests

```bash
CC=clang CXX=clang++ cmake -S . -B build-sanitize -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DASTERION_ENABLE_WARNINGS=ON \
  -DASTERION_ENABLE_SANITIZERS=ON \
  -DASTERION_BUILD_BENCHMARKS=OFF
cmake --build build-sanitize --target asterion_tests
ctest --test-dir build-sanitize --output-on-failure
```

## Docker-Only Use

Without a devcontainer-aware editor:

```bash
docker build -f .devcontainer/Dockerfile -t asterion-dev .
docker run --rm -it \
  -v "$PWD:/workspaces/Asterion" \
  -w /workspaces/Asterion \
  asterion-dev
```

Run the Release commands above inside the container. Build directories remain
git-ignored and should not be committed. [`.dockerignore`](../.dockerignore)
also keeps local builds, caches, generated corpora, raw profiler data and
environment files out of the Docker build context.

## Optional ONNX Runtime

ONNX Runtime is intentionally outside the default image. To exercise it, provide
an external installation and configure with
`-DASTERION_USE_ONNXRUNTIME=ON -DONNXRUNTIME_ROOT=/path/to/onnxruntime`.
When the dependency is absent, Asterion preserves the deterministic
`LinearModel` fallback. See [chronoslob_bridge.md](chronoslob_bridge.md).

This environment is a reproducible reviewer/development path, not production
deployment or production model-serving validation.
