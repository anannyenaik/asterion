from __future__ import annotations

import json
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]
METADATA = ROOT / "data" / "models" / "chronoslob_tiny_fixture.metadata.json"
MODEL = ROOT / "data" / "models" / "chronoslob_tiny_fixture.onnx"


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
