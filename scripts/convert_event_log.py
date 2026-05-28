#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

import asterion


def main() -> None:
    parser = argparse.ArgumentParser(description="Convert an Asterion event log.")
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--input-format", choices=("auto", "csv", "binary"), default="auto")
    parser.add_argument("--output-format", choices=("auto", "csv", "binary"), default="auto")
    args = parser.parse_args()

    result = asterion.convert_log(
        Path(args.input),
        Path(args.output),
        input_format=args.input_format,
        output_format=args.output_format,
    )
    print(f"converted_events={result.events_written}")
    print(f"event_checksum={result.event_checksum}")
    print(f"output={args.output}")


if __name__ == "__main__":
    main()
