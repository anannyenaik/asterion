from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any

from .analysis.latency import BenchmarkRow, parse_benchmark_line

PathLike = str | os.PathLike[str]


def load_benchmark_json(path: PathLike) -> dict[str, Any]:
    with Path(path).open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError("benchmark JSON root must be an object")
    return data


def summarise_benchmark_json(path: PathLike) -> dict[str, Any]:
    data = load_benchmark_json(path)
    benchmarks = data.get("benchmarks", [])
    if not isinstance(benchmarks, list):
        raise ValueError("benchmark JSON 'benchmarks' field must be a list")

    rows: list[dict[str, int | str]] = []
    for row in benchmarks:
        if not isinstance(row, dict):
            raise ValueError("benchmark row must be an object")
        rows.append(
            {
                "name": str(row["name"]),
                "iterations": int(row["iterations"]),
                "total_ns": int(row["total_ns"]),
                "avg_ns": int(row["avg_ns"]),
            }
        )

    return {
        "schema_version": int(data.get("schema_version", 0)),
        "environment": data.get("environment", {}),
        "benchmark_count": len(rows),
        "rows": rows,
    }


def summarise_benchmark_text(path: PathLike) -> list[BenchmarkRow]:
    rows: list[BenchmarkRow] = []
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        if line.strip():
            rows.append(parse_benchmark_line(line))
    return rows
