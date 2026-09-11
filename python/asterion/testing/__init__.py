"""Test-only specification aids for Asterion's matching semantics.

This subpackage is a *reference / oracle* surface used by Asterion's Python
tests. It is intentionally simple and independent of the C++ matching engine.

It is not part of Asterion's public runtime API and it performs no I/O beyond
the event sequences the tests hand it.
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
