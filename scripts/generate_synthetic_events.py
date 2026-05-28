#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import os
import random
from dataclasses import dataclass


MODES = (
    "balanced",
    "high-cancel",
    "deep-book",
    "bursty",
    "multi-symbol",
    "wide-price-range",
)


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
    if mode == "deep-book":
        depth = rng.randint(1, price_range)
        price = mid - depth if side == "Buy" else mid + depth
    else:
        price = mid + rng.randint(-price_range, price_range)
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


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate deterministic Asterion CSV events.")
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
    args = parser.parse_args()

    rng = random.Random(args.seed)
    active: list[ActiveOrder] = []
    next_order_id = 1
    next_trade_id = 1

    output_dir = os.path.dirname(args.output)
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)

    with open(args.output, "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
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
        )
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
                writer.writerow(
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
                writer.writerow(
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
                writer.writerow(
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
            writer.writerow(
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
