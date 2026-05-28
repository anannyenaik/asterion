#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import os
import struct
from pathlib import Path


CSV_HEADER = [
    "timestamp_ns",
    "sequence_number",
    "symbol_id",
    "event_type",
    "side",
    "price_ticks",
    "quantity",
    "order_id",
    "trade_id",
    "flags",
]

BINARY_MAGIC = b"ASTITCH1"
BINARY_VERSION = 1
BINARY_HEADER_SIZE = 16
BINARY_RECORD_SIZE = 58
BINARY_HEADER = BINARY_MAGIC + struct.pack(
    "<HHHH", BINARY_VERSION, BINARY_HEADER_SIZE, BINARY_RECORD_SIZE, 0
)
BINARY_RECORD = struct.Struct("<qQIBBIqqQQ")
EVENT_TYPE_IDS = {
    "Add": 1,
    "Cancel": 2,
    "Replace": 3,
    "Execute": 4,
    "Trade": 5,
    "Snapshot": 6,
    "Heartbeat": 7,
}
SIDE_IDS = {"None": 0, "Buy": 1, "Sell": 2, "": 0}


def pack_row(row: dict[str, str]) -> bytes:
    return BINARY_RECORD.pack(
        int(row["timestamp_ns"]),
        int(row["sequence_number"]),
        int(row["symbol_id"]),
        EVENT_TYPE_IDS[row["event_type"]],
        SIDE_IDS[row["side"]],
        int(row["flags"]),
        int(row["price_ticks"]),
        int(row["quantity"]),
        int(row["order_id"]),
        int(row["trade_id"]),
    )


def convert_csv_to_binary(input_path: Path, output_path: Path) -> int:
    output_dir = output_path.parent
    if str(output_dir):
        os.makedirs(output_dir, exist_ok=True)

    count = 0
    with input_path.open("r", newline="", encoding="utf-8") as source, output_path.open(
        "wb"
    ) as target:
        reader = csv.DictReader(
            row for row in source if row.strip() and not row.lstrip().startswith("#")
        )
        if reader.fieldnames != CSV_HEADER:
            raise ValueError(f"unexpected CSV header: {reader.fieldnames}")

        target.write(BINARY_HEADER)
        for row in reader:
            target.write(pack_row(row))
            count += 1
    return count


def main() -> None:
    parser = argparse.ArgumentParser(description="Convert an Asterion CSV event log to binary.")
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    count = convert_csv_to_binary(Path(args.input), Path(args.output))
    print(f"converted_events={count}")
    print(f"output={args.output}")


if __name__ == "__main__":
    main()
