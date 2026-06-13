# Reproducible Development Environment

The checked-in devcontainer provides an Ubuntu 24.04 development path with GCC,
Clang and its sanitizer runtime, CMake, Ninja, Python, pytest, git and basic
build tools. It deliberately does **not** install ONNX Runtime, download
model-serving dependencies, run benchmarks or build the project automatically.

Validation status: the Dockerfile and documented Docker-only path were validated
on **2026-06-05** using a local Windows Docker Desktop Linux-container
installation (Docker Desktop 4.76.0, Engine 29.5.2). The built Ubuntu 24.04 image
used GCC 13.3, Clang 18.1, CMake 3.28, Ninja 1.11, Python 3.12 and pytest 7.4.
It passed:

- GCC Release configure/build and `ctest`;
- Python-enabled GCC Release configure/build, `ctest`, pytest
  (`108 passed, 1 optional ONNX Runtime test skipped`) and the evaluation demo;
- Clang Debug ASan/UBSan configure/build and `ctest`.

The validation fixed two image-build gaps: Ubuntu 24.04 already reserves
UID/GID 1000, and Clang's sanitizer runtime is not installed when recommended
packages are disabled. The Dockerfile now handles both explicitly.

The Dev Container CLI path was subsequently exercised on **2026-06-06** on the
same host (Dev Container CLI 0.87.0 on Node.js 24.15.0, Docker Desktop Engine
29.5.2). `devcontainer up --workspace-folder .` built and started the container
(`remoteUser` `vscode`, workspace `/workspaces/Asterion`), and the documented
checks were run inside it via
`devcontainer exec --workspace-folder . bash -lc '<command>'`:

- Release C++ configure/build and `ctest` (passed);
- Python-enabled configure/build and pytest
  (`157 passed, 1 optional ONNX Runtime test skipped`);
- the evaluation demo (`scripts/run_demo.sh --skip-build`);
- Clang Debug ASan/UBSan configure/build and `ctest` (passed).

These were the documented commands run verbatim except that container-local build
directories (`build-dc`, `build-dc-python`, `build-dc-sanitize`) were used, so the
bind-mounted workspace's pre-existing native host build tree was left untouched.
ONNX Runtime and heavy benchmarks remained optional and were not part of this
validation. `devcontainer.json` uses the validated Dockerfile, `vscode` user and
`/workspaces/<repo>` layout. This record is one local development
validation, not a portable or production deployment guarantee.

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

This environment is a reproducible development path, not production
deployment or production model-serving validation.
