#!/usr/bin/env python3
"""Export the tiny deterministic ChronosLOB-style ONNX fixture for Asterion.

This script intentionally lives in Asterion rather than ChronosLOB. The local
ChronosLOB checkout has model definitions and feature documentation, but no
existing ONNX export path. The fixture below is therefore model-plumbing
evidence only: a small fixed-shape numeric feature model with ChronosLOB-style
snapshot-feature semantics, not a trained forecasting model.

The script requires the optional ``onnx`` Python package only when regenerating
the checked-in artefact. Asterion's default build/test path does not import
``onnx`` or ONNX Runtime.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
from typing import Any

IR_VERSION = 8
OPSET_VERSION = 13
PRODUCER_NAME = "asterion"
PRODUCER_VERSION = "chronoslob-bridge-2026-05-31"

MODEL_NAME = "chronoslob_tiny_fixture"
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

# Linear scoring head encoded as Gemm(features, weights) + bias. The constants
# are deliberately small and deterministic; they are not fitted to market data.
REFERENCE_WEIGHTS = [0.1, -0.0005, 0.25, 0.0001]
REFERENCE_BIAS = 0.05
EXPECTED_TEST_INPUT = [2.0, 1000.0, 0.5, 400.0]
EXPECTED_TEST_OUTPUT = [
    sum(weight * value for weight, value in zip(REFERENCE_WEIGHTS, EXPECTED_TEST_INPUT))
    + REFERENCE_BIAS
]

DEFAULT_MODEL_OUTPUT = pathlib.Path("data/models/chronoslob_tiny_fixture.onnx")
DEFAULT_METADATA_OUTPUT = pathlib.Path("data/models/chronoslob_tiny_fixture.metadata.json")


def _candidate_chronoslob_roots(root: pathlib.Path) -> list[pathlib.Path]:
    parent = root.parent
    return [
        parent / "chronos-lob",
        parent / "ChronosLOB" / "chronos-lob",
        parent / "ChronosLOB",
        parent / "chronoslob",
    ]


def _resolve_chronoslob(root: pathlib.Path) -> pathlib.Path | None:
    for candidate in _candidate_chronoslob_roots(root):
        if (candidate / "chronoslob").is_dir():
            return candidate
    return None


def _git_value(repo: pathlib.Path, *args: str) -> str:
    try:
        completed = subprocess.run(
            ["git", *args],
            cwd=repo,
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
    except (OSError, subprocess.CalledProcessError):
        return ""
    return completed.stdout.strip()


def _source_status(root: pathlib.Path) -> dict[str, Any]:
    chronos = _resolve_chronoslob(root)
    if chronos is None:
        return {
            "source_repo_path": "",
            "source_commit": "",
            "source_dirty": False,
            "source_notes": "ChronosLOB checkout was not found; fixture generated from the documented Asterion-side contract.",
        }

    commit = _git_value(chronos, "rev-parse", "HEAD")
    status = _git_value(chronos, "status", "--short")
    return {
        "source_repo_path": chronos.relative_to(root.parent).as_posix(),
        "source_commit": commit,
        "source_dirty": bool(status),
        "source_notes": (
            "ChronosLOB inspected locally for model and feature conventions. "
            "No ONNX export hook was present, so this Asterion-side deterministic "
            "fixture preserves only a small fixed-shape feature/scoring contract."
        ),
    }


def build_model_bytes() -> bytes:
    try:
        import onnx
        from onnx import TensorProto, helper
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
        "chronoslob_tiny_fixture_graph",
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
        "claim": "research-model-to-systems integration fixture",
    }.items():
        prop = model.metadata_props.add()
        prop.key = key
        prop.value = value
    onnx.checker.check_model(model)
    return model.SerializeToString()


def build_metadata(root: pathlib.Path, export_command: str) -> dict[str, Any]:
    metadata: dict[str, Any] = {
        "model_name": MODEL_NAME,
        "export_command": export_command,
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
            "Single deterministic plumbing score from a fixed linear head over "
            "the Asterion L2 feature buffer."
        ),
        "claim_boundary": "Research-model-to-systems integration using a tiny ChronosLOB-style ONNX artefact.",
        "claim_limitations": [
            "No predictive quality claim.",
            "No trading profitability claim.",
            "Not live trading infrastructure.",
            "Not production model-serving infrastructure.",
            "Not production-HFT infrastructure.",
            "Not a portable latency guarantee.",
        ],
    }
    metadata.update(_source_status(root))
    return metadata


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=pathlib.Path, default=DEFAULT_MODEL_OUTPUT)
    parser.add_argument("--metadata-output", type=pathlib.Path, default=DEFAULT_METADATA_OUTPUT)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args(argv)

    root = pathlib.Path(__file__).resolve().parents[1]
    export_command = (
        "python tools/export_chronoslob_tiny_onnx.py "
        "--output data/models/chronoslob_tiny_fixture.onnx "
        "--metadata-output data/models/chronoslob_tiny_fixture.metadata.json"
    )
    model_bytes = build_model_bytes()
    metadata = build_metadata(root, export_command)
    metadata_text = json.dumps(metadata, indent=2, sort_keys=True) + "\n"

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
