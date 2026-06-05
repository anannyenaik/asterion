"""Validation for the recorded-public-L2 ChronosLOB model-contract artefact.

This guards the *systems/integration contract* of the windowed DeepLOB-style
artefact exported from recorded public Binance crypto L2 depth: metadata schema,
window/shape contract, normalisation metadata, content checksums (model, source
dataset, expected fixtures) and — when ``onnxruntime`` is importable — that the
committed model reproduces its recorded expected output.

No predictive-quality, profitability, alpha or trading claim is made or tested
here; any accuracy in the metadata is diagnostic context only.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
MODELS = ROOT / "data" / "models"
MODEL = MODELS / "chronoslob_public_l2_tiny.onnx"
METADATA = MODELS / "chronoslob_public_l2_tiny.metadata.json"
EXPECTED_INPUT = MODELS / "chronoslob_public_l2_tiny.expected_input.json"
EXPECTED_OUTPUT = MODELS / "chronoslob_public_l2_tiny.expected_output.json"
MANIFEST = MODELS / "chronoslob_public_l2_tiny.manifest.json"
DATASET = ROOT / "data" / "samples" / "binance_public_l2_window_sample.jsonl"

_BANNED_CLAIM_TERMS = ("alpha", "profit", "predictive")


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def test_public_l2_artefacts_exist_and_are_compact() -> None:
    for path in (MODEL, METADATA, EXPECTED_INPUT, EXPECTED_OUTPUT, MANIFEST, DATASET):
        assert path.exists(), f"missing artefact: {path}"
    # Tiny artefact, safe to commit; the recorded dataset stays a compact fixture.
    assert MODEL.stat().st_size < 100_000
    assert DATASET.stat().st_size < 200_000


def test_public_l2_metadata_contract_is_explicit() -> None:
    meta = json.loads(METADATA.read_text(encoding="utf-8"))

    assert meta["model_name"] == "chronoslob_public_l2_tiny"
    assert meta["model_class"] == "DeepLOBModel"
    assert meta["artefact_type"] == "trained_recorded_public_l2"
    assert meta["trained_model"] is True
    assert meta["deterministic_fixture"] is False

    # Multi-timestep window contract (window length > 1).
    assert meta["window_length"] == 16
    assert meta["window_length"] > 1
    assert meta["feature_count"] == 40
    assert meta["input_shape"] == [1, 16, 40]
    assert meta["output_shape"] == [1, 3]
    assert meta["feature_levels_per_side"] == 10

    # Explicit feature schema and normalisation metadata.
    assert len(meta["feature_order"]) == 40
    assert meta["feature_order"][0] == "bid_price_rel_0"
    assert meta["feature_order"][-1] == "ask_qty_9"
    norm = meta["normalisation"]
    assert len(norm["mean"]) == 40
    assert len(norm["std"]) == 40
    assert "zscore" in norm["scheme"]

    # Flattened input value count = feature_count * window_length.
    assert len(meta["expected_test_input"]) == 40 * 16
    assert len(meta["expected_test_output"]) == 3

    # Recorded public data provenance + checksums.
    assert meta["source_data"]["kind"] == "recorded_public_binance_l2_depth"
    assert meta["source_data"]["symbol"] == "BTCUSDT"
    assert meta["training"]["data"] == "recorded_public_binance_l2"
    assert len(meta["onnx_sha256"]) == 64
    assert len(meta["source_data_sha256"]) == 64

    # Honest claim boundary: systems/integration only, no trading claim.
    boundary = meta["claim_boundary"].lower()
    assert "model-contract" in boundary or "integration" in boundary
    for term in _BANNED_CLAIM_TERMS:
        assert term not in boundary
    assert any("diagnostic" in lim.lower() for lim in meta["claim_limitations"])


def test_public_l2_model_sha256_matches_metadata() -> None:
    meta = json.loads(METADATA.read_text(encoding="utf-8"))
    assert _sha256(MODEL) == meta["onnx_sha256"]


def test_public_l2_source_dataset_sha256_matches_metadata() -> None:
    meta = json.loads(METADATA.read_text(encoding="utf-8"))
    assert _sha256(DATASET) == meta["source_data_sha256"]
    assert _sha256(DATASET) == meta["source_data"]["sha256"]


def test_public_l2_manifest_checksums_match_files() -> None:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    for name, entry in manifest["artefacts"].items():
        path = MODELS / name
        assert path.exists(), f"manifest references missing file: {name}"
        assert _sha256(path) == entry["sha256"], f"checksum mismatch for {name}"
        assert path.stat().st_size == entry["bytes"]
    assert _sha256(DATASET) == manifest["source_data"]["sha256"]


def test_public_l2_expected_fixtures_match_metadata() -> None:
    meta = json.loads(METADATA.read_text(encoding="utf-8"))
    expected_input = json.loads(EXPECTED_INPUT.read_text(encoding="utf-8"))
    expected_output = json.loads(EXPECTED_OUTPUT.read_text(encoding="utf-8"))

    assert expected_input["shape"] == meta["input_shape"]
    assert expected_input["data"] == meta["expected_test_input"]
    assert expected_output["shape"] == meta["output_shape"]
    assert expected_output["data"] == meta["expected_test_output"]


def test_public_l2_model_reproduces_expected_output_via_onnxruntime() -> None:
    ort = pytest.importorskip("onnxruntime")
    import numpy as np

    meta = json.loads(METADATA.read_text(encoding="utf-8"))
    array = np.asarray(meta["expected_test_input"], dtype=np.float32).reshape(meta["input_shape"])
    session = ort.InferenceSession(str(MODEL), providers=["CPUExecutionProvider"])
    result = np.asarray(session.run(None, {meta["input_name"]: array})[0]).reshape(-1)
    expected = np.asarray(meta["expected_test_output"], dtype=np.float64)
    assert float(np.max(np.abs(result.astype(np.float64) - expected))) < 1e-4
