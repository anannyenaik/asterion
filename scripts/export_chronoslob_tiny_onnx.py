#!/usr/bin/env python3
"""Export Asterion's deterministic tiny ONNX inference fixture.

The fixture is a single Gemm node holding a fixed linear head over Asterion's
four-element L2 feature buffer. Its weights, bias and expected input/output are
constants in this module, so a clean clone regenerates the artefact and its
metadata byte-for-byte with no other checkout present.

    python scripts/export_chronoslob_tiny_onnx.py            # write the artefact
    python scripts/export_chronoslob_tiny_onnx.py --verify    # compare against the committed files

The optional ``onnx`` package is required only for regeneration. Asterion's
default build and test path imports neither ``onnx`` nor ONNX Runtime.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from typing import Any

IR_VERSION = 8
OPSET_VERSION = 13
PRODUCER_NAME = "asterion"
PRODUCER_VERSION = "chronoslob-bridge-2026-05-31"

MODEL_NAME = "chronoslob_tiny_fixture"
GRAPH_NAME = "chronoslob_tiny_fixture_graph"
INPUT_NAME = "features"
OUTPUT_NAME = "score"
INPUT_SHAPE = [1, 4]
OUTPUT_SHAPE = [1, 1]
FEATURE_VERSION = 1
FEATURE_ORDER = [
    "spread_ticks",
    "mid_price_ticks",
    "top_level_imbalance",
    "top_level_quantity",
]

# Feature naming and ordering follow the ChronosLOB research repository; the
# graph and its constants are defined here.
DERIVED_FROM = "https://github.com/anannyenaik/chronos-lob"

# Linear scoring head encoded as Gemm(features, weights) + bias. The constants
# are fixed and small; they are not fitted to any data.
REFERENCE_WEIGHTS = [0.1, -0.0005, 0.25, 0.0001]
REFERENCE_BIAS = 0.05
EXPECTED_TEST_INPUT = [2.0, 1000.0, 0.5, 400.0]
EXPECTED_TEST_OUTPUT = [
    sum(weight * value for weight, value in zip(REFERENCE_WEIGHTS, EXPECTED_TEST_INPUT, strict=True))
    + REFERENCE_BIAS
]

DEFAULT_MODEL_OUTPUT = pathlib.Path("data/models/chronoslob_tiny_fixture.onnx")
DEFAULT_METADATA_OUTPUT = pathlib.Path("data/models/chronoslob_tiny_fixture.metadata.json")

EXPORT_COMMAND = (
    "python scripts/export_chronoslob_tiny_onnx.py "
    "--output data/models/chronoslob_tiny_fixture.onnx "
    "--metadata-output data/models/chronoslob_tiny_fixture.metadata.json"
)


def build_model_bytes() -> bytes:
    try:
        import onnx  # noqa: PLC0415 - optional dependency, reported clearly below
        from onnx import TensorProto, helper  # noqa: PLC0415
    except ImportError as exc:  # pragma: no cover - optional dependency
        raise SystemExit(
            "the 'onnx' package is required to export the fixture: "
            "python -m pip install onnx"
        ) from exc

    input_info = helper.make_tensor_value_info(INPUT_NAME, TensorProto.FLOAT, INPUT_SHAPE)
    output_info = helper.make_tensor_value_info(OUTPUT_NAME, TensorProto.FLOAT, OUTPUT_SHAPE)
    weights = helper.make_tensor(
        "weights",
        TensorProto.FLOAT,
        [len(REFERENCE_WEIGHTS), 1],
        REFERENCE_WEIGHTS,
    )
    bias = helper.make_tensor("bias", TensorProto.FLOAT, [1], [REFERENCE_BIAS])
    node = helper.make_node(
        "Gemm",
        [INPUT_NAME, "weights", "bias"],
        [OUTPUT_NAME],
        name="chronoslob_tiny_linear_score",
        alpha=1.0,
        beta=1.0,
        transB=0,
    )
    graph = helper.make_graph(
        [node],
        GRAPH_NAME,
        [input_info],
        [output_info],
        [weights, bias],
    )
    model = helper.make_model(
        graph,
        producer_name=PRODUCER_NAME,
        producer_version=PRODUCER_VERSION,
        opset_imports=[helper.make_opsetid("", OPSET_VERSION)],
    )
    model.ir_version = IR_VERSION
    for key, value in {
        "model_name": MODEL_NAME,
        "feature_version": str(FEATURE_VERSION),
        "feature_order": ",".join(FEATURE_ORDER),
        "trained_model": "false",
        "deterministic_fixture": "true",
    }.items():
        prop = model.metadata_props.add()
        prop.key = key
        prop.value = value
    onnx.checker.check_model(model)
    return model.SerializeToString()


def build_metadata() -> dict[str, Any]:
    return {
        "model_name": MODEL_NAME,
        "purpose": (
            "Deterministic ONNX fixture used to exercise Asterion inference-path model "
            "loading, feature-contract validation and reproducible scoring."
        ),
        "derived_from": DERIVED_FROM,
        "export_command": EXPORT_COMMAND,
        "producer_name": PRODUCER_NAME,
        "producer_version": PRODUCER_VERSION,
        "ir_version": IR_VERSION,
        "opset_version": OPSET_VERSION,
        "input_name": INPUT_NAME,
        "input_shape": INPUT_SHAPE,
        "output_name": OUTPUT_NAME,
        "output_shape": OUTPUT_SHAPE,
        "feature_count": len(FEATURE_ORDER),
        "feature_version": FEATURE_VERSION,
        "feature_order": FEATURE_ORDER,
        "expected_test_input": EXPECTED_TEST_INPUT,
        "expected_test_output": EXPECTED_TEST_OUTPUT,
        "reference_weights": REFERENCE_WEIGHTS,
        "reference_bias": REFERENCE_BIAS,
        "trained_model": False,
        "deterministic_fixture": True,
        "output_semantics": (
            "One scalar from the fixed linear head over Asterion's four-element L2 "
            "feature buffer."
        ),
    }


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=pathlib.Path, default=DEFAULT_MODEL_OUTPUT)
    parser.add_argument("--metadata-output", type=pathlib.Path, default=DEFAULT_METADATA_OUTPUT)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args(argv)

    root = pathlib.Path(__file__).resolve().parents[1]
    model_bytes = build_model_bytes()
    metadata_text = json.dumps(build_metadata(), indent=2, sort_keys=True) + "\n"

    model_path = args.output if args.output.is_absolute() else root / args.output
    metadata_path = (
        args.metadata_output
        if args.metadata_output.is_absolute()
        else root / args.metadata_output
    )

    if args.verify:
        if model_path.read_bytes() != model_bytes:
            print(f"model mismatch: {model_path}", file=sys.stderr)
            return 1
        existing = json.loads(metadata_path.read_text(encoding="utf-8"))
        regenerated = json.loads(metadata_text)
        if existing != regenerated:
            print(f"metadata mismatch: {metadata_path}", file=sys.stderr)
            return 1
        print(f"OK: fixture and metadata match ({len(model_bytes)} bytes)")
        return 0

    model_path.parent.mkdir(parents=True, exist_ok=True)
    metadata_path.parent.mkdir(parents=True, exist_ok=True)
    model_path.write_bytes(model_bytes)
    metadata_path.write_text(metadata_text, encoding="utf-8")
    print(f"wrote {model_path} ({len(model_bytes)} bytes)")
    print(f"wrote {metadata_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
