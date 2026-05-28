#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import os
import random
import struct
from dataclasses import dataclass


MODES = (
    "balanced",
    "high-cancel",
    "deep-book",
    "bursty",
    "multi-symbol",
    "wide-price-range",
)

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
SIDE_IDS = {"None": 0, "Buy": 1, "Sell": 2}


@dataclass
class ActiveOrder:
    order_id: int
    symbol_id: int
    side: str
    price_ticks: int
    quantity: int


def price_range_for_mode(mode: str, requested: int) -> int:
    if mode == "deep-book":
        return max(requested, 100)
    if mode == "wide-price-range":
        return max(requested, 500)
    return max(requested, 1)


def choose_price(rng: random.Random, mode: str, side: str, mid: int, requested_range: int) -> int:
    price_range = price_range_for_mode(mode, requested_range)
    depth = rng.randint(1, price_range)
    price = mid - depth if side == "Buy" else mid + depth
    return max(1, price)


def choose_symbol(index: int, base_symbol: int, symbols: int, mode: str) -> int:
    if mode != "multi-symbol":
        return base_symbol
    return base_symbol + (index % max(1, symbols))


def choose_timestamp(index: int, first_timestamp_ns: int, burst_size: int, mode: str) -> int:
    if mode != "bursty":
        return first_timestamp_ns + index
    return first_timestamp_ns + (index // max(1, burst_size))


def should_add(mode: str, roll: int, active_empty: bool) -> bool:
    if active_empty:
        return True
    if mode == "high-cancel":
        return roll < 35
    if mode == "deep-book":
        return roll < 75
    return roll < 55


def choose_existing_event(mode: str, roll: int) -> str:
    if mode == "high-cancel":
        if roll < 85:
            return "Cancel"
        return "Replace" if roll < 95 else "Execute"
    if roll < 75:
        return "Cancel"
    return "Replace" if roll < 90 else "Execute"


def choose_output_format(path: str, requested: str) -> str:
    if requested != "auto":
        return requested
    extension = os.path.splitext(path)[1].lower()
    return "binary" if extension in {".bin", ".itch", ".ablog"} else "csv"


def pack_binary_event(row: list[object]) -> bytes:
    return BINARY_RECORD.pack(
        int(row[0]),
        int(row[1]),
        int(row[2]),
        EVENT_TYPE_IDS[str(row[3])],
        SIDE_IDS[str(row[4])],
        int(row[9]),
        int(row[5]),
        int(row[6]),
        int(row[7]),
        int(row[8]),
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate deterministic Asterion event logs.")
    parser.add_argument("--events", type=int, default=100)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--symbol", type=int, default=1)
    parser.add_argument("--symbols", type=int, default=4)
    parser.add_argument("--mode", choices=MODES, default="balanced")
    parser.add_argument("--mid-price", type=int, default=1000)
    parser.add_argument("--price-range", type=int, default=5)
    parser.add_argument("--burst-size", type=int, default=8)
    parser.add_argument("--first-sequence", type=int, default=1)
    parser.add_argument("--first-timestamp-ns", type=int, default=1_000_000_000)
    parser.add_argument("--output", default="data/generated/generated_replay.csv")
    parser.add_argument("--format", choices=("auto", "csv", "binary"), default="auto")
    args = parser.parse_args()

    rng = random.Random(args.seed)
    active: list[ActiveOrder] = []
    next_order_id = 1
    next_trade_id = 1

    output_dir = os.path.dirname(args.output)
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)

    output_format = choose_output_format(args.output, args.format)
    if output_format == "csv":
        handle = open(args.output, "w", newline="", encoding="utf-8")
        writer = csv.writer(handle)
        writer.writerow(CSV_HEADER)

        def write_event(row: list[object]) -> None:
            writer.writerow(row)

    else:
        handle = open(args.output, "wb")
        handle.write(BINARY_HEADER)

        def write_event(row: list[object]) -> None:
            handle.write(pack_binary_event(row))

    with handle:
        for index in range(args.events):
            timestamp_ns = choose_timestamp(index, args.first_timestamp_ns, args.burst_size, args.mode)
            sequence_number = args.first_sequence + index
            roll = rng.randrange(100)

            if should_add(args.mode, roll, not active):
                side = "Buy" if rng.randrange(2) == 0 else "Sell"
                symbol_id = choose_symbol(index, args.symbol, args.symbols, args.mode)
                price_ticks = choose_price(rng, args.mode, side, args.mid_price, args.price_range)
                quantity = rng.randint(1, 250)
                order_id = next_order_id
                next_order_id += 1
                active.append(ActiveOrder(order_id, symbol_id, side, price_ticks, quantity))
                write_event(
                    [
                        timestamp_ns,
                        sequence_number,
                        symbol_id,
                        "Add",
                        side,
                        price_ticks,
                        quantity,
                        order_id,
                        0,
                        0,
                    ]
                )
                continue

            active_index = rng.randrange(len(active))
            order = active[active_index]
            event_type = choose_existing_event(args.mode, roll)

            if event_type == "Cancel":
                write_event(
                    [
                        timestamp_ns,
                        sequence_number,
                        order.symbol_id,
                        "Cancel",
                        order.side,
                        order.price_ticks,
                        0,
                        order.order_id,
                        0,
                        0,
                    ]
                )
                active.pop(active_index)
                continue

            if event_type == "Replace":
                order.price_ticks = choose_price(
                    rng, args.mode, order.side, args.mid_price, args.price_range
                )
                order.quantity = rng.randint(1, 250)
                write_event(
                    [
                        timestamp_ns,
                        sequence_number,
                        order.symbol_id,
                        "Replace",
                        order.side,
                        order.price_ticks,
                        order.quantity,
                        order.order_id,
                        0,
                        0,
                    ]
                )
                continue

            executed_quantity = rng.randint(1, order.quantity)
            write_event(
                [
                    timestamp_ns,
                    sequence_number,
                    order.symbol_id,
                    "Execute",
                    order.side,
                    order.price_ticks,
                    executed_quantity,
                    order.order_id,
                    next_trade_id,
                    0,
                ]
            )
            next_trade_id += 1
            order.quantity -= executed_quantity
            if order.quantity == 0:
                active.pop(active_index)


if __name__ == "__main__":
    main()
