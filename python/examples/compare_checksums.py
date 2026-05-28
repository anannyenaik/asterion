#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path

import asterion


ROOT = Path(__file__).resolve().parents[2]
SAMPLES = ROOT / "data" / "samples"


def main() -> None:
    csv_result = asterion.run_replay(SAMPLES / "sample_replay.csv", symbol_id=1)
    binary_result = asterion.run_replay(SAMPLES / "sample_replay.bin", symbol_id=1)

    checksums = (
        ("event_log", csv_result.event_log_checksum, binary_result.event_log_checksum),
        ("final_book", csv_result.final_book_checksum, binary_result.final_book_checksum),
        (
            "execution_report",
            csv_result.execution_report_checksum,
            binary_result.execution_report_checksum,
        ),
        ("diagnostics", csv_result.diagnostics_checksum, binary_result.diagnostics_checksum),
    )

    for name, csv_value, binary_value in checksums:
        status = "match" if csv_value == binary_value else "DIFF"
        print(f"{name}: csv={csv_value} binary={binary_value} {status}")


if __name__ == "__main__":
    main()
