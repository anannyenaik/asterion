#!/usr/bin/env python3
"""Regenerate the publication figures from committed result files.

Each plotting function writes an opaque 200 DPI PNG for the README and a vector
PDF with fixed metadata. No measurements are performed here.

    python figures/generate_figures.py
    python figures/generate_figures.py --check
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
import style

ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "results"
FIGURES = Path(__file__).resolve().parent

PERCENTILES = ("p50_ns", "p95_ns", "p99_ns", "p999_ns")
PERCENTILE_LABELS = ("p50", "p95", "p99", "p99.9")
PNG_DPI = 200
PNG_METADATA = {"Software": None}
PDF_AUTHOR = "Anannye Naik"


def read_csv(name: str) -> list[dict[str, str]]:
    with (RESULTS / name).open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def short_corpus(name: str) -> str:
    return name.removeprefix("stress_").removesuffix("_1m").replace("_", " ")


def pdf_metadata(title: str) -> dict[str, str | None]:
    return {
        "Title": title,
        "Author": PDF_AUTHOR,
        "Creator": "figures/generate_figures.py",
        "Producer": "matplotlib",
        "Subject": "Derived from committed measurements under results/",
        "Keywords": "order book, deterministic replay, latency, allocation",
        "CreationDate": None,
        "ModDate": None,
    }


def save(figure: plt.Figure, stem: str, title: str) -> tuple[Path, ...]:
    """Write a single Matplotlib figure as vector PDF and README PNG."""

    figure.set_layout_engine("constrained")
    pdf_path = FIGURES / f"{stem}.pdf"
    png_path = FIGURES / f"{stem}.png"
    figure.savefig(pdf_path, format="pdf", metadata=pdf_metadata(title))
    figure.savefig(
        png_path,
        format="png",
        dpi=PNG_DPI,
        facecolor="white",
        edgecolor="none",
        transparent=False,
        metadata=PNG_METADATA,
    )
    plt.close(figure)
    return pdf_path, png_path


def figure_pooled_allocation_effect() -> tuple[Path, ...]:
    """Show paired latency effects and the corresponding allocation change."""

    rows = read_csv("stress_corpora_standard_vs_pooled.csv")
    rows.sort(key=lambda row: int(row["pooled_p99_ns"]) - int(row["standard_p99_ns"]))

    labels = [short_corpus(row["corpus"]) for row in rows]
    p50_deltas = [int(row["pooled_p50_ns"]) - int(row["standard_p50_ns"]) for row in rows]
    p99_deltas = [int(row["pooled_p99_ns"]) - int(row["standard_p99_ns"]) for row in rows]
    standard_allocations_per_event = [
        int(row["standard_allocations"]) / int(row["measured_events"]) for row in rows
    ]
    positions = list(range(len(rows)))

    assert all(int(row["pooled_allocations"]) == 0 for row in rows)
    assert all(row["guard_parity"] == "true" for row in rows)

    figure, (axis, allocation_axis) = plt.subplots(
        1,
        2,
        figsize=(7.6, 3.4),
        sharey=True,
        gridspec_kw={"width_ratios": [4.5, 1.35], "wspace": 0.04},
    )

    axis.axvline(0, color=style.MUTED, linewidth=0.9, zorder=0)
    axis.scatter(p50_deltas, positions, color=style.STANDARD, marker="o", s=30, label="p50")
    axis.scatter(p99_deltas, positions, color=style.POOLED, marker="D", s=27, label="p99")
    axis.set_yticks(positions, labels)
    axis.invert_yaxis()
    axis.set_xlabel("Pooled - standard latency (ns)")
    axis.set_xlim(-90, 130)
    axis.xaxis.grid(True, linestyle=":", zorder=0)
    axis.set_axisbelow(True)
    axis.legend(loc="lower right", ncol=2, handletextpad=0.35, columnspacing=1.0)
    style.strip_spines(axis, keep=("left", "bottom"))

    allocation_axis.set_title("Allocations/event\nstandard to pooled", pad=7)
    allocation_axis.set_xlim(0, 1)
    allocation_axis.set_xticks([])
    allocation_axis.tick_params(axis="y", left=False, labelleft=False)
    for y, value in zip(positions, standard_allocations_per_event, strict=True):
        allocation_axis.text(
            0.5,
            y,
            f"{value:.2f}  to  0",
            ha="center",
            va="center",
            fontsize=style.ANNOTATION_FONT_PT,
            color=style.TEXT,
        )
    style.strip_spines(allocation_axis, keep=())

    return save(
        figure,
        "pooled_allocation_effect",
        "Pooled minus standard latency across deterministic stress corpora",
    )


def figure_hot_path_latency_percentiles() -> tuple[Path, ...]:
    """Compare the percentile profiles and their paired differences."""

    rows = read_csv("hot_path_1m_standard_vs_pooled.csv")
    by_variant = {row["book_variant"]: row for row in rows}
    standard = [int(by_variant["standard"][percentile]) for percentile in PERCENTILES]
    pooled = [int(by_variant["pooled"][percentile]) for percentile in PERCENTILES]
    deltas = [
        pooled_value - standard_value
        for standard_value, pooled_value in zip(standard, pooled, strict=True)
    ]
    assert deltas == [10, -10, 50, 20]

    x = list(range(len(PERCENTILES)))
    figure, (profile_axis, delta_axis) = plt.subplots(
        2,
        1,
        figsize=(5.7, 4.2),
        sharex=True,
        gridspec_kw={"height_ratios": [2.1, 1.0]},
    )

    profile_axis.plot(
        x,
        standard,
        color=style.STANDARD,
        linewidth=1.5,
        marker="o",
        markersize=4.5,
        label="standard",
    )
    profile_axis.plot(
        x,
        pooled,
        color=style.POOLED,
        linewidth=1.5,
        marker="D",
        markersize=4.2,
        label="pooled",
    )
    profile_axis.set_title("A  Measured event latency", loc="left")
    profile_axis.set_ylabel("latency (ns)")
    profile_axis.yaxis.grid(True, linestyle=":", zorder=0)
    profile_axis.set_axisbelow(True)
    profile_axis.legend(loc="upper left", ncol=2, handletextpad=0.4, columnspacing=1.1)
    style.strip_spines(profile_axis)

    delta_axis.axhline(0, color=style.MUTED, linewidth=0.9, zorder=0)
    delta_axis.plot(x, deltas, color=style.POOLED, linewidth=1.2, marker="o", markersize=4.5)
    for percentile_x, delta in zip(x, deltas, strict=True):
        delta_axis.annotate(
            f"{delta:+d} ns",
            xy=(percentile_x, delta),
            xytext=(0, 7 if delta >= 0 else -13),
            textcoords="offset points",
            ha="center",
            fontsize=style.ANNOTATION_FONT_PT,
            color=style.POOLED,
        )
    delta_axis.set_title("B  Paired difference", loc="left")
    delta_axis.set_ylabel("pooled - standard (ns)")
    delta_axis.set_ylim(-65, 65)
    delta_axis.set_xticks(x, PERCENTILE_LABELS)
    delta_axis.set_xlabel("latency percentile")
    delta_axis.yaxis.grid(True, linestyle=":", zorder=0)
    delta_axis.set_axisbelow(True)
    style.strip_spines(delta_axis)

    return save(
        figure,
        "hot_path_latency_percentiles",
        "High-cancellation hot-path latency percentiles and paired differences",
    )


def figure_inference_loop_cost() -> tuple[Path, ...]:
    """Separate complete event-loop integration from component measurements."""

    rows = read_csv("inference_replay_loop.csv")
    event_rows = {row["variant"]: row for row in rows if row["timing_mode"] == "per-event"}
    component_rows = {
        row["benchmark"]: row for row in rows if row["timing_mode"] == "per-call"
    }

    no_inference = event_rows["standard_inference_free"]
    linear_inference = event_rows["standard_linear_inference"]
    standard = [int(no_inference[percentile]) for percentile in PERCENTILES]
    linear = [int(linear_inference[percentile]) for percentile in PERCENTILES]
    assert linear[0] - standard[0] == 140
    assert int(linear_inference["allocations"]) == int(no_inference["allocations"])
    assert linear_inference["guard"] != no_inference["guard"]

    event_positions = list(range(len(PERCENTILES)))
    figure, (event_axis, component_axis) = plt.subplots(
        2,
        1,
        figsize=(6.4, 5.3),
        gridspec_kw={"height_ratios": [1.2, 1.0]},
    )

    for y, (base, measured) in enumerate(zip(standard, linear, strict=True)):
        event_axis.plot([base, measured], [y, y], color=style.GRID, linewidth=1.4, zorder=1)
        event_axis.text(
            (base + measured) / 2,
            y - 0.16,
            f"+{measured - base} ns",
            ha="center",
            va="top",
            fontsize=style.ANNOTATION_FONT_PT,
            color=style.MUTED,
        )
    event_axis.scatter(
        standard,
        event_positions,
        color=style.STANDARD,
        marker="o",
        s=30,
        label="no inference",
        zorder=2,
    )
    event_axis.scatter(
        linear,
        event_positions,
        color=style.POOLED,
        marker="D",
        s=27,
        label="LinearModel",
        zorder=2,
    )
    event_axis.set_title("A  Event-loop integration", loc="left")
    event_axis.set_yticks(event_positions, PERCENTILE_LABELS)
    event_axis.invert_yaxis()
    event_axis.set_xlabel("event latency (ns)")
    event_axis.xaxis.grid(True, linestyle=":", zorder=0)
    event_axis.set_axisbelow(True)
    event_axis.legend(loc="upper right", handletextpad=0.35)
    style.strip_spines(event_axis)

    component_order = (
        "feature_extraction_caller_owned_buffer",
        "linear_inference_only",
        "feature_buffer_measured_linear_inference",
    )
    component_labels = (
        "feature extraction,\ncaller-owned buffer",
        "linear inference only",
        "feature buffer +\nmeasured linear inference",
    )
    component_p50 = [int(component_rows[name]["p50_ns"]) for name in component_order]
    component_p99 = [int(component_rows[name]["p99_ns"]) for name in component_order]
    component_positions = list(range(len(component_order)))
    component_axis.scatter(
        component_p50,
        component_positions,
        color=style.STANDARD,
        marker="o",
        s=30,
        label="p50",
    )
    component_axis.scatter(
        component_p99,
        component_positions,
        color=style.POOLED,
        marker="D",
        s=27,
        label="p99",
    )
    for y, (p50, p99) in enumerate(zip(component_p50, component_p99, strict=True)):
        component_axis.annotate(
            str(p50),
            xy=(p50, y),
            xytext=(-5, 7),
            textcoords="offset points",
            ha="right",
            fontsize=style.ANNOTATION_FONT_PT,
            color=style.STANDARD,
        )
        component_axis.annotate(
            str(p99),
            xy=(p99, y),
            xytext=(5, -3),
            textcoords="offset points",
            fontsize=style.ANNOTATION_FONT_PT,
            color=style.POOLED,
        )
    component_axis.set_title("B  Component measurements", loc="left")
    component_axis.set_yticks(component_positions, component_labels)
    component_axis.invert_yaxis()
    component_axis.set_xlabel("component latency (ns)")
    component_axis.set_xlim(0, 170)
    component_axis.xaxis.grid(True, linestyle=":", zorder=0)
    component_axis.set_axisbelow(True)
    component_axis.legend(loc="upper right", ncol=2, handletextpad=0.35, columnspacing=1.0)
    style.strip_spines(component_axis)

    return save(
        figure,
        "inference_loop_cost",
        "LinearModel event-loop integration and isolated component latency",
    )


def figure_spsc_throughput_parity() -> tuple[Path, ...]:
    """Show the SPSC throughput ratio against the single-thread baseline."""

    rows = read_csv("spsc_steady_state_1m.csv")
    rows.sort(key=lambda row: float(row["spsc_to_single_ratio"]))
    labels = [short_corpus(row["corpus"]) for row in rows]
    ratios = [float(row["spsc_to_single_ratio"]) for row in rows]
    positions = list(range(len(rows)))

    assert all(row["checksum_parity"] == "true" for row in rows)
    assert all(int(row["dropped_events"]) == 0 for row in rows)
    assert all(int(row["max_queue_depth"]) == int(row["queue_capacity"]) for row in rows)

    figure, axis = plt.subplots(figsize=(6.2, 3.0))
    axis.axvline(1.0, color=style.MUTED, linewidth=0.9, zorder=0)
    axis.scatter(ratios, positions, color=style.ALTERNATE, marker="o", s=34, zorder=2)
    for y, ratio in zip(positions, ratios, strict=True):
        axis.annotate(
            f"{ratio:.2f}\u00d7",
            xy=(ratio, y),
            xytext=(7, -3),
            textcoords="offset points",
            fontsize=style.ANNOTATION_FONT_PT,
            color=style.ALTERNATE,
        )
    axis.set_yticks(positions, labels)
    axis.invert_yaxis()
    axis.set_xlabel("SPSC / single-thread throughput ratio")
    axis.set_xlim(0.64, 1.34)
    axis.xaxis.grid(True, linestyle=":", zorder=0)
    axis.set_axisbelow(True)
    style.strip_spines(axis)

    return save(
        figure,
        "spsc_throughput_parity",
        "SPSC to single-thread throughput ratio across deterministic corpora",
    )


BUILDERS = (
    figure_pooled_allocation_effect,
    figure_hot_path_latency_percentiles,
    figure_inference_loop_cost,
    figure_spsc_throughput_parity,
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="Regenerate and fail if any committed figure was stale.",
    )
    args = parser.parse_args()
    style.apply()

    if args.check:
        before = {
            path.name: hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted([*FIGURES.glob("*.pdf"), *FIGURES.glob("*.png")])
        }

    written = [path for build in BUILDERS for path in build()]

    if args.check:
        stale = [
            path.name
            for path in written
            if before.get(path.name) != hashlib.sha256(path.read_bytes()).hexdigest()
        ]
        if stale:
            print("stale figures: " + ", ".join(sorted(stale)), file=sys.stderr)
            return 1
        print(f"{len(written)} figure files up to date")
        return 0

    for path in written:
        print(f"wrote {path.relative_to(ROOT).as_posix()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
