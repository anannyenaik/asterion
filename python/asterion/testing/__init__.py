"""Test-only specification aids for Asterion's matching semantics.

This subpackage is a *reference / oracle* surface used by Asterion's Python
tests. It is intentionally simple and independent of the C++ matching engine.

It is **not** part of Asterion's public runtime API, it does not connect to any
venue or broker, it does not place orders anywhere, and it makes no claim of
production-exchange correctness, live-trading support or real-exchange
completeness. See ``docs/reference_matcher.md``.
"""

from __future__ import annotations

from .reference_matcher import (
    ReferenceMatcher,
    ReferenceReport,
    canonical_report_tuple,
)

__all__ = [
    "ReferenceMatcher",
    "ReferenceReport",
    "canonical_report_tuple",
]
