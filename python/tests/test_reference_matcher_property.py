"""Fixed-seed random cross-checks. Each seed builds a small, varied order
stream (multiple prices, bid/ask flow, cancels, replaces, IOC/FOK/post-only/
market orders, duplicate and unknown ids, attributed STP clients) and replays
it into both Asterion's C++ matching engine and the independent Python
reference matcher.

The streams are deterministic so a failure pins an exact, reproducible seed.
See ``docs/reference_matcher.md`` for how to reproduce a failing seed.
"""

from __future__ import annotations

import pytest

from asterion.testing.cross_check import generate_stream, run_cross_check

SEEDS = list(range(20260601, 20260621))  # 20 fixed seeds.
OPS_PER_SEED = 120


@pytest.mark.parametrize("seed", SEEDS)
def test_random_stream_matches_reference(seed: int):
    ops = generate_stream(seed, OPS_PER_SEED)
    result = run_cross_check(ops)
    assert result.matched, (
        f"C++ and reference matcher disagree for seed={seed}, "
        f"{len(ops)} ops:\n{result.detail}"
    )


@pytest.mark.parametrize("seed", SEEDS[:5])
def test_random_stream_deterministic_replay(seed: int):
    # The same generated stream must replay identically across repeated runs
    # (both the C++ checksum and the reference's own canonical checksum).
    ops = generate_stream(seed, OPS_PER_SEED)
    first = run_cross_check(ops)
    ops_again = generate_stream(seed, OPS_PER_SEED)
    second = run_cross_check(ops_again)
    assert first.cpp_reports_checksum == second.cpp_reports_checksum
    assert first.ref_reports_checksum == second.ref_reports_checksum
    assert first.cpp_l2 == second.cpp_l2


def test_generate_stream_is_pure_for_seed():
    # Determinism of the generator itself: identical op descriptions per seed.
    a = [op.describe() for op in generate_stream(20260601, 60)]
    b = [op.describe() for op in generate_stream(20260601, 60)]
    assert a == b
