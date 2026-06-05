"""Cross-check harness: replay the same order flow into Asterion's C++ matching
engine and the independent Python :class:`ReferenceMatcher`, then compare.

This is test-only glue. It builds request objects, applies an operation stream
to both matchers, normalises their outputs into a common canonical form, and
reports the *first* meaningful difference so a failing case is reproducible.

What is compared (see ``docs/reference_matcher.md`` for the rationale and the
known limitations):

* the full execution-report sequence, field by field;
* the final L2 book (price / aggregate quantity per level, both sides);
* the C++ canonical report checksum, recomputed over the reference's reports
  via the bound ``checksum_execution_reports`` (so both checksums are produced
  by the same hashing function).
"""

from __future__ import annotations

import random
from dataclasses import dataclass

import asterion

from .reference_matcher import ReferenceMatcher, canonical_report_tuple


# --------------------------------------------------------------------------
# Operations
# --------------------------------------------------------------------------


@dataclass
class Op:
    """A single matcher operation. ``request`` is a bound request object that is
    read (never mutated) by both matchers."""

    kind: str  # "new" | "cancel" | "replace"
    request: object

    def describe(self) -> str:
        r = self.request
        if self.kind == "new":
            return (
                f"new coid={r.client_order_id} side={r.side.name} type={r.order_type.name} "
                f"px={r.price_ticks} qty={r.quantity} tif={r.time_in_force.name} "
                f"post_only={r.post_only} client_id={r.client_id} ts={r.timestamp_ns}"
            )
        if self.kind == "cancel":
            return f"cancel coid={r.client_order_id} exch_id={r.exchange_order_id} ts={r.timestamp_ns}"
        return (
            f"replace coid={r.client_order_id} exch_id={r.exchange_order_id} "
            f"new_px={r.new_price_ticks} new_qty={r.new_quantity} ts={r.timestamp_ns}"
        )


def new_order(
    client_order_id: int,
    side: asterion.Side,
    order_type: asterion.OrderType,
    price_ticks: int,
    quantity: int,
    timestamp_ns: int,
    *,
    time_in_force: asterion.TimeInForce = asterion.TimeInForce.Gtc,
    post_only: bool = False,
    client_id: int = 0,
    symbol_id: int = 1,
) -> Op:
    request = asterion.NewOrderRequest()
    request.client_order_id = client_order_id
    request.symbol_id = symbol_id
    request.side = side
    request.order_type = order_type
    request.price_ticks = price_ticks
    request.quantity = quantity
    request.timestamp_ns = timestamp_ns
    request.time_in_force = time_in_force
    request.post_only = post_only
    request.client_id = client_id
    return Op("new", request)


def cancel_order(client_order_id: int, exchange_order_id: int, timestamp_ns: int) -> Op:
    request = asterion.CancelOrderRequest()
    request.client_order_id = client_order_id
    request.exchange_order_id = exchange_order_id
    request.timestamp_ns = timestamp_ns
    return Op("cancel", request)


def replace_order(
    client_order_id: int,
    exchange_order_id: int,
    new_price_ticks: int,
    new_quantity: int,
    timestamp_ns: int,
) -> Op:
    request = asterion.ReplaceOrderRequest()
    request.client_order_id = client_order_id
    request.exchange_order_id = exchange_order_id
    request.new_price_ticks = new_price_ticks
    request.new_quantity = new_quantity
    request.timestamp_ns = timestamp_ns
    return Op("replace", request)


def _apply(matcher, op: Op):
    if op.kind == "new":
        return list(matcher.submit_order(op.request))
    if op.kind == "cancel":
        return list(matcher.cancel_order(op.request))
    if op.kind == "replace":
        return list(matcher.replace_order(op.request))
    raise ValueError(f"unknown op kind: {op.kind}")


# --------------------------------------------------------------------------
# Canonical normalisation
# --------------------------------------------------------------------------


def _cpp_l2(engine: asterion.MatchingEngine) -> dict:
    view = engine.book().l2_view(engine.book().order_count() + 1)
    return {
        "bids": [(level.price_ticks, level.quantity) for level in view.bids],
        "asks": [(level.price_ticks, level.quantity) for level in view.asks],
    }


