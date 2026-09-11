"""Shared matplotlib settings for the repository figures.

Every figure is exported as a vector PDF at print size with a shared palette and
without gradients, shadows or three-dimensional effects.
"""

from __future__ import annotations

import matplotlib as mpl

# Two-series palette. STANDARD identifies the default path; POOLED and ALTERNATE
# identify the opt-in variants being compared against it.
STANDARD = "#1f3b63"
POOLED = "#b8532b"
ALTERNATE = "#4a7c59"
GRID = "#d4d4d4"
TEXT = "#1a1a1a"
MUTED = "#6b6b6b"

BASE_FONT_PT = 10.5
ANNOTATION_FONT_PT = 8.5


def apply() -> None:
    """Install the shared rcParams. Call once before building any figure."""

    mpl.rcParams.update(
        {
            "figure.dpi": 150,
            "savefig.dpi": 150,
            "savefig.bbox": "tight",
            "savefig.pad_inches": 0.02,
            # Embed Type 42 fonts so the PDFs survive a publisher's toolchain.
            "pdf.fonttype": 42,
            "ps.fonttype": 42,
            "font.family": "sans-serif",
            "font.sans-serif": ["DejaVu Sans", "Arial", "Helvetica"],
            "font.size": BASE_FONT_PT,
            "axes.titlesize": BASE_FONT_PT,
            "axes.labelsize": BASE_FONT_PT,
            "xtick.labelsize": BASE_FONT_PT - 1,
            "ytick.labelsize": BASE_FONT_PT - 1,
            "legend.fontsize": BASE_FONT_PT - 1,
            "axes.edgecolor": MUTED,
            "axes.labelcolor": TEXT,
            "text.color": TEXT,
            "xtick.color": MUTED,
            "ytick.color": MUTED,
            "axes.linewidth": 0.6,
            "xtick.major.width": 0.6,
            "ytick.major.width": 0.6,
            "grid.color": GRID,
            "grid.linewidth": 0.5,
            "legend.frameon": False,
            "axes.spines.top": False,
            "axes.spines.right": False,
            # Deterministic output: no creation timestamp in the PDF metadata.
            "svg.hashsalt": "asterion",
        }
    )


def strip_spines(axis, keep: tuple[str, ...] = ("left", "bottom")) -> None:
    """Hide every spine except the ones named."""

    for name, spine in axis.spines.items():
        spine.set_visible(name in keep)
