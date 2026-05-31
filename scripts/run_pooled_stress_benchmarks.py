#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "scripts" / "generate_synthetic_events.py"
STANDARD_BENCHMARK = "hot_path_binary_replay_l3_l2_strategy_risk"
POOLED_BENCHMARK = "hot_path_binary_replay_pooled_l3_l2_strategy_risk"


@dataclass(frozen=True)
class DatasetSpec:
    name: str
    mode: str
    events: int
    seed: int
    price_range: int = 5
    burst_size: int = 8
    symbols: int = 1
    benchmark_compatible: bool = True


DATASETS = (
    DatasetSpec("baseline_balanced", "balanced", 4_000, 20260531),
    DatasetSpec("high_cancellation_rate", "high-cancel", 4_000, 20260532),
    DatasetSpec("replace_heavy", "replace-heavy", 4_000, 20260533),
    DatasetSpec("deep_book", "deep-book", 5_000, 20260534, price_range=20),
    DatasetSpec("wide_price_range", "wide-price-range", 5_000, 20260535, price_range=50),
    DatasetSpec("bursty_flow", "bursty", 4_000, 20260536, burst_size=4),
    DatasetSpec("long_running_same_symbol", "long-same-symbol", 8_000, 20260537),
    DatasetSpec("adversarial_lifecycle", "adversarial-lifecycle", 4_000, 20260538),
    DatasetSpec(
        "multi_symbol_style",
        "multi-symbol",
        4_000,
        20260539,
        symbols=4,
        benchmark_compatible=False,
    ),
)


def benchmark_path(build_dir: Path) -> Path:
    candidates = [build_dir / "asterion_benchmarks"]
    if os.name == "nt":
        candidates.insert(0, build_dir / "asterion_benchmarks.exe")
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise SystemExit(
        f"missing benchmark executable under {build_dir}; build target asterion_benchmarks first"
    )


def run_command(args: list[str], cwd: Path) -> None:
    print("+ " + " ".join(args))
    subprocess.run(args, cwd=cwd, check=True)


def generate_dataset(spec: DatasetSpec, output_dir: Path, python_exe: str) -> Path:
    output = output_dir / f"{spec.name}.bin"
    args = [
        python_exe,
        str(GENERATOR),
        "--mode",
        spec.mode,
        "--events",
        str(spec.events),
        "--seed",
        str(spec.seed),
        "--price-range",
        str(spec.price_range),
        "--burst-size",
        str(spec.burst_size),
        "--symbols",
        str(spec.symbols),
        "--output",
        str(output),
        "--format",
        "binary",
    ]
    run_command(args, ROOT)
    return output


def find_result(payload: dict[str, Any], name: str) -> dict[str, Any]:
    for row in payload.get("benchmarks", []):
        if row.get("name") == name:
            return row
    raise RuntimeError(f"benchmark result missing from JSON: {name}")


def run_benchmark(
    executable: Path,
    dataset: Path,
    result_json: Path,
    hot_path_iterations: int,
    warmup_iterations: int,
) -> dict[str, Any]:
    args = [
        str(executable),
        "--dataset",
        str(dataset),
        "--hot-path-iterations",
        str(hot_path_iterations),
        "--warmup-iterations",
        str(warmup_iterations),
        "--json",
        str(result_json),
        "--no-text",
    ]
    run_command(args, ROOT)
    with result_json.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def comparison_row(spec: DatasetSpec, dataset: Path, payload: dict[str, Any]) -> dict[str, Any]:
    standard = find_result(payload, STANDARD_BENCHMARK)
    pooled = find_result(payload, POOLED_BENCHMARK)
    measured_iterations = int(pooled.get("measured_iterations") or pooled["iterations"])
    warmup_iterations = int(pooled.get("warmup_iterations", 0))
    measured_events = int(pooled["event_count"])
    dataset_events = measured_events // measured_iterations if measured_iterations else spec.events

    return {
        "dataset": spec.name,
        "mode": spec.mode,
        "path": str(dataset),
        "dataset_events": dataset_events,
        "warmup_iterations": warmup_iterations,
        "measured_iterations": measured_iterations,
        "measured_events": measured_events,
        "standard": {
            "allocations": standard["allocations"],
            "bytes_allocated": standard["bytes_allocated"],
            "avg_ns": standard["avg_ns"],
            "p50_ns": standard["p50_ns"],
            "p95_ns": standard["p95_ns"],
            "p99_ns": standard["p99_ns"],
            "p999_ns": standard["p999_ns"],
            "max_ns": standard["max_ns"],
            "guard": standard["guard"],
        },
        "pooled": {
            "allocations": pooled["allocations"],
            "bytes_allocated": pooled["bytes_allocated"],
            "avg_ns": pooled["avg_ns"],
            "p50_ns": pooled["p50_ns"],
            "p95_ns": pooled["p95_ns"],
            "p99_ns": pooled["p99_ns"],
            "p999_ns": pooled["p999_ns"],
            "max_ns": pooled["max_ns"],
            "guard": pooled["guard"],
        },
        "guard_match": standard["guard"] == pooled["guard"],
        "pooled_steady_state_allocation_free": pooled["allocations"] == 0
        and pooled["bytes_allocated"] == 0,
    }


