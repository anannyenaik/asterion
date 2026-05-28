from __future__ import annotations

import importlib.util
import json
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SAMPLES = ROOT / "data" / "samples"


def _load_regression():
    module_path = ROOT / "python" / "asterion" / "regression.py"
    spec = importlib.util.spec_from_file_location("asterion_regression", module_path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    # Register before exec so dataclass annotation resolution can find the module.
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


regression = _load_regression()


def _benchmark(name: str, avg_ns: int) -> dict:
    return {
        "name": name,
        "iterations": 1000,
        "total_ns": avg_ns * 1000,
        "avg_ns": avg_ns,
    }


def test_summarise_benchmarks_from_dict() -> None:
    data = {
        "schema_version": 1,
        "environment": {"note": "synthetic"},
        "benchmarks": [_benchmark("add_order", 100), _benchmark("cancel_order", 120)],
    }
    summary = regression.summarise_benchmarks(data)
    assert summary.schema_version == 1
    assert summary.benchmark_count == 2
    assert summary.metrics[0].name == "add_order"
    assert summary.metrics[0].avg_ns == 100

    as_dict = regression.benchmark_summary_to_dict(summary)
    # Must round-trip through JSON without custom encoders.
    json.loads(json.dumps(as_dict))


def test_compare_benchmarks_detects_regression_improvement_new_missing() -> None:
    baseline = {
        "benchmarks": [
            _benchmark("add_order", 100),
            _benchmark("cancel_order", 120),
            _benchmark("risk_check_only", 80),
            _benchmark("replace_order", 200),
        ]
    }
    current = {
        "benchmarks": [
            _benchmark("add_order", 105),
            _benchmark("cancel_order", 150),
            _benchmark("risk_check_only", 60),
            _benchmark("l2_snapshot_generation", 50),
        ]
    }

    comparison = regression.compare_benchmarks(baseline, current, threshold_pct=10.0)
    by_name = {delta.name: delta for delta in comparison.deltas}

    # add_order: +5% is within the 10% threshold.
    assert not by_name["add_order"].regressed
    assert not by_name["add_order"].improved
    assert by_name["add_order"].pct_change == pytest.approx(5.0)

    # cancel_order: +25% is a regression.
    assert by_name["cancel_order"].regressed
    assert by_name["cancel_order"].pct_change == pytest.approx(25.0)

    # risk_check_only: -25% is an improvement.
    assert by_name["risk_check_only"].improved
    assert by_name["risk_check_only"].pct_change == pytest.approx(-25.0)

    assert comparison.regressions == ["cancel_order"]
    assert comparison.improvements == ["risk_check_only"]
    assert comparison.new_benchmarks == ["l2_snapshot_generation"]
    assert comparison.missing_benchmarks == ["replace_order"]
    assert comparison.has_regressions is True

    payload = regression.comparison_to_dict(comparison)
    json.loads(json.dumps(payload))


def test_compare_benchmarks_threshold_is_configurable() -> None:
    baseline = {"benchmarks": [_benchmark("add_order", 100)]}
    current = {"benchmarks": [_benchmark("add_order", 130)]}

    strict = regression.compare_benchmarks(baseline, current, threshold_pct=10.0)
    assert strict.has_regressions

    lenient = regression.compare_benchmarks(baseline, current, threshold_pct=50.0)
    assert not lenient.has_regressions


def test_compare_benchmarks_handles_zero_baseline() -> None:
    baseline = {"benchmarks": [_benchmark("add_order", 0)]}
    current = {"benchmarks": [_benchmark("add_order", 5)]}
    comparison = regression.compare_benchmarks(baseline, current)
    delta = comparison.deltas[0]
    assert delta.pct_change is None
    assert delta.regressed is True
    json.loads(json.dumps(regression.comparison_to_dict(comparison)))


def test_compare_checked_in_benchmark_fixtures() -> None:
    comparison = regression.compare_benchmarks(
        SAMPLES / "sample_benchmark_baseline.json",
        SAMPLES / "sample_benchmark_current.json",
        threshold_pct=10.0,
    )
    assert comparison.regressions == ["cancel_order"]
    assert comparison.improvements == ["risk_check_only"]
    assert comparison.new_benchmarks == ["l2_snapshot_generation"]
    assert comparison.missing_benchmarks == ["replace_order"]


def test_summarise_latency_budget_fixture() -> None:
    summary = regression.summarise_latency_budget(SAMPLES / "sample_latency_budget.json")
    assert summary.schema_version == 1
    assert summary.exceeded_count == 1
    assert summary.worst_offender == "risk"
    stages = {stage.stage: stage for stage in summary.stages}
    assert stages["risk"].exceeded is True
    assert stages["risk"].budget_ns == 100
    assert stages["replay"].has_budget is False

    payload = regression.latency_budget_summary_to_dict(summary)
    json.loads(json.dumps(payload))


def test_text_formatters_return_strings() -> None:
    comparison = regression.compare_benchmarks(
        {"benchmarks": [_benchmark("add_order", 100)]},
        {"benchmarks": [_benchmark("add_order", 150)]},
    )
    text = regression.format_comparison_text(comparison)
    assert "add_order" in text
    assert "REGRESSION" in text

    budget = regression.summarise_latency_budget(SAMPLES / "sample_latency_budget.json")
    assert "risk" in regression.format_latency_budget_text(budget)
