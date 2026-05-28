"""Pure-stdlib benchmark and latency-budget analysis helpers.

This module deliberately depends on the standard library only and never imports
the compiled ``_native`` extension, so it can be used (and unit-tested) without a
built C++ project. It powers the offline benchmark-regression comparison and the
JSON inspection commands.

Benchmark regression results are machine-dependent. Comparing two JSON files only
makes sense when both were produced on comparable hardware in comparable
conditions; see BENCHMARKS.md.
"""

from __future__ import annotations

import json
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Any

PathLike = str | os.PathLike[str]
JsonSource = PathLike | dict[str, Any]

DEFAULT_THRESHOLD_PCT = 10.0
DEFAULT_METRIC = "avg_ns"


def _load_json_object(source: JsonSource) -> dict[str, Any]:
    if isinstance(source, dict):
        return source
    with Path(source).open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError("benchmark JSON root must be an object")
    return data


# --------------------------------------------------------------------------- #
# Benchmark JSON loading and summary
# --------------------------------------------------------------------------- #


@dataclass(frozen=True)
class BenchmarkMetric:
    name: str
    iterations: int
    total_ns: int
    avg_ns: int

    def value(self, metric: str) -> int:
        if metric == "iterations":
            return self.iterations
        if metric == "total_ns":
            return self.total_ns
        if metric == "avg_ns":
            return self.avg_ns
        raise ValueError(f"unknown benchmark metric: {metric}")


def load_benchmark_metrics(source: JsonSource) -> dict[str, BenchmarkMetric]:
    data = _load_json_object(source)
    benchmarks = data.get("benchmarks", [])
    if not isinstance(benchmarks, list):
        raise ValueError("benchmark JSON 'benchmarks' field must be a list")

    metrics: dict[str, BenchmarkMetric] = {}
    for row in benchmarks:
        if not isinstance(row, dict):
            raise ValueError("benchmark row must be an object")
        name = str(row["name"])
        if name in metrics:
            raise ValueError(f"duplicate benchmark name: {name}")
        metrics[name] = BenchmarkMetric(
            name=name,
            iterations=int(row.get("iterations", 0)),
            total_ns=int(row.get("total_ns", 0)),
            avg_ns=int(row.get("avg_ns", 0)),
        )
    return metrics


@dataclass(frozen=True)
class BenchmarkSummary:
    schema_version: int
    environment: dict[str, Any]
    benchmark_count: int
    metrics: list[BenchmarkMetric]


def summarise_benchmarks(source: JsonSource) -> BenchmarkSummary:
    data = _load_json_object(source)
    metrics = list(load_benchmark_metrics(data).values())
    environment = data.get("environment", {})
    if not isinstance(environment, dict):
        environment = {}
    return BenchmarkSummary(
        schema_version=int(data.get("schema_version", 0)),
        environment=environment,
        benchmark_count=len(metrics),
        metrics=metrics,
    )


def benchmark_summary_to_dict(summary: BenchmarkSummary) -> dict[str, Any]:
    return {
        "schema_version": summary.schema_version,
        "environment": summary.environment,
        "benchmark_count": summary.benchmark_count,
        "benchmarks": [
            {
                "name": metric.name,
                "iterations": metric.iterations,
                "total_ns": metric.total_ns,
                "avg_ns": metric.avg_ns,
            }
            for metric in summary.metrics
        ],
    }


def format_benchmark_summary_text(summary: BenchmarkSummary) -> str:
    lines = [
        f"schema_version={summary.schema_version}",
        f"benchmark_count={summary.benchmark_count}",
    ]
    for metric in summary.metrics:
        lines.append(
            f"{metric.name}: iterations={metric.iterations} "
            f"total_ns={metric.total_ns} avg_ns={metric.avg_ns}"
        )
    return "\n".join(lines)


# --------------------------------------------------------------------------- #
# Benchmark regression comparison
# --------------------------------------------------------------------------- #


