# ChronosLOB Public-L2 Model-Bridge Report (2026-06-04)

This report documents a **recorded-public-data model-contract artefact** that
moves a research-style LOB model into Asterion's deterministic inference path. It
supersedes the synthetic-toy ChronosLOB artefact (`chronoslob_tiny_real`) **for
model-contract evidence** with a genuine, if tiny, model trained on **recorded
public Binance crypto L2 depth**, exposing a richer windowed contract.

**Scope / claim boundary.** This is *recorded-public-data model-contract evidence
for moving a research-style LOB model into Asterion's deterministic inference
path* — a systems/integration artefact. It does **not** claim predictive quality,
trading profitability, alpha, live trading, authenticated/broker connectivity,
order placement, production model-serving, production HFT, portable latency,
equities-market realism or L3 exchange-feed realism. Any accuracy/loss below is
**diagnostic context only**, with no trading significance.

> Latency numbers below are representative local measurements on this
> machine/environment, not portable performance claims. Durham Hamilton8 HPC
> remains Asterion's primary performance context; this artefact's timings are an
> isolated, local ONNX Runtime diagnostic only.

## Executive summary

* A tiny `DeepLOBModel` (CNN-LSTM) was trained on a compact **recorded public
  Binance crypto L2 depth** window sample and exported to ONNX with a **windowed
  `[1, 16, 40] → [1, 3]`** contract — window length **16** (greater than 1) and a
  classic **40-dim** DeepLOB-style LOB frame per timestep.
* Asterion's model-metadata schema and C++ validation were extended to understand
  windowed models (`window_length`), recorded-data provenance + model checksums
  (`source_data_sha256`, `onnx_sha256`) and a new `trained_recorded_public_l2`
  artefact type — **backward compatible** with the existing fixture/real
  artefacts.
* The artefact is validated in both the default (no ONNX Runtime) and optional
  ONNX Runtime lanes, and in Python; the deterministic `LinearModel` fallback and
  the live 4-feature contract gate are preserved.

## What changed versus the synthetic-toy ChronosLOB artefact

| aspect | `chronoslob_tiny_real` (synthetic toy) | `chronoslob_public_l2_tiny` (this artefact) |
|---|---|---|
| training data | seeded **synthetic toy** rows | **recorded public Binance crypto L2 depth** (BTCUSDT REST `/api/v3/depth`) |
| input | `[1, 1, 4]` (single timestep) | `[1, 16, 40]` (**window length 16**) |
| per-timestep features | 4 (Asterion L2 buffer order) | **40** = 10 levels × {bid price, bid qty, ask price, ask qty} |
| normalisation metadata | none (toy standardised) | mid-relative prices + **per-feature z-score (mean/std × 40)** recorded |
| source-data checksum | n/a | `source_data_sha256` over the committed dataset |
| expected fixtures | inline only | inline **+ standalone** `expected_input`/`expected_output` JSON |
| checksum manifest | n/a | `*.manifest.json` (SHA-256 + bytes of every artefact + dataset) |
| label | artificial 3-class synthetic rule | direction of mid 5 steps ahead (diagnostic) |

The synthetic-toy `chronoslob_tiny_real` and the hand-written
`chronoslob_tiny_fixture` **remain checked in** as historical/plumbing context
(see [chronoslob_real_model_bridge_report_2026_06_01.md](chronoslob_real_model_bridge_report_2026_06_01.md)
and [chronoslob_onnx_bridge_report_2026_05_31.md](chronoslob_onnx_bridge_report_2026_05_31.md));
they are not deleted. This artefact supersedes them only for *model-contract
evidence*.

## Data source and limitations

| field | value |
|---|---|
| source | `https://api.binance.com` public REST `/api/v3/depth` (snapshot poll) |
| symbol | `BTCUSDT` |
| capture | `tools/capture_binance_depth.py`, manual/opt-in, never in CI |
| committed dataset | `data/samples/binance_public_l2_window_sample.jsonl` (96 snapshots, top-10 per side, ~66 KB) |
| `source_data_sha256` | `9ca17722baed10103c2a09d388a2f42cdc3f86949a46fbecea249500840cffb8` |

Recorded **public crypto L2 depth only**: no API keys, no account/order
endpoints, no authenticated connectivity, no private data. The committed sample
is a compact top-10 subset of a larger local capture (kept under the ignored
`data/captures/`). It is **not** a full market dataset, **not** L3 order-level
data and carries **no** equities-market-realism claim. The window set is small
and heavily overlapping by construction.

