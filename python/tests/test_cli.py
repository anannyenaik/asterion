from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SAMPLES = ROOT / "data" / "samples"
CLI = ROOT / "scripts" / "asterion_inspect.py"


def _run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(CLI), *args],
        capture_output=True,
        text=True,
        check=False,
    )


def test_benchmark_summary_json() -> None:
    result = _run(
        "benchmark-summary",
        "--input",
        str(SAMPLES / "sample_benchmark_baseline.json"),
        "--json",
    )
    assert result.returncode == 0, result.stderr
    payload = json.loads(result.stdout)
    assert payload["schema_version"] == 1
    assert payload["benchmark_count"] == 4
    names = [row["name"] for row in payload["benchmarks"]]
    assert "add_order" in names


def test_benchmark_summary_text() -> None:
    result = _run(
        "benchmark-summary",
        "--input",
        str(SAMPLES / "sample_benchmark_baseline.json"),
    )
    assert result.returncode == 0, result.stderr
    assert "benchmark_count=4" in result.stdout


def test_benchmark_compare_json_reports_regression() -> None:
    result = _run(
        "benchmark-compare",
        "--baseline",
        str(SAMPLES / "sample_benchmark_baseline.json"),
        "--current",
        str(SAMPLES / "sample_benchmark_current.json"),
        "--json",
    )
    assert result.returncode == 0, result.stderr
    payload = json.loads(result.stdout)
    assert payload["regressions"] == ["cancel_order"]
    assert payload["new_benchmarks"] == ["l2_snapshot_generation"]
    assert payload["missing_benchmarks"] == ["replace_order"]
    assert payload["has_regressions"] is True


def test_benchmark_compare_does_not_fail_by_default() -> None:
    # Normal runs must not fail CI on performance variance.
    result = _run(
        "benchmark-compare",
        "--baseline",
        str(SAMPLES / "sample_benchmark_baseline.json"),
        "--current",
        str(SAMPLES / "sample_benchmark_current.json"),
    )
    assert result.returncode == 0, result.stderr


def test_benchmark_compare_fail_on_regression_flag() -> None:
    result = _run(
        "benchmark-compare",
        "--baseline",
        str(SAMPLES / "sample_benchmark_baseline.json"),
        "--current",
        str(SAMPLES / "sample_benchmark_current.json"),
        "--fail-on-regression",
    )
    assert result.returncode == 1


def test_latency_budget_summary_json() -> None:
    result = _run(
        "latency-budget",
        "--input",
        str(SAMPLES / "sample_latency_budget.json"),
        "--json",
    )
    assert result.returncode == 0, result.stderr
    payload = json.loads(result.stdout)
    assert payload["exceeded_count"] == 1
    assert payload["worst_offender"] == "risk"


def test_latency_budget_fail_on_exceeded_flag() -> None:
    result = _run(
        "latency-budget",
        "--input",
        str(SAMPLES / "sample_latency_budget.json"),
        "--fail-on-exceeded",
    )
    assert result.returncode == 1
