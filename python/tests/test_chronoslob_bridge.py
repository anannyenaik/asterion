from __future__ import annotations

import hashlib
import json
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
MODELS = ROOT / "data" / "models"
FIXTURE_METADATA = MODELS / "chronoslob_tiny_fixture.metadata.json"
FIXTURE_MODEL = MODELS / "chronoslob_tiny_fixture.onnx"
SYNTHETIC_METADATA = MODELS / "chronoslob_tiny_synthetic.metadata.json"
SYNTHETIC_MODEL = MODELS / "chronoslob_tiny_synthetic.onnx"

FEATURE_ORDER = [
    "spread_ticks",
    "mid_price_ticks",
    "top_level_imbalance",
    "top_level_quantity",
]


def test_fixture_metadata_declares_its_contract() -> None:
    payload = json.loads(FIXTURE_METADATA.read_text(encoding="utf-8"))

    assert FIXTURE_MODEL.exists()
    assert FIXTURE_MODEL.stat().st_size < 10_000
    assert payload["model_name"] == "chronoslob_tiny_fixture"
    assert payload["trained_model"] is False
    assert payload["deterministic_fixture"] is True
    assert payload["input_shape"] == [1, 4]
    assert payload["output_shape"] == [1, 1]
    assert payload["feature_count"] == 4
    assert payload["feature_version"] == 1
    assert payload["feature_order"] == FEATURE_ORDER
    # Version fields needed to regenerate the artefact from scripts/.
    assert payload["opset_version"] == 13
    assert payload["ir_version"] == 8
    assert payload["producer_name"] == "asterion"


def test_fixture_reference_output_is_deterministic() -> None:
    payload = json.loads(FIXTURE_METADATA.read_text(encoding="utf-8"))
    score = (
        sum(
            weight * value
            for weight, value in zip(
                payload["reference_weights"], payload["expected_test_input"], strict=True
            )
        )
        + payload["reference_bias"]
    )

    assert score == pytest.approx(payload["expected_test_output"][0])


def test_synthetic_metadata_declares_its_contract() -> None:
    payload = json.loads(SYNTHETIC_METADATA.read_text(encoding="utf-8"))

    assert SYNTHETIC_MODEL.exists()
    assert SYNTHETIC_MODEL.stat().st_size < 50_000
    assert payload["model_name"] == "chronoslob_tiny_synthetic"
    assert payload["model_class"] == "DeepLOBModel"
    assert payload["artefact_type"] == "trained_synthetic_smoke"
    assert payload["trained_model"] is True
    assert payload["deterministic_fixture"] is False
    assert payload["input_shape"] == [1, 1, 4]
    assert payload["output_shape"] == [1, 3]
    assert payload["feature_count"] == 4
    assert payload["feature_version"] == 1
    assert payload["feature_order"] == FEATURE_ORDER
    assert payload["training"]["data"] == "synthetic_toy"
    # The recorded expected input and output are the determinism contract.
    assert len(payload["expected_test_input"]) == 4
    assert len(payload["expected_test_output"]) == 3
    # A trained artefact carries no hand-written linear head.
    assert "reference_weights" not in payload


def test_synthetic_artefact_sha256_matches_metadata() -> None:
    payload = json.loads(SYNTHETIC_METADATA.read_text(encoding="utf-8"))
    digest = hashlib.sha256(SYNTHETIC_MODEL.read_bytes()).hexdigest()
    assert digest == payload["onnx_sha256"]


def test_synthetic_artefact_reproduces_expected_output_via_onnxruntime() -> None:
    ort = pytest.importorskip("onnxruntime")
    import numpy as np  # noqa: PLC0415 - only needed once onnxruntime is present

    payload = json.loads(SYNTHETIC_METADATA.read_text(encoding="utf-8"))
    array = np.asarray(payload["expected_test_input"], dtype=np.float32).reshape(
        payload["input_shape"]
    )
    session = ort.InferenceSession(str(SYNTHETIC_MODEL), providers=["CPUExecutionProvider"])
    result = np.asarray(session.run(None, {payload["input_name"]: array})[0]).reshape(-1)
    expected = np.asarray(payload["expected_test_output"], dtype=np.float64)
    assert float(np.max(np.abs(result.astype(np.float64) - expected))) < 1e-4
