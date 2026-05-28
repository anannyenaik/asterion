#!/usr/bin/env python3
"""Inspect Asterion replay output, latency budgets and benchmark JSON.

Commands are split into two groups:

* Offline commands (``benchmark-summary``, ``benchmark-compare``,
  ``latency-budget``) read JSON files only and need no compiled extension.
* Replay commands (``replay-checksums``, ``diagnostics``, ``per-symbol``) import
  the ``asterion`` bindings lazily and therefore require the built C++ project on
  ``PYTHONPATH``.

Every command supports a readable text mode (default) and ``--json``.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from pathlib import Path
from typing import Any

_ROOT = Path(__file__).resolve().parents[1]


def _load_regression() -> Any:
    """Load the pure-stdlib regression module directly, without the package init.

    This keeps the offline commands usable even when the native extension is not
    built (importing the ``asterion`` package eagerly imports ``_native``).
    """
    module_path = _ROOT / "python" / "asterion" / "regression.py"
    spec = importlib.util.spec_from_file_location("asterion_regression", module_path)
    if spec is None or spec.loader is None:  # pragma: no cover - defensive.
        raise ImportError(f"unable to load regression module from {module_path}")
    module = importlib.util.module_from_spec(spec)
    # Register before exec so dataclass annotation resolution can find the module.
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


regression = _load_regression()


def _emit(payload: dict[str, Any], text: str, as_json: bool) -> None:
    if as_json:
        print(json.dumps(payload, indent=2, sort_keys=True))
    else:
        print(text)


# --------------------------------------------------------------------------- #
# Offline commands
# --------------------------------------------------------------------------- #


def cmd_benchmark_summary(args: argparse.Namespace) -> int:
    summary = regression.summarise_benchmarks(args.input)
    _emit(
        regression.benchmark_summary_to_dict(summary),
        regression.format_benchmark_summary_text(summary),
        args.json,
    )
    return 0


def cmd_benchmark_compare(args: argparse.Namespace) -> int:
    comparison = regression.compare_benchmarks(
        args.baseline,
        args.current,
        threshold_pct=args.threshold_pct,
        metric=args.metric,
    )
    _emit(
        regression.comparison_to_dict(comparison),
        regression.format_comparison_text(comparison),
        args.json,
    )
    if args.fail_on_regression and comparison.has_regressions:
        return 1
    return 0


def cmd_latency_budget(args: argparse.Namespace) -> int:
    summary = regression.summarise_latency_budget(args.input)
    _emit(
        regression.latency_budget_summary_to_dict(summary),
        regression.format_latency_budget_text(summary),
        args.json,
    )
    if args.fail_on_exceeded and summary.exceeded_count > 0:
        return 1
    return 0


# --------------------------------------------------------------------------- #
# Replay commands (require the built extension)
# --------------------------------------------------------------------------- #


def _run_replay(args: argparse.Namespace) -> Any:
    import asterion  # noqa: PLC0415 - imported lazily so offline commands stay usable.

    return asterion.run_replay(Path(args.input), symbol_id=args.symbol, format=args.format)


def cmd_replay_checksums(args: argparse.Namespace) -> int:
    result = _run_replay(args)
    payload = {
        "input": str(args.input),
        "symbol_id": args.symbol,
        "events_processed": result.events_processed,
        "sequence_valid": result.sequence_valid,
        "event_log_checksum": result.event_log_checksum,
        "final_book_checksum": result.final_book_checksum,
        "execution_report_checksum": result.execution_report_checksum,
        "diagnostics_checksum": result.diagnostics_checksum,
        "error": result.error,
    }
    text = "\n".join(f"{key}={value}" for key, value in payload.items())
    _emit(payload, text, args.json)
    return 0


def cmd_diagnostics(args: argparse.Namespace) -> int:
    import asterion  # noqa: PLC0415

    result = _run_replay(args)
    diagnostics = [
        {
            "event_index": diagnostic.event_index,
            "sequence_number": diagnostic.sequence_number,
            "symbol_id": diagnostic.symbol_id,
            "severity": asterion.diagnostic_severity_to_string(diagnostic.severity),
            "reason": diagnostic.reason,
        }
        for diagnostic in result.diagnostics
    ]
    payload = {
        "input": str(args.input),
        "diagnostic_count": len(diagnostics),
        "diagnostic_error_count": result.diagnostic_error_count,
        "diagnostic_warning_count": result.diagnostic_warning_count,
        "diagnostics_checksum": result.diagnostics_checksum,
        "diagnostics": diagnostics,
    }
    lines = [
        f"diagnostic_count={payload['diagnostic_count']}",
        f"diagnostic_error_count={payload['diagnostic_error_count']}",
        f"diagnostic_warning_count={payload['diagnostic_warning_count']}",
        f"diagnostics_checksum={payload['diagnostics_checksum']}",
    ]
    for diagnostic in diagnostics:
        lines.append(
            f"diagnostic event_index={diagnostic['event_index']} "
            f"sequence={diagnostic['sequence_number']} "
            f"severity={diagnostic['severity']} reason={diagnostic['reason']}"
        )
    _emit(payload, "\n".join(lines), args.json)
    return 0


def cmd_per_symbol(args: argparse.Namespace) -> int:
    import asterion  # noqa: PLC0415

    summary = asterion.aggregate_by_symbol(Path(args.input), format=args.format)
    symbols = [
        {
            "symbol_id": symbol.symbol_id,
            "event_count": symbol.event_count,
            "first_sequence": symbol.first_sequence,
            "last_sequence": symbol.last_sequence,
            "sequence_valid": symbol.sequence_valid,
            "final_book_checksum": symbol.final_book_checksum,
            "execution_report_checksum": symbol.execution_report_checksum,
            "diagnostics_checksum": symbol.diagnostics_checksum,
            "diagnostic_error_count": symbol.diagnostic_error_count,
        }
        for symbol in summary.symbols
    ]
    payload = {
        "input": str(args.input),
        "total_events": summary.total_events,
        "symbol_count": summary.symbol_count,
        "aggregate_checksum": summary.aggregate_checksum,
        "symbols": symbols,
    }
    lines = [
        f"total_events={summary.total_events}",
        f"symbol_count={summary.symbol_count}",
        f"aggregate_checksum={summary.aggregate_checksum}",
    ]
    for symbol in symbols:
        lines.append(
            f"symbol={symbol['symbol_id']} events={symbol['event_count']} "
            f"final_book_checksum={symbol['final_book_checksum']} "
            f"sequence_valid={symbol['sequence_valid']}"
        )
    _emit(payload, "\n".join(lines), args.json)
    return 0


# --------------------------------------------------------------------------- #
# Argument parsing
# --------------------------------------------------------------------------- #


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Inspect Asterion replay and benchmark artifacts.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    def add_replay_args(subparser: argparse.ArgumentParser) -> None:
        subparser.add_argument("--input", required=True, type=Path)
        subparser.add_argument("--symbol", type=int, default=1)
        subparser.add_argument("--format", default="auto", choices=["auto", "csv", "binary"])
        subparser.add_argument("--json", action="store_true")

    replay = subparsers.add_parser("replay-checksums", help="Print deterministic replay checksums.")
    add_replay_args(replay)
    replay.set_defaults(func=cmd_replay_checksums)

    diagnostics = subparsers.add_parser("diagnostics", help="Summarise replay diagnostics.")
    add_replay_args(diagnostics)
    diagnostics.set_defaults(func=cmd_diagnostics)

    per_symbol = subparsers.add_parser("per-symbol", help="Aggregate per-symbol replay summaries.")
    per_symbol.add_argument("--input", required=True, type=Path)
    per_symbol.add_argument("--format", default="auto", choices=["auto", "csv", "binary"])
    per_symbol.add_argument("--json", action="store_true")
    per_symbol.set_defaults(func=cmd_per_symbol)

    bench_summary = subparsers.add_parser(
        "benchmark-summary", help="Summarise a benchmark JSON file."
    )
    bench_summary.add_argument("--input", required=True, type=Path)
    bench_summary.add_argument("--json", action="store_true")
    bench_summary.set_defaults(func=cmd_benchmark_summary)

    bench_compare = subparsers.add_parser(
        "benchmark-compare", help="Compare two benchmark JSON files."
    )
    bench_compare.add_argument("--baseline", required=True, type=Path)
    bench_compare.add_argument("--current", required=True, type=Path)
    bench_compare.add_argument("--threshold-pct", type=float, default=10.0)
    bench_compare.add_argument(
        "--metric", default="avg_ns", choices=["avg_ns", "total_ns", "iterations"]
    )
    bench_compare.add_argument("--json", action="store_true")
    bench_compare.add_argument("--fail-on-regression", action="store_true")
    bench_compare.set_defaults(func=cmd_benchmark_compare)

    latency = subparsers.add_parser(
        "latency-budget", help="Summarise a latency-budget JSON file."
    )
    latency.add_argument("--input", required=True, type=Path)
    latency.add_argument("--json", action="store_true")
    latency.add_argument("--fail-on-exceeded", action="store_true")
    latency.set_defaults(func=cmd_latency_budget)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return int(args.func(args))


if __name__ == "__main__":
    sys.exit(main())
