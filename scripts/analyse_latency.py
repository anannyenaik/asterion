#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

from asterion.analysis.latency import parse_benchmark_line


def main() -> None:
    parser = argparse.ArgumentParser(description="Summarise Asterion benchmark output.")
    parser.add_argument("input", type=Path)
    args = parser.parse_args()

    for line in args.input.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        row = parse_benchmark_line(line)
        print(f"{row.name}: iterations={row.iterations} avg_ns={row.avg_ns}")


if __name__ == "__main__":
    main()
