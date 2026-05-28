#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path

import asterion


ROOT = Path(__file__).resolve().parents[2]
SAMPLES = ROOT / "data" / "samples"


def main() -> None:
    for path in (SAMPLES / "sample_replay.csv", SAMPLES / "sample_replay.bin"):
        result = asterion.run_replay(path, symbol_id=1)
        detected = asterion.event_log_format_to_string(asterion.detect_format(path))
        print(
            f"{path.name}: format={detected} events={result.events_processed} "
            f"book_checksum={result.final_book_checksum} "
            f"diagnostics={len(result.diagnostics)}"
        )


if __name__ == "__main__":
    main()