def _ref_report_to_execution_report(report) -> asterion.ExecutionReport:
    out = asterion.ExecutionReport()
    out.client_order_id = report.client_order_id
    out.exchange_order_id = report.exchange_order_id
    out.symbol_id = report.symbol_id
    out.side = report.side
    out.order_status = report.order_status
    out.exec_type = report.exec_type
    out.filled_quantity = report.filled_quantity
    out.remaining_quantity = report.remaining_quantity
    out.last_fill_quantity = report.last_fill_quantity
    out.last_fill_price_ticks = report.last_fill_price_ticks
    out.average_price_ticks = report.average_price_ticks
    out.resting_price_ticks = report.resting_price_ticks
    out.timestamp_ns = report.timestamp_ns
    out.reject_reason = report.reject_reason
    return out


# --------------------------------------------------------------------------
# Cross-check result
# --------------------------------------------------------------------------


@dataclass
class CrossCheckResult:
    matched: bool
    cpp_reports: list
    ref_reports: list
    cpp_l2: dict
    ref_l2: dict
    cpp_reports_checksum: int
    ref_reports_checksum: int
    first_report_mismatch: int | None
    detail: str


def run_cross_check(ops, symbol_id: int = 1) -> CrossCheckResult:
    """Replay ``ops`` into both matchers and compare canonical outputs."""

    cpp = asterion.MatchingEngine(symbol_id)
    ref = ReferenceMatcher(symbol_id)

    cpp_reports: list = []
    ref_reports: list = []
    op_boundaries: list[int] = []  # index into the flat report stream per op

    for op in ops:
        cpp_reports.extend(_apply(cpp, op))
        ref_reports.extend(_apply(ref, op))
        op_boundaries.append(len(ref_reports))

    # 1. report sequence, field by field.
    first_mismatch = None
    limit = min(len(cpp_reports), len(ref_reports))
    for i in range(limit):
        if canonical_report_tuple(cpp_reports[i]) != canonical_report_tuple(ref_reports[i]):
            first_mismatch = i
            break
    if first_mismatch is None and len(cpp_reports) != len(ref_reports):
        first_mismatch = limit

    # 2. final L2 book.
    cpp_l2 = _cpp_l2(cpp)
    ref_l2 = ref.l2_levels()

    # 3. checksums (same hashing function on both sides).
    cpp_checksum = cpp.reports_checksum()
    ref_checksum = asterion.checksum_execution_reports(
        [_ref_report_to_execution_report(r) for r in ref_reports]
    )

    matched = (
        first_mismatch is None
        and len(cpp_reports) == len(ref_reports)
        and cpp_l2 == ref_l2
        and cpp_checksum == ref_checksum
    )

    detail = ""
    if not matched:
        detail = _build_detail(
            ops,
            op_boundaries,
            cpp_reports,
            ref_reports,
            cpp_l2,
            ref_l2,
            cpp_checksum,
            ref_checksum,
            first_mismatch,
        )

    return CrossCheckResult(
        matched=matched,
        cpp_reports=cpp_reports,
        ref_reports=ref_reports,
        cpp_l2=cpp_l2,
        ref_l2=ref_l2,
        cpp_reports_checksum=cpp_checksum,
        ref_reports_checksum=ref_checksum,
        first_report_mismatch=first_mismatch,
        detail=detail,
    )


def _op_index_for_report(op_boundaries: list[int], report_index: int) -> int:
    for op_index, boundary in enumerate(op_boundaries):
        if report_index < boundary:
            return op_index
    return len(op_boundaries) - 1


