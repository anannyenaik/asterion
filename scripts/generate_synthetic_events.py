#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import random


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate deterministic Asterion CSV events.")
    parser.add_argument("--events", type=int, default=100)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--symbol", type=int, default=1)
    parser.add_argument("--output", default="data/samples/generated_replay.csv")
    args = parser.parse_args()

    rng = random.Random(args.seed)
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
            side = "Buy" if rng.randrange(2) == 0 else "Sell"
            base = 999 if side == "Buy" else 1001
            writer.writerow(
                [
                    1_000_000_000 + index,
                    index + 1,
                    args.symbol,
                    "Add",
                    side,
                    base + rng.randint(-5, 5),
                    rng.randint(1, 250),
                    index + 1,
                    0,
                    0,
                ]
            )


if __name__ == "__main__":
    main()