def _pct_change(baseline: int, current: int) -> float | None:
    if baseline == 0:
        return 0.0 if current == 0 else None
    return (current - baseline) / baseline * 100.0


@dataclass(frozen=True)
class BenchmarkDelta:
    name: str
    baseline_value: int
    current_value: int
    delta: int
    pct_change: float | None
    regressed: bool
    improved: bool


@dataclass(frozen=True)
class BenchmarkComparison:
    metric: str
    threshold_pct: float
    deltas: list[BenchmarkDelta]
    new_benchmarks: list[str]
    missing_benchmarks: list[str]

    @property
    def regressions(self) -> list[str]:
        return [delta.name for delta in self.deltas if delta.regressed]

    @property
    def improvements(self) -> list[str]:
        return [delta.name for delta in self.deltas if delta.improved]

    @property
    def has_regressions(self) -> bool:
        return any(delta.regressed for delta in self.deltas)


def compare_benchmarks(
    baseline: JsonSource,
    current: JsonSource,
    *,
    threshold_pct: float = DEFAULT_THRESHOLD_PCT,
    metric: str = DEFAULT_METRIC,
) -> BenchmarkComparison:
    if threshold_pct < 0:
        raise ValueError("threshold_pct must be non-negative")

    baseline_metrics = load_benchmark_metrics(baseline)
    current_metrics = load_benchmark_metrics(current)

    shared = sorted(set(baseline_metrics) & set(current_metrics))
    deltas: list[BenchmarkDelta] = []
    for name in shared:
        base_value = baseline_metrics[name].value(metric)
        cur_value = current_metrics[name].value(metric)
        pct = _pct_change(base_value, cur_value)
        if pct is None:
            regressed = cur_value > base_value
            improved = cur_value < base_value
        else:
            regressed = pct > threshold_pct
            improved = pct < -threshold_pct
        deltas.append(
            BenchmarkDelta(
                name=name,
                baseline_value=base_value,
                current_value=cur_value,
                delta=cur_value - base_value,
                pct_change=pct,
                regressed=regressed,
                improved=improved,
            )
        )

    new_benchmarks = sorted(set(current_metrics) - set(baseline_metrics))
    missing_benchmarks = sorted(set(baseline_metrics) - set(current_metrics))

    return BenchmarkComparison(
        metric=metric,
        threshold_pct=threshold_pct,
        deltas=deltas,
        new_benchmarks=new_benchmarks,
        missing_benchmarks=missing_benchmarks,
    )


def comparison_to_dict(comparison: BenchmarkComparison) -> dict[str, Any]:
    return {
        "metric": comparison.metric,
        "threshold_pct": comparison.threshold_pct,
        "deltas": [
            {
                "name": delta.name,
                "baseline_value": delta.baseline_value,
                "current_value": delta.current_value,
                "delta": delta.delta,
                "pct_change": delta.pct_change,
                "regressed": delta.regressed,
                "improved": delta.improved,
            }
            for delta in comparison.deltas
        ],
        "new_benchmarks": comparison.new_benchmarks,
        "missing_benchmarks": comparison.missing_benchmarks,
        "regressions": comparison.regressions,
        "improvements": comparison.improvements,
        "has_regressions": comparison.has_regressions,
    }


def _format_pct(pct: float | None) -> str:
    if pct is None:
        return "n/a"
    return f"{pct:+.2f}%"


def format_comparison_text(comparison: BenchmarkComparison) -> str:
    lines = [
        f"metric={comparison.metric} threshold_pct={comparison.threshold_pct:g}",
    ]
    for delta in comparison.deltas:
        flag = "REGRESSION" if delta.regressed else ("improved" if delta.improved else "ok")
        lines.append(
            f"{delta.name}: {delta.baseline_value} -> {delta.current_value} "
            f"({_format_pct(delta.pct_change)}) [{flag}]"
        )
    if comparison.new_benchmarks:
        lines.append("new: " + ", ".join(comparison.new_benchmarks))
    if comparison.missing_benchmarks:
        lines.append("missing: " + ", ".join(comparison.missing_benchmarks))
    lines.append(
        f"regressions={len(comparison.regressions)} "
        f"improvements={len(comparison.improvements)} "
        f"has_regressions={str(comparison.has_regressions).lower()}"
    )
    return "\n".join(lines)