def _build_detail(
    ops,
    op_boundaries,
    cpp_reports,
    ref_reports,
    cpp_l2,
    ref_l2,
    cpp_checksum,
    ref_checksum,
    first_mismatch,
) -> str:
    lines: list[str] = []
    lines.append(f"report counts: cpp={len(cpp_reports)} ref={len(ref_reports)}")
    if first_mismatch is not None:
        op_index = _op_index_for_report(op_boundaries, first_mismatch)
        lines.append(f"first report mismatch at report index {first_mismatch} (op index {op_index})")
        lines.append(f"  op: {ops[op_index].describe()}")
        cpp_t = (
            canonical_report_tuple(cpp_reports[first_mismatch])
            if first_mismatch < len(cpp_reports)
            else "<missing>"
        )
        ref_t = (
            canonical_report_tuple(ref_reports[first_mismatch])
            if first_mismatch < len(ref_reports)
            else "<missing>"
        )
        lines.append(f"  cpp report: {cpp_t}")
        lines.append(f"  ref report: {ref_t}")
    if cpp_l2 != ref_l2:
        lines.append("final L2 mismatch:")
        lines.append(f"  cpp L2: {cpp_l2}")
        lines.append(f"  ref L2: {ref_l2}")
    if cpp_checksum != ref_checksum:
        lines.append(f"report checksum mismatch: cpp={cpp_checksum} ref={ref_checksum}")
    lines.append("operation sequence:")
    for i, op in enumerate(ops):
        lines.append(f"  [{i}] {op.describe()}")
    return "\n".join(lines)


# --------------------------------------------------------------------------
# Deterministic random stream generation
# --------------------------------------------------------------------------


def generate_stream(
    seed: int,
    n_ops: int,
    *,
    symbol_id: int = 1,
    price_min: int = 995,
    price_max: int = 1005,
    qty_min: int = 1,
    qty_max: int = 8,
    n_clients: int = 3,
) -> list[Op]:
    """Build a small, varied, fully deterministic operation stream.

    Generation runs a throwaway reference matcher so that cancel/replace
    operations can target genuinely-resting exchange ids (and, sometimes,
    unknown ones). Because exchange-id assignment is deterministic and identical
    across matchers, the produced stream is valid to replay into both.
    """

    rng = random.Random(seed)
    gen_ref = ReferenceMatcher(symbol_id)
    ops: list[Op] = []
    used_coids: list[int] = []
    next_coid = 1
    ts = 1

    def fresh_coid() -> int:
        nonlocal next_coid
        coid = next_coid
        next_coid += 1
        used_coids.append(coid)
        return coid

    def resting_ids() -> list[int]:
        return [row[0] for row in gen_ref.open_orders()]

    for _ in range(n_ops):
        roll = rng.random()
        if roll < 0.62:
            # New order: vary side, type, TIF, post-only, attribution.
            side = rng.choice([asterion.Side.Buy, asterion.Side.Sell])
            order_type = (
                asterion.OrderType.Market if rng.random() < 0.12 else asterion.OrderType.Limit
            )
            tif = rng.choices(
                [asterion.TimeInForce.Gtc, asterion.TimeInForce.Ioc, asterion.TimeInForce.Fok],
                weights=[0.6, 0.25, 0.15],
            )[0]
            post_only = (
                order_type == asterion.OrderType.Limit
                and tif == asterion.TimeInForce.Gtc
                and rng.random() < 0.18
            )
            price = (
                0
                if order_type == asterion.OrderType.Market
                else rng.randint(price_min, price_max)
            )
            quantity = rng.randint(qty_min, qty_max)
            client_id = rng.randint(0, n_clients)  # 0 == unattributed
            # Occasionally reuse a coid to exercise duplicate rejects.
            if used_coids and rng.random() < 0.06:
                coid = rng.choice(used_coids)
            else:
                coid = fresh_coid()
            op = new_order(
                coid,
                side,
                order_type,
                price,
                quantity,
                ts,
                time_in_force=tif,
                post_only=post_only,
                client_id=client_id,
                symbol_id=symbol_id,
            )
        elif roll < 0.81:
            # Cancel: usually a resting order, sometimes an unknown id.
            resting = resting_ids()
            if resting and rng.random() < 0.8:
                target = rng.choice(resting)
            else:
                target = rng.randint(1, max(1, next_coid + 3))
            op = cancel_order(fresh_coid(), target, ts)
        else:
            # Replace: usually a resting order, sometimes an unknown id.
            resting = resting_ids()
            if resting and rng.random() < 0.8:
                target = rng.choice(resting)
            else:
                target = rng.randint(1, max(1, next_coid + 3))
            op = replace_order(
                fresh_coid(),
                target,
                rng.randint(price_min, price_max),
                rng.randint(qty_min, qty_max),
                ts,
            )

        _apply(gen_ref, op)
        ops.append(op)
        ts += 1

    return ops