## Model contract

| field | value |
|---|---|
| model name | `chronoslob_public_l2_tiny` |
| model class | `DeepLOBModel` (CNN-LSTM) |
| artefact type | `trained_recorded_public_l2` |
| input | `features`, shape `[1, 16, 40]` |
| output | `logits`, shape `[1, 3]` (raw `[down, flat, up]`) |
| window length | **16** |
| per-timestep feature count | **40** |
| feature version | 1 |
| opset / IR | 17 / 8 |
| `onnx_sha256` | `26d7df8d5efee1beafc39924308231201ef095841aab1d0e330f53c8c893754d` |
| artefact size | 10,614 bytes |

### Feature schema (per timestep, 40-dim)

Grouped as `[bid_price_rel_0..9, bid_qty_0..9, ask_price_rel_0..9, ask_qty_0..9]`
(10 price levels per side × four channels). Price columns are **mid-relative**
(`price − mid`); all 40 columns are then standardised.

### Normalisation metadata

`normalisation.scheme = "mid_relative_then_per_feature_zscore"`: price columns are
mid-relative, then every feature is z-scored using a per-feature `mean[40]` /
`std[40]` computed over all timesteps in the committed dataset (no forward-label
leakage into the feature scale; `std` floored to avoid divide-by-zero). Both
vectors are recorded in the metadata so the transform is fully reproducible.

### Window length

16 timesteps (greater than 1). The flattened ONNX input value count is
`feature_count × window_length = 40 × 16 = 640`, matching `shape_value_count(input_shape)`.

## Training / export

| field | value |
|---|---|
| export tool | ChronosLOB `tools/export_asterion_public_l2_onnx.py` |
| ChronosLOB source commit | `4e8fd562280385ebc713b7b8a13593728e3a10f6` (clean, `source_dirty=false`) |
| seed | 7 (single-threaded, deterministic algorithms) |
| windows / snapshots | 76 windows from 96 snapshots |
| class balance | down 17 / flat 38 / up 21 |
| label | mid direction 5 steps ahead, median-abs-move threshold |
| optimiser / steps | Adam, lr 0.02, 300 full-batch steps, cross-entropy |
| initial train loss | 1.150748 |
| eval loss | 0.000342 |
| eval accuracy (diagnostic) | 1.000000 |
| parameters | ~1.7k |

**Read the accuracy honestly.** `eval_accuracy_diagnostic = 1.0` is *training-set*
accuracy on 76 tiny, heavily-overlapping windows — it indicates the network
**memorised** its smoke-scale training set, not predictive skill. There is **no**
held-out validation and **no** generalisation claim. The number exists only to
show the network is genuinely trained (loss falls from ~1.15 to ~3e-4) rather
than random; it has **no trading significance**.

Export command (run from the Asterion repo root, ChronosLOB checked out as a
sibling):

```bash
python ../ChronosLOB/chronos-lob/tools/export_asterion_public_l2_onnx.py \
  --dataset data/samples/binance_public_l2_window_sample.jsonl \
  --output  data/models/chronoslob_public_l2_tiny.onnx
python ../ChronosLOB/chronos-lob/tools/export_asterion_public_l2_onnx.py --verify \
  --output  data/models/chronoslob_public_l2_tiny.onnx
```

## Checksums / artefacts (Asterion `data/models/`)

| artefact | bytes | sha256 (prefix) |
|---|---|---|
| `chronoslob_public_l2_tiny.onnx` | 10,614 | `26d7df8d…` |
| `chronoslob_public_l2_tiny.metadata.json` | — | recorded in manifest |
| `chronoslob_public_l2_tiny.expected_input.json` | 16,199 | `d347390c…` |
| `chronoslob_public_l2_tiny.expected_output.json` | 221 | `040d3a6b…` |
| `chronoslob_public_l2_tiny.manifest.json` | 1,297 | — |
| source dataset | 67,132 | `9ca17722…` |

The manifest records SHA-256 + byte size for every emitted artefact and the
source dataset; `python/tests/test_chronoslob_public_l2_bridge.py` re-checks them.

### Deterministic test vector

* `expected_test_input`: a recorded normalised window (640 values, `[1,16,40]`).
* `expected_test_output` (ONNX Runtime): `[-3.168041, 6.623186, -2.361043]`.
* `--verify` reproduces this through ONNX Runtime with `max_abs_diff = 0.00e+00`.
* On this machine retraining was byte-identical (`onnx_sha256` matched on
  re-export), but the **committed artefact** — not bit-exact regeneration — is the
  determinism contract.

