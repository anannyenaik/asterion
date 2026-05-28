#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

import asterion


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_INPUT = ROOT / "data" / "samples" / "sample_replay.csv"


def counts_for(path: Path) -> dict[str, int]:
    result = asterion.run_replay(path, symbol_id=1)
    return {
        "errors": int(result.diagnostic_error_count),
        "warnings": int(result.diagnostic_warning_count),
        "total": len(result.diagnostics),
    }


def print_bars(counts: dict[str, int]) -> None:
    for name, count in counts.items():
        bar = "#" * count
        print(f"{name:8} | {bar} {count}")


def maybe_write_png(counts: dict[str, int], output: Path | None) -> None:
    if output is None:
        return
    import matplotlib.pyplot as plt

    labels = list(counts)
    values = [counts[label] for label in labels]
    figure, axis = plt.subplots(figsize=(5, 3))
    axis.bar(labels, values, color=["#9b1c31", "#d97706", "#2563eb"])
    axis.set_ylabel("count")
    axis.set_title("Replay diagnostics")
    figure.tight_layout()
    figure.savefig(output)


def main() -> None:
    parser = argparse.ArgumentParser(description="Plot replay diagnostic counts.")
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--png", type=Path)
    args = parser.parse_args()

    counts = counts_for(args.input)
    print_bars(counts)
    maybe_write_png(counts, args.png)


if __name__ == "__main__":
    main()
