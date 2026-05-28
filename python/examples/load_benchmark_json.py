#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

import asterion


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_INPUT = ROOT / "data" / "samples" / "sample_benchmark_schema.json"


def main() -> None:
    parser = argparse.ArgumentParser(description="Load and summarise Asterion benchmark JSON.")
    parser.add_argument("input", nargs="?", type=Path, default=DEFAULT_INPUT)
    args = parser.parse_args()

    summary = asterion.summarise_benchmark_json(args.input)
    print(f"schema_version={summary['schema_version']}")
    print(f"benchmark_count={summary['benchmark_count']}")
    for row in summary["rows"]:
        print(f"{row['name']}: iterations={row['iterations']} avg_ns={row['avg_ns']}")


if __name__ == "__main__":
    main()