# --------------------------------------------------------------------------- #
# Latency-budget JSON summary
# --------------------------------------------------------------------------- #


@dataclass(frozen=True)
class LatencyBudgetStageSummary:
    stage: str
    has_budget: bool
    budget_ns: int
    sample_count: int
    worst_observed_ns: int
    total_observed_ns: int
    worst_utilization_ppm: int
    exceeded: bool


@dataclass(frozen=True)
class LatencyBudgetSummary:
    schema_version: int
    config_checksum: int
    exceeded_count: int
    worst_offender: str | None
    stages: list[LatencyBudgetStageSummary]


def summarise_latency_budget(source: JsonSource) -> LatencyBudgetSummary:
    data = _load_json_object(source)
    stages_raw = data.get("stages", [])
    if not isinstance(stages_raw, list):
        raise ValueError("latency-budget JSON 'stages' field must be a list")

    stages: list[LatencyBudgetStageSummary] = []
    for row in stages_raw:
        if not isinstance(row, dict):
            raise ValueError("latency-budget stage must be an object")
        stages.append(
            LatencyBudgetStageSummary(
                stage=str(row["stage"]),
                has_budget=bool(row.get("has_budget", False)),
                budget_ns=int(row.get("budget_ns", 0)),
                sample_count=int(row.get("sample_count", 0)),
                worst_observed_ns=int(row.get("worst_observed_ns", 0)),
                total_observed_ns=int(row.get("total_observed_ns", 0)),
                worst_utilization_ppm=int(row.get("worst_utilization_ppm", 0)),
                exceeded=bool(row.get("exceeded", False)),
            )
        )

    worst = data.get("worst_offender")
    return LatencyBudgetSummary(
        schema_version=int(data.get("schema_version", 0)),
        config_checksum=int(data.get("config_checksum", 0)),
        exceeded_count=int(data.get("exceeded_count", 0)),
        worst_offender=None if worst is None else str(worst),
        stages=stages,
    )


def latency_budget_summary_to_dict(summary: LatencyBudgetSummary) -> dict[str, Any]:
    return {
        "schema_version": summary.schema_version,
        "config_checksum": summary.config_checksum,
        "exceeded_count": summary.exceeded_count,
        "worst_offender": summary.worst_offender,
        "stages": [
            {
                "stage": stage.stage,
                "has_budget": stage.has_budget,
                "budget_ns": stage.budget_ns,
                "sample_count": stage.sample_count,
                "worst_observed_ns": stage.worst_observed_ns,
                "total_observed_ns": stage.total_observed_ns,
                "worst_utilization_ppm": stage.worst_utilization_ppm,
                "exceeded": stage.exceeded,
            }
            for stage in summary.stages
        ],
    }


def format_latency_budget_text(summary: LatencyBudgetSummary) -> str:
    lines = [
        f"schema_version={summary.schema_version}",
        f"config_checksum={summary.config_checksum}",
        f"exceeded_count={summary.exceeded_count}",
        f"worst_offender={summary.worst_offender if summary.worst_offender else 'none'}",
    ]
    for stage in summary.stages:
        lines.append(
            f"{stage.stage}: samples={stage.sample_count} "
            f"worst_ns={stage.worst_observed_ns} budget_ns={stage.budget_ns} "
            f"has_budget={str(stage.has_budget).lower()} "
            f"utilization_ppm={stage.worst_utilization_ppm} "
            f"exceeded={str(stage.exceeded).lower()}"
        )
    return "\n".join(lines)