## ONNX validation result

* **Default lane (no ONNX Runtime):** `tests/unit/test_inference_backend.cpp`
  loads and validates the windowed metadata (`window_length=16`, `1x16x40`/`1x3`,
  feature_count 40, flattened 640, 64-char checksums), asserts the artefact is
  **not** Asterion's live 4-feature contract (feature-count mismatch), asserts a
  declared-feature-count ONNX request falls back to `LinearModel` before any load,
  and rejects a window-length that breaks the flattened shape. **All passed.**
* **Optional ONNX Runtime lane** (`-DASTERION_USE_ONNXRUNTIME=ON`, local ONNX
  Runtime 1.20.1): a new case loads `chronoslob_public_l2_tiny.onnx` directly
  (`backend=onnx`, `input_shape=1x16x40`, `output_shape=1x3`), feeds the 640-value
  recorded window and reproduces `expected_test_output[0]` deterministically. The
  test requires `active == Onnx`, so ONNX-named evidence cannot silently degrade
  to the fallback. **Full ONNX lane: 179 test cases, 115,017 assertions passed.**
* **Python** (`python/tests/test_chronoslob_public_l2_bridge.py`): metadata schema,
  window/shape contract, normalisation arrays, model/dataset/manifest checksums,
  expected-fixture/metadata consistency and (guarded by `importorskip`) ONNX
  Runtime reproduction. **7 passed.**

## Inference benchmark (local, isolated, diagnostic)

Isolated ONNX Runtime inference latency for this artefact, measured locally via
`--benchmark` (Python `onnxruntime` 1.20.1, CPU EP, single process, 20,000
steady-state iterations after warm-up):

| metric | value |
|---|---|
| p50 | 49.2 µs |
| p95 | 82.3 µs |
| p99 | 111.5 µs |
| p99.9 | 160.7 µs |
| max | 323.9 µs |
| throughput | ~18.8k inferences/s |

This is a **local Python ONNX Runtime diagnostic**, not the C++ hot path and not
portable. The C++ optional-lane replay-loop ONNX rows in
`benchmarks/benchmark_main.cpp` exercise the **live 4-feature** contract; this
windowed 40×16 artefact has a different input contract and is **not** wired into
those rows. A C++ optional-lane isolated-latency row for the windowed contract is
**pending** and is not fabricated here.

## Fallback behaviour

* Default builds do **not** require ONNX Runtime; `LinearModel` is the default and
  the fallback. CI stays dependency-light and network-free.
* This windowed artefact is a standalone model-contract artefact, not the live
  4-feature buffer model: requesting it against the live feature contract falls
  back to `LinearModel` with a clear `feature count mismatch` diagnostic (it
  cannot masquerade as the live model). The correctness-first default path is
  unchanged.

## What this proves

* A research-style, multi-timestep, 40-dim DeepLOB-style LOB model trained on
  **recorded public crypto L2 data** can be exported and moved into Asterion's
  deterministic inference path behind a validated, checksummed model contract.
* Asterion validates the full contract — windowed shapes, per-timestep feature
  count, normalisation metadata, recorded-data + model checksums and expected
  input/output fixtures — and reproduces the recorded output through ONNX Runtime.
* The optional ONNX path and the deterministic `LinearModel` fallback coexist;
  ONNX-named evidence cannot silently fall back.

## What this does not prove

* **Nothing about predictive quality, profitability, alpha or signal value.** The
  reported accuracy is training-set memorisation on a tiny overlapping window set.
* Nothing about live trading, authenticated/broker connectivity or order
  placement (none exist here).
* Nothing about production model-serving, production HFT or portable latency.
* No L3 / equities-market realism: the input is public crypto **L2** depth.

## Limitations

* Tiny, heavily-overlapping recorded window set; smoke-scale model; no held-out
  validation.
* Latency is a local Python ONNX Runtime diagnostic, not the C++ hot path and not
  portable; Durham HPC remains the primary performance context.
* ONNX Runtime is opt-in; default builds/CI never require it.
* The recorded dataset is a compact public-L2 subset, not a market dataset.

## Next work

1. CI visibility / matrix polish (surface the optional ONNX lane and the
   recorded-public-L2 contract checks in the documented matrix).
2. Matching / order-semantics polish.
3. A technical paper, only after the above and any final evidence polish.
