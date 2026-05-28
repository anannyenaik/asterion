from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class BenchmarkRow:
    name: str
    iterations: int
    total_ns: int
    avg_ns: int


def parse_benchmark_line(line: str) -> BenchmarkRow:
    fields = line.strip().split(",")
    name = fields[0]
    values: dict[str, int] = {}
    for field in fields[1:]:
        key, value = field.split("=", 1)
        if key != "guard":
            values[key] = int(value)
    return BenchmarkRow(
        name=name,
        iterations=values["iterations"],
        total_ns=values["total_ns"],
        avg_ns=values["avg_ns"],
    )
