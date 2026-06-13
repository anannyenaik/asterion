#!/usr/bin/env python3
"""Evaluate parity for Asterion's opt-in SPSC replay pipeline.

Runs the deterministic single-thread replay path and the bounded
single-producer/single-consumer (SPSC) replay pipeline over the same events,
then reports parity and SPSC stats. Deterministic single-thread replay remains
the default in Asterion; the SPSC path is strictly opt-in and exists for systems
evaluation, not for production networking, live exchange connectivity or any
latency guarantee.

Requires the built Python bindings on PYTHONPATH, for example:

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_BUILD_PYTHON=ON
    cmake --build build
    PYTHONPATH=build/python python scripts/run_spsc_replay_demo.py \
        --input data/samples/sample_replay.csv --json
"""
from __future__ import annotations

import argparse
import json
import sys

try:
    import asterion
except ImportError as exc:  # pragma: no cover - depends on build
    sys.stderr.write(
        "unable to import the asterion bindings; build with "
        "-DASTERION_BUILD_PYTHON=ON and put build/python on PYTHONPATH\n"
    )
    raise SystemExit(2) from exc


def _single_thread(events, symbol_id):
    return asterion.run_replay(events, symbol_id=symbol_id)


def _spsc(events, symbol_id, capacity, drop):
    config = asterion.SpscReplayConfig()
    config.queue_capacity = capacity
    if drop:
        config.backpressure = asterion.SpscBackpressurePolicy.DropNewestOnFull
    return asterion.run_spsc_replay(events, symbol_id, config)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, help="event log path (CSV or binary)")
    parser.add_argument("--format", default="auto", help="auto|csv|binary")
    parser.add_argument(
        "--symbol-id",
        type=int,
        default=None,
        help="symbol id to replay (defaults to the first event's symbol)",
    )
    parser.add_argument("--queue-capacity", type=int, default=1024)
    parser.add_argument(
        "--drop-on-full",
        action="store_true",
        help="OPT-IN lossy overload shedding; not correctness-preserving (see docs)",
    )
    parser.add_argument("--json", action="store_true", help="emit JSON instead of text")
    args = parser.parse_args()

    events = asterion.load_log(args.input, format=args.format)
    if not events:
        sys.stderr.write("no events loaded from input\n")
        return 1
    symbol_id = args.symbol_id if args.symbol_id is not None else events[0].symbol_id

    single = _single_thread(events, symbol_id)
    spsc = _spsc(events, symbol_id, args.queue_capacity, args.drop_on_full)

    parity = (
        single.events_processed == spsc.replay.events_processed
        and single.final_book_checksum == spsc.replay.final_book_checksum
        and single.execution_report_checksum == spsc.replay.execution_report_checksum
        and single.diagnostics_checksum == spsc.replay.diagnostics_checksum
        and single.sequence_valid == spsc.replay.sequence_valid
    )

    report = {
        "input": args.input,
        "symbol_id": symbol_id,
        "event_count": len(events),
        "single_thread": {
            "events_processed": single.events_processed,
            "final_book_checksum": single.final_book_checksum,
            "execution_report_checksum": single.execution_report_checksum,
            "diagnostics_checksum": single.diagnostics_checksum,
            "sequence_valid": single.sequence_valid,
        },
        "spsc": {
            "events_processed": spsc.replay.events_processed,
            "final_book_checksum": spsc.replay.final_book_checksum,
            "execution_report_checksum": spsc.replay.execution_report_checksum,
            "diagnostics_checksum": spsc.replay.diagnostics_checksum,
            "sequence_valid": spsc.replay.sequence_valid,
            "queue_capacity": spsc.stats.queue_capacity,
            "produced_events": spsc.stats.produced_events,
            "consumed_events": spsc.stats.consumed_events,
            "max_queue_depth": spsc.stats.max_queue_depth,
            "backpressure_count": spsc.stats.backpressure_count,
            "dropped_events": spsc.stats.dropped_events,
            "end_of_stream_seen": spsc.stats.end_of_stream_seen,
            "end_of_stream_markers_produced": spsc.stats.end_of_stream_markers_produced,
            "end_of_stream_markers_consumed": spsc.stats.end_of_stream_markers_consumed,
            "drop_policy_enabled": spsc.stats.drop_policy_enabled,
            "elapsed_ns": spsc.stats.elapsed_ns,
            "throughput_events_per_second": spsc.stats.throughput_events_per_second,
        },
        "checksum_parity": parity,
    }

    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(f"input={report['input']} symbol_id={symbol_id} event_count={len(events)}")
        print(
            "single_thread "
            f"events_processed={single.events_processed} "
            f"final_book_checksum={single.final_book_checksum} "
            f"diagnostics_checksum={single.diagnostics_checksum}"
        )
        print(
            "spsc          "
            f"events_processed={spsc.replay.events_processed} "
            f"final_book_checksum={spsc.replay.final_book_checksum} "
            f"diagnostics_checksum={spsc.replay.diagnostics_checksum}"
        )
        print(
            "spsc_stats    "
            f"queue_capacity={spsc.stats.queue_capacity} "
            f"produced={spsc.stats.produced_events} "
            f"consumed={spsc.stats.consumed_events} "
            f"max_queue_depth={spsc.stats.max_queue_depth} "
            f"backpressure_count={spsc.stats.backpressure_count} "
            f"dropped={spsc.stats.dropped_events} "
            f"end_of_stream_seen={spsc.stats.end_of_stream_seen} "
            f"eos_produced={spsc.stats.end_of_stream_markers_produced} "
            f"eos_consumed={spsc.stats.end_of_stream_markers_consumed}"
        )
        print(f"checksum_parity={parity}")

    # Under the default lossless policy, parity must hold and nothing is dropped.
    if not args.drop_on_full and not parity:
        sys.stderr.write("ERROR: SPSC pipeline diverged from single-thread path\n")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
