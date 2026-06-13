from __future__ import annotations

import hashlib
import json
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]
METADATA = ROOT / "data" / "models" / "chronoslob_tiny_fixture.metadata.json"
MODEL = ROOT / "data" / "models" / "chronoslob_tiny_fixture.onnx"
REAL_METADATA = ROOT / "data" / "models" / "chronoslob_tiny_real.metadata.json"
REAL_MODEL = ROOT / "data" / "models" / "chronoslob_tiny_real.onnx"


def test_chronoslob_fixture_metadata_is_explicit() -> None:
    payload = json.loads(METADATA.read_text(encoding="utf-8"))

    assert MODEL.exists()
    assert MODEL.stat().st_size < 10_000
    assert payload["model_name"] == "chronoslob_tiny_fixture"
    assert payload["trained_model"] is False
    assert payload["deterministic_fixture"] is True
    assert payload["input_shape"] == [1, 4]
    assert payload["output_shape"] == [1, 1]
    assert payload["feature_count"] == 4
    assert payload["feature_version"] == 1
    assert payload["feature_order"] == [
        "spread_ticks",
        "mid_price_ticks",
        "top_level_imbalance",
        "top_level_quantity",
    ]
    assert "Research-model-to-systems integration" in payload["claim_boundary"]


def test_chronoslob_fixture_reference_output_is_deterministic() -> None:
    payload = json.loads(METADATA.read_text(encoding="utf-8"))
    score = (
        sum(
            weight * value
            for weight, value in zip(payload["reference_weights"], payload["expected_test_input"])
        )
        + payload["reference_bias"]
    )

    assert score == pytest.approx(payload["expected_test_output"][0])


def test_chronoslob_real_metadata_is_explicit() -> None:
    payload = json.loads(REAL_METADATA.read_text(encoding="utf-8"))

    assert REAL_MODEL.exists()
    assert REAL_MODEL.stat().st_size < 50_000  # tiny artefact, safe to commit
    assert payload["model_name"] == "chronoslob_tiny_real"
    assert payload["model_class"] == "DeepLOBModel"
    assert payload["artefact_type"] == "trained_synthetic_smoke"
    assert payload["trained_model"] is True
    assert payload["deterministic_fixture"] is False
    assert payload["input_shape"] == [1, 1, 4]
    assert payload["output_shape"] == [1, 3]
    assert payload["feature_count"] == 4
    assert payload["feature_version"] == 1
    assert payload["feature_order"] == [
        "spread_ticks",
        "mid_price_ticks",
        "top_level_imbalance",
        "top_level_quantity",
    ]
    # Scope boundary: integration and latency only, with no predictive claim.
    assert "systems-integration" in payload["claim_boundary"]
    assert payload["training"]["data"] == "synthetic_toy"
    # The recorded expected output is the per-model determinism contract.
    assert len(payload["expected_test_input"]) == 4
    assert len(payload["expected_test_output"]) == 3
    # No hand-written linear head for a real exported model.
    assert "reference_weights" not in payload


def test_chronoslob_real_artifact_sha256_matches_metadata() -> None:
    payload = json.loads(REAL_METADATA.read_text(encoding="utf-8"))
    digest = hashlib.sha256(REAL_MODEL.read_bytes()).hexdigest()
    assert digest == payload["onnx_sha256"]