def print_summary(rows: list[dict[str, Any]], skipped: list[dict[str, Any]]) -> None:
    print("\npooled stress benchmark summary")
    for row in rows:
        print(
            f"{row['dataset']}: events={row['dataset_events']} "
            f"standard_allocs={row['standard']['allocations']} "
            f"pooled_allocs={row['pooled']['allocations']} "
            f"pooled_bytes={row['pooled']['bytes_allocated']} "
            f"guard_match={row['guard_match']}"
        )
    for row in skipped:
        print(f"{row['dataset']}: generated only ({row['reason']})")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate pooled-order-book stress corpora and benchmark standard vs pooled replay."
    )
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--output-dir", default="data/generated/pooled_order_book_stress")
    parser.add_argument("--results-dir", default="benchmarks/results/pooled_order_book_stress")
    parser.add_argument("--hot-path-iterations", type=int, default=100)
    parser.add_argument("--warmup-iterations", type=int, default=5)
    parser.add_argument("--python", default=sys.executable)
    parser.add_argument("--generate-only", action="store_true")
    parser.add_argument("--skip-generate", action="store_true")
    args = parser.parse_args()

    build_dir = (ROOT / args.build_dir).resolve()
    output_dir = (ROOT / args.output_dir).resolve()
    results_dir = (ROOT / args.results_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    results_dir.mkdir(parents=True, exist_ok=True)

    executable = None if args.generate_only else benchmark_path(build_dir)
    rows: list[dict[str, Any]] = []
    skipped: list[dict[str, Any]] = []

    for spec in DATASETS:
        dataset = output_dir / f"{spec.name}.bin"
        if not args.skip_generate:
            dataset = generate_dataset(spec, output_dir, args.python)
        if not spec.benchmark_compatible:
            skipped.append(
                {
                    "dataset": spec.name,
                    "mode": spec.mode,
                    "path": str(dataset),
                    "dataset_events": spec.events,
                    "reason": "single-symbol hot-path benchmark is not a multi-symbol router",
                }
            )
            continue
        if args.generate_only:
            continue
        result_json = results_dir / f"{spec.name}.benchmark.json"
        payload = run_benchmark(
            executable,
            dataset,
            result_json,
            args.hot_path_iterations,
            args.warmup_iterations,
        )
        rows.append(comparison_row(spec, dataset, payload))

    summary = {
        "schema_version": 1,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "note": "These are representative local measurements on this machine, not portable performance claims.",
        "hot_path_iterations": args.hot_path_iterations,
        "warmup_iterations": args.warmup_iterations,
        "datasets": rows,
        "skipped": skipped,
    }
    summary_path = results_dir / "pooled_order_book_stress_summary.json"
    with summary_path.open("w", encoding="utf-8") as handle:
        json.dump(summary, handle, indent=2)
        handle.write("\n")

    print_summary(rows, skipped)
    print(f"summary_json={summary_path}")


if __name__ == "__main__":
    main()
