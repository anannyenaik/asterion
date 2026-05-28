#!/usr/bin/env python3
"""Inspect Asterion replay output, risk audit logs, latency budgets and benchmark JSON.

Commands are split into two groups:

* Offline commands (``benchmark-summary``, ``benchmark-compare``,
  ``latency-budget``, ``audit-summary``, ``audit-verify`` and
  ``rate-limit-mode``) read JSON/text files only and need no compiled extension.
* Native-backed commands (``replay-checksums``, ``diagnostics``, ``per-symbol`` and
  ``shared-fuzz`` and ``risk-exposure``) import the ``asterion`` bindings lazily
  and therefore require the built C++ project on ``PYTHONPATH``.

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


def cmd_benchmark_store(args: argparse.Namespace) -> int:
    stored = regression.store_benchmark(args.input, args.history_dir, name=args.name)
    payload = {"stored": str(stored)}
    _emit(payload, f"stored={stored}", args.json)
    return 0


def cmd_benchmark_trend(args: argparse.Namespace) -> int:
    if args.inputs:
        sources: list[Any] = [Path(item) for item in args.inputs]
    elif args.history_dir is not None:
        sources = sorted(Path(args.history_dir).glob(args.glob))
    else:
        print("provide --inputs or --history-dir", file=sys.stderr)
        return 1
    if not sources:
        print("no benchmark JSON files found", file=sys.stderr)
        return 1
    trend = regression.load_trend(sources, metric=args.metric)
    _emit(regression.trend_to_dict(trend), regression.format_trend_text(trend), args.json)
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


def _normalise_rate_limit_mode(value: str) -> str:
    token = value.strip().lower().replace("_", "-")
    aliases = {
        "fixed": "fixed-window",
        "fixed-window": "fixed-window",
        "sliding": "sliding-window",
        "sliding-window": "sliding-window",
    }
    if token not in aliases:
        raise ValueError(f"unknown rate-limit mode: {value}")
    return aliases[token]


def cmd_rate_limit_mode(args: argparse.Namespace) -> int:
    try:
        mode = _normalise_rate_limit_mode(args.mode)
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 1
    payload = {"mode": mode, "default": mode == "fixed-window"}
    _emit(payload, f"mode={mode}\ndefault={str(payload['default']).lower()}", args.json)
    return 0


def _parse_bool(value: str) -> bool:
    return value.lower() in {"1", "true", "yes"}


def _audit_format(path: Path, requested: str) -> str:
    if requested != "auto":
        return requested
    return "jsonl" if path.suffix == ".jsonl" else "text"


def _read_audit_entries(path: Path, requested_format: str) -> list[dict[str, Any]]:
    entries: list[dict[str, Any]] = []
    fmt = _audit_format(path, requested_format)
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        if fmt == "jsonl":
            entries.append(json.loads(line))
            continue
        fields: dict[str, Any] = {}
        for item in line.split():
            if "=" not in item:
                continue
            key, value = item.split("=", 1)
            if key in {
                "timestamp_ns",
                "client_order_id",
                "symbol_id",
                "limit_value",
                "observed_value",
                "checksum",
            }:
                fields[key] = int(value)
            elif key == "accepted":
                fields[key] = _parse_bool(value)
            else:
                fields[key] = value
        entries.append(fields)
    return entries


_FNV_OFFSET = 14695981039346656037
_FNV_PRIME = 1099511628211
_MASK64 = (1 << 64) - 1


def _fnv_append_byte(seed: int, byte: int) -> int:
    seed ^= byte & 0xFF
    seed = (seed * _FNV_PRIME) & _MASK64
    return seed


def _checksum_append_int(seed: int, value: int, byte_count: int) -> int:
    value &= (1 << (byte_count * 8)) - 1
    for index in range(byte_count):
        seed = _fnv_append_byte(seed, (value >> (index * 8)) & 0xFF)
    return seed


def _checksum_append_string(seed: int, value: str) -> int:
    data = value.encode("utf-8")
    seed = _checksum_append_int(seed, len(data), 8)
    for byte in data:
        seed = _fnv_append_byte(seed, byte)
    return seed


_SIDE_VALUES = {"None": 0, "Buy": 1, "Sell": 2}
_REJECT_REASON_VALUES = {
    "None": 0,
    "InvalidQuantity": 1,
    "InvalidPrice": 2,
    "DuplicateClientOrderId": 3,
    "UnknownOrder": 4,
    "KillSwitch": 5,
    "MaxOrderQuantity": 6,
    "MaxNotional": 7,
    "MaxPosition": 8,
    "MaxGrossExposure": 9,
    "PriceBand": 10,
    "StaleMarketData": 11,
    "Unsupported": 12,
    "InternalError": 13,
    "MaxOpenOrderQuantity": 14,
    "MessageRateLimit": 15,
    "SelfTradePrevention": 16,
    "Disconnected": 17,
}


def _append_audit_entry_checksum(seed: int, entry: dict[str, Any]) -> int:
    seed = _checksum_append_int(seed, int(entry["timestamp_ns"]), 8)
    seed = _checksum_append_int(seed, int(entry["client_order_id"]), 8)
    seed = _checksum_append_int(seed, int(entry["symbol_id"]), 4)
    seed = _checksum_append_int(seed, _SIDE_VALUES[str(entry["side"])], 1)
    seed = _checksum_append_int(seed, 1 if bool(entry["accepted"]) else 0, 1)
    seed = _checksum_append_int(seed, _REJECT_REASON_VALUES[str(entry["reject_reason"])], 1)
    seed = _checksum_append_string(seed, str(entry["check_name"]))
    seed = _checksum_append_int(seed, int(entry["limit_value"]), 8)
    seed = _checksum_append_int(seed, int(entry["observed_value"]), 8)
    return seed


def _verify_audit_logs(paths: list[Path], requested_format: str) -> dict[str, Any]:
    checksum = _FNV_OFFSET
    entries_checked = 0
    for path in paths:
        try:
            entries = _read_audit_entries(path, requested_format)
        except (OSError, json.JSONDecodeError, ValueError) as exc:
            return {
                "valid": False,
                "files_checked": paths.index(path) + 1,
                "entries_checked": entries_checked,
                "final_checksum": checksum,
                "error": f"{path}: {exc}",
            }
        for index, entry in enumerate(entries, start=1):
            if "checksum" not in entry:
                return {
                    "valid": False,
                    "files_checked": paths.index(path) + 1,
                    "entries_checked": entries_checked,
                    "final_checksum": checksum,
                    "error": f"{path}:{index}: missing checksum",
                }
            try:
                checksum = _append_audit_entry_checksum(checksum, entry)
            except (KeyError, ValueError) as exc:
                return {
                    "valid": False,
                    "files_checked": paths.index(path) + 1,
                    "entries_checked": entries_checked,
                    "final_checksum": checksum,
                    "error": f"{path}:{index}: malformed audit fields: {exc}",
                }
            entries_checked += 1
            if checksum != int(entry["checksum"]):
                return {
                    "valid": False,
                    "files_checked": paths.index(path) + 1,
                    "entries_checked": entries_checked,
                    "final_checksum": checksum,
                    "error": (
                        f"{path}:{index}: checksum mismatch, expected {checksum}, "
                        f"received {entry['checksum']}"
                    ),
                }
    return {
        "valid": True,
        "files_checked": len(paths),
        "entries_checked": entries_checked,
        "final_checksum": checksum,
        "error": "",
    }


def cmd_audit_summary(args: argparse.Namespace) -> int:
    entries = _read_audit_entries(args.input, args.format)
    check_counts: dict[str, int] = {}
    accepted_count = 0
    for entry in entries:
        check_name = str(entry.get("check_name", "unknown"))
        check_counts[check_name] = check_counts.get(check_name, 0) + 1
        if bool(entry.get("accepted", False)):
            accepted_count += 1
    payload = {
        "input": str(args.input),
        "entry_count": len(entries),
        "accepted_count": accepted_count,
        "rejected_count": len(entries) - accepted_count,
        "last_checksum": entries[-1].get("checksum", 0) if entries else 0,
        "check_counts": check_counts,
    }
    lines = [
        f"entry_count={payload['entry_count']}",
        f"accepted_count={payload['accepted_count']}",
        f"rejected_count={payload['rejected_count']}",
        f"last_checksum={payload['last_checksum']}",
    ]
    for name, count in sorted(check_counts.items()):
        lines.append(f"check={name} count={count}")
    _emit(payload, "\n".join(lines), args.json)
    return 0


def cmd_audit_verify(args: argparse.Namespace) -> int:
    payload = _verify_audit_logs(args.input, args.format)
    lines = [
        f"valid={str(payload['valid']).lower()}",
        f"files_checked={payload['files_checked']}",
        f"entries_checked={payload['entries_checked']}",
        f"final_checksum={payload['final_checksum']}",
    ]
    if payload["error"]:
        lines.append(f"error={payload['error']}")
    _emit(payload, "\n".join(lines), args.json)
    return 0 if payload["valid"] else 2


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

    summary = asterion.aggregate_by_symbol(Path(args.input), format=args.format, shared=args.shared)
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
        "combined_book_checksum": summary.combined_book_checksum,
        "aggregate_checksum": summary.aggregate_checksum,
        "path": "shared" if args.shared else "grouped",
        "symbols": symbols,
    }
    lines = [
        f"total_events={summary.total_events}",
        f"symbol_count={summary.symbol_count}",
        f"combined_book_checksum={summary.combined_book_checksum}",
        f"aggregate_checksum={summary.aggregate_checksum}",
        f"path={payload['path']}",
    ]
    for symbol in symbols:
        lines.append(
            f"symbol={symbol['symbol_id']} events={symbol['event_count']} "
            f"final_book_checksum={symbol['final_book_checksum']} "
            f"sequence_valid={symbol['sequence_valid']}"
        )
    _emit(payload, "\n".join(lines), args.json)
    return 0


def cmd_shared_fuzz(args: argparse.Namespace) -> int:
    import asterion  # noqa: PLC0415

    payload = asterion.shared_replay_fuzz_summary(
        seeds=args.seed,
        event_count=args.events,
        symbol_count=args.symbols,
    )
    lines = [
        f"case_count={payload['case_count']}",
        f"mismatch_count={payload['mismatch_count']}",
        f"events_per_case={payload['events_per_case']}",
        f"symbol_count={payload['symbol_count']}",
    ]
    for case in payload["cases"]:
        lines.append(
            f"seed={case['seed']} events={case['events']} "
            f"grouped_checksum={case['grouped_aggregate_checksum']} "
            f"shared_checksum={case['shared_aggregate_checksum']} "
            f"matched={str(case['matched']).lower()}"
        )
    _emit(payload, "\n".join(lines), args.json)
    return 0 if payload["mismatch_count"] == 0 else 2


def _enum(module: Any, enum_name: str, value: str) -> Any:
    token = value.strip().replace("-", "_")
    lookup = {
        "none": "None_",
        "buy": "Buy",
        "sell": "Sell",
        "limit": "Limit",
        "market": "Market",
        "new": "New",
        "trade": "Trade",
        "canceled": "Canceled",
        "cancelled": "Canceled",
        "replaced": "Replaced",
        "rejected": "Rejected",
        "partiallyfilled": "PartiallyFilled",
        "partially_filled": "PartiallyFilled",
        "filled": "Filled",
        "none_": "None_",
        "killswitch": "KillSwitch",
        "kill_switch": "KillSwitch",
        "fixedwindow": "FixedWindow",
        "fixed_window": "FixedWindow",
        "slidingwindow": "SlidingWindow",
        "sliding_window": "SlidingWindow",
        "rejectneworders": "RejectNewOrders",
        "reject_new_orders": "RejectNewOrders",
        "reject-new-orders": "RejectNewOrders",
        "allowneworders": "AllowNewOrders",
        "allow_new_orders": "AllowNewOrders",
        "allow-new-orders": "AllowNewOrders",
        "disconnected": "Disconnected",
    }
    attr = lookup.get(token.lower(), token)
    return getattr(getattr(module, enum_name), attr)


def _new_order_from_dict(asterion: Any, item: dict[str, Any]) -> Any:
    request = asterion.NewOrderRequest()
    request.client_order_id = int(item["client_order_id"])
    request.symbol_id = int(item.get("symbol_id", 1))
    request.side = _enum(asterion, "Side", str(item.get("side", "Buy")))
    request.order_type = _enum(asterion, "OrderType", str(item.get("order_type", "Limit")))
    request.price_ticks = int(item.get("price_ticks", 0))
    request.quantity = int(item.get("quantity", 0))
    request.timestamp_ns = int(item.get("timestamp_ns", item.get("now_ns", 0)))
    request.client_id = int(item.get("client_id", 0))
    return request


def _execution_report_from_dict(asterion: Any, item: dict[str, Any]) -> Any:
    report = asterion.ExecutionReport()
    report.client_order_id = int(item["client_order_id"])
    report.exchange_order_id = int(item.get("exchange_order_id", 0))
    report.symbol_id = int(item.get("symbol_id", 1))
    report.side = _enum(asterion, "Side", str(item.get("side", "None")))
    report.order_status = _enum(asterion, "OrderStatus", str(item.get("order_status", "New")))
    report.exec_type = _enum(asterion, "ExecType", str(item.get("exec_type", "New")))
    report.filled_quantity = int(item.get("filled_quantity", 0))
    report.remaining_quantity = int(item.get("remaining_quantity", 0))
    report.last_fill_quantity = int(item.get("last_fill_quantity", 0))
    report.last_fill_price_ticks = int(item.get("last_fill_price_ticks", 0))
    report.average_price_ticks = int(item.get("average_price_ticks", 0))
    report.resting_price_ticks = int(item.get("resting_price_ticks", 0))
    report.timestamp_ns = int(item.get("timestamp_ns", 0))
    report.reject_reason = _enum(asterion, "RejectReason", str(item.get("reject_reason", "None")))
    return report


def _replace_order_from_dict(asterion: Any, item: dict[str, Any]) -> Any:
    request = asterion.ReplaceOrderRequest()
    request.client_order_id = int(item["client_order_id"])
    request.exchange_order_id = int(item["exchange_order_id"])
    request.new_price_ticks = int(item.get("new_price_ticks", item.get("price_ticks", 0)))
    request.new_quantity = int(item.get("new_quantity", item.get("quantity", 0)))
    request.timestamp_ns = int(item.get("timestamp_ns", item.get("now_ns", 0)))
    return request


def cmd_risk_exposure(args: argparse.Namespace) -> int:
    import asterion  # noqa: PLC0415

    fixture = json.loads(args.input.read_text(encoding="utf-8"))
    limits = asterion.RiskLimits()
    for key, value in fixture.get("limits", {}).items():
        if key == "rate_limit_mode":
            setattr(limits, key, _enum(asterion, "RateLimitMode", str(value)))
        elif key == "disconnect_order_policy":
            setattr(limits, key, _enum(asterion, "DisconnectOrderPolicy", str(value)))
        else:
            setattr(limits, key, value)
    risk = asterion.RiskGateway(limits)
    if fixture.get("audit_enabled", False):
        risk.set_audit_enabled(True)
    for item in fixture.get("market_data", []):
        risk.on_market_data(
            int(item["symbol_id"]),
            int(item["reference_price_ticks"]),
            int(item["timestamp_ns"]),
        )
    for item in fixture.get("positions", []):
        risk.set_position(int(item["symbol_id"]), int(item["quantity"]))

    decisions = []
    for item in fixture.get("orders", []):
        request = _new_order_from_dict(asterion, item)
        result = risk.check_new_order(request, int(item.get("now_ns", request.timestamp_ns)))
        decisions.append(
            {
                "type": "new",
                "client_order_id": request.client_order_id,
                "accepted": result.accepted,
                "reject_reason": str(result.reject_reason).split(".")[-1],
            }
        )
    for item in fixture.get("execution_reports", []):
        risk.on_execution_report(_execution_report_from_dict(asterion, item))
    for item in fixture.get("replaces", []):
        request = _replace_order_from_dict(asterion, item)
        result = risk.check_replace_order(request, int(item.get("now_ns", request.timestamp_ns)))
        decisions.append(
            {
                "type": "replace",
                "client_order_id": request.client_order_id,
                "exchange_order_id": request.exchange_order_id,
                "accepted": result.accepted,
                "reject_reason": str(result.reject_reason).split(".")[-1],
            }
        )
    if "disconnect_timestamp_ns" in fixture:
        risk.on_disconnect(int(fixture["disconnect_timestamp_ns"]))
    if "reconnect_timestamp_ns" in fixture:
        risk.on_reconnect(int(fixture["reconnect_timestamp_ns"]))
    if "kill_switch_timestamp_ns" in fixture:
        risk.enable_kill_switch(int(fixture["kill_switch_timestamp_ns"]))

    snapshot = risk.exposure_snapshot()
    payload = {
        "input": str(args.input),
        "decisions": decisions,
        "positions": {str(key): value for key, value in dict(snapshot.positions).items()},
        "working_quantity": {
            str(key): value for key, value in dict(snapshot.working_quantity).items()
        },
        "working_order_count": snapshot.working_order_count,
        "kill_switch_enabled": snapshot.kill_switch_enabled,
        "connected": snapshot.connected,
        "disconnect_count": snapshot.disconnect_count,
        "disconnect_cancel_count": snapshot.disconnect_cancel_count,
        "rate_limit_mode": asterion.rate_limit_mode_to_string(snapshot.rate_limit_mode),
        "disconnect_order_policy": asterion.disconnect_order_policy_to_string(
            snapshot.disconnect_order_policy
        ),
        "audit_entry_count": snapshot.audit_entry_count,
        "audit_checksum": snapshot.audit_checksum,
    }
    lines = [
        f"working_order_count={payload['working_order_count']}",
        f"kill_switch_enabled={str(payload['kill_switch_enabled']).lower()}",
        f"connected={str(payload['connected']).lower()}",
        f"disconnect_count={payload['disconnect_count']}",
        f"disconnect_cancel_count={payload['disconnect_cancel_count']}",
        f"rate_limit_mode={payload['rate_limit_mode']}",
        f"disconnect_order_policy={payload['disconnect_order_policy']}",
        f"audit_entry_count={payload['audit_entry_count']}",
        f"audit_checksum={payload['audit_checksum']}",
    ]
    for symbol, quantity in sorted(payload["working_quantity"].items()):
        lines.append(f"working_quantity symbol={symbol} quantity={quantity}")
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
    per_symbol.add_argument("--shared", action="store_true", help="Use the opt-in shared path.")
    per_symbol.add_argument("--json", action="store_true")
    per_symbol.set_defaults(func=cmd_per_symbol)

    shared_fuzz = subparsers.add_parser(
        "shared-fuzz", help="Run deterministic shared-vs-grouped replay fuzz summaries."
    )
    shared_fuzz.add_argument("--seed", type=int, nargs="+", default=[20260528, 20260529])
    shared_fuzz.add_argument("--events", type=int, default=80)
    shared_fuzz.add_argument("--symbols", type=int, default=4)
    shared_fuzz.add_argument("--json", action="store_true")
    shared_fuzz.set_defaults(func=cmd_shared_fuzz)

    risk_exposure = subparsers.add_parser(
        "risk-exposure", help="Run a small JSON risk flow and print exposure state."
    )
    risk_exposure.add_argument("--input", required=True, type=Path)
    risk_exposure.add_argument("--json", action="store_true")
    risk_exposure.set_defaults(func=cmd_risk_exposure)

    audit_summary = subparsers.add_parser(
        "audit-summary", help="Summarise an append-only risk audit log."
    )
    audit_summary.add_argument("--input", required=True, type=Path)
    audit_summary.add_argument("--format", default="auto", choices=["auto", "jsonl", "text"])
    audit_summary.add_argument("--json", action="store_true")
    audit_summary.set_defaults(func=cmd_audit_summary)

    audit_verify = subparsers.add_parser(
        "audit-verify", help="Verify append-only risk audit log checksums."
    )
    audit_verify.add_argument("--input", required=True, nargs="+", type=Path)
    audit_verify.add_argument("--format", default="auto", choices=["auto", "jsonl", "text"])
    audit_verify.add_argument("--json", action="store_true")
    audit_verify.set_defaults(func=cmd_audit_verify)

    rate_mode = subparsers.add_parser(
        "rate-limit-mode", help="Normalise a configured rate-limit mode."
    )
    rate_mode.add_argument("--mode", required=True)
    rate_mode.add_argument("--json", action="store_true")
    rate_mode.set_defaults(func=cmd_rate_limit_mode)

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

    bench_store = subparsers.add_parser(
        "benchmark-store", help="Store a benchmark JSON in the local history directory."
    )
    bench_store.add_argument("--input", required=True, type=Path)
    bench_store.add_argument("--history-dir", required=True, type=Path)
    bench_store.add_argument("--name", default=None)
    bench_store.add_argument("--json", action="store_true")
    bench_store.set_defaults(func=cmd_benchmark_store)

    bench_trend = subparsers.add_parser(
        "benchmark-trend", help="Report benchmark trends across stored JSON files."
    )
    bench_trend.add_argument("--history-dir", type=Path)
    bench_trend.add_argument("--inputs", nargs="+", type=Path)
    bench_trend.add_argument("--glob", default="*.json")
    bench_trend.add_argument(
        "--metric", default="avg_ns", choices=["avg_ns", "total_ns", "iterations"]
    )
    bench_trend.add_argument("--json", action="store_true")
    bench_trend.set_defaults(func=cmd_benchmark_trend)

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
