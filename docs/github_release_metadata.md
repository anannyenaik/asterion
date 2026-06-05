# GitHub And v0.1.0 Release Metadata

This document prepares owner-side repository metadata and the final `v0.1.0`
release. It does **not** cut the release or change GitHub settings by itself.

## Recommended Repository Metadata

**Description**

> Deterministic C++20 trading systems lab for market replay, L3 order books, matching, risk controls, telemetry and measured ONNX inference.

**Topics**

```text
cpp20
low-latency
trading-systems
order-book
matching-engine
market-data
deterministic-replay
risk-management
onnxruntime
pybind11
linux
performance-engineering
```

**Pinned-repository blurb**

> Deterministic C++20 systems lab with replayable market-data fixtures, L3 books,
> matching and risk semantics, checksum-backed correctness evidence, Durham HPC
> performance reports, and optional measured ONNX integration.

GitHub description, topics and profile pinning are owner-side settings. They
cannot be fully applied by a normal repository commit without an authenticated
GitHub CLI/API session.

## Suggested Final Release

- Title: **Asterion v0.1.0: deterministic trading systems lab**
- Tag: `v0.1.0`, annotated, pointing at the final verified commit on `main`
- Framing: reviewer-facing reproducibility, architecture and performance-evidence
  polish for a deterministic systems lab
- Do not frame as production readiness, production-HFT validation, portable
  benchmark proof, live-trading capability or production model serving

## Release Body Outline

1. Short project summary and explicit non-claims.
2. Reviewer path: devcontainer or local Release build, `ctest`, `pytest`, demo.
3. Architecture overview link.
4. Correctness and determinism evidence.
5. Primary Durham HPC performance evidence, with environment limitations.
6. Optional public-L2 ONNX model-contract evidence, clearly systems-only.
7. Known limitations and deferred work.

## Final Release Checklist

- Confirm `main` is clean and up to date.
- Run [RELEASE_CHECKLIST.md](../RELEASE_CHECKLIST.md) end to end.
- Build and test the devcontainer on a Docker-capable host.
- Confirm regular CI and sanitizer CI are green on the exact release commit.
- Recheck README benchmark values against their linked source reports.
- Recheck [claim_audit.md](claim_audit.md) and [LIMITATIONS.md](../LIMITATIONS.md).
- Confirm no generated corpora, benchmark JSON, profiler output, build artifacts,
  caches, secrets or `.env` files are staged.
- Update final release notes with the verified commit and CI run links.
- Apply the recommended GitHub description/topics and pinning owner-side.
- Create the annotated tag only after all checks pass.

## Later Tag And Release Commands

Run these only in the final-release task, after replacing placeholders and
verifying the exact commit:

```bash
git switch main
git pull --ff-only
git status --short --branch
git tag -a v0.1.0 -m "Asterion v0.1.0: deterministic trading systems lab"
git push origin v0.1.0
gh release create v0.1.0 --title "Asterion v0.1.0: deterministic trading systems lab" --notes-file RELEASE_NOTES.md
```

Do not create or push the tag until the final release body describes the current
commit and both default CI workflows are green.
