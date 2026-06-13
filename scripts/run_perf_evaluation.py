#!/usr/bin/env python3
"""Replay performance evaluation over larger standard and pooled L3 corpora.

This orchestrator wires together the existing, single-source-of-truth pieces:

1. deterministic corpus generation with ``scripts/generate_synthetic_events.py``
   (kept out of git under an ignored corpus directory, default
   ``build/perf_corpora/``);
2. the ``asterion_benchmarks`` hot-path runner, which emits *both* the standard
   and the opt-in pooled L3 hot-path rows in a single ``--only-hot-path`` pass
   (binary replay -> L3 update -> reusable L2 view -> strategy callback -> risk
   check);
3. a per-corpus comparison plus a compact manifest and summary written under an
   ignored results directory (default ``build/perf_results/``). The optional
   multi-symbol corpus is generated and recorded, but skipped by the hot-path
   benchmark because that benchmark is intentionally single-symbol.

It records, for every corpus: mode, seed, event count, symbol count, the
generated-file SHA-256 checksum, the exact generation command and the CSV/binary
format. For every benchmark it records warm-up/measured iterations, the
p50/p95/p99/p99.9/max latency distribution, throughput, allocation count and
bytes, the deterministic guard checksum, the standard-vs-pooled checksum parity
and whether the pooled path stayed allocation-free in steady state.

These are representative measurements on the machine/environment that runs the
script, not portable performance claims. Nothing here implies live exchange
connectivity, production-HFT performance or profitability. Generated corpora are
deterministic stress workloads, not market-alpha evidence. Large corpora are
never committed; only this script and small CI fixtures are tracked.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import shlex
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional

REPO_ROOT = Path(__file__).resolve().parents[1]
GENERATOR = REPO_ROOT / "scripts" / "generate_synthetic_events.py"
DEFAULT_BUILD_DIR = REPO_ROOT / "build"
DEFAULT_CORPUS_DIR = REPO_ROOT / "build" / "perf_corpora"
DEFAULT_RESULTS_DIR = REPO_ROOT / "build" / "perf_results"

STANDARD_ROW = "hot_path_binary_replay_l3_l2_strategy_risk"
POOLED_ROW = "hot_path_binary_replay_pooled_l3_l2_strategy_risk"


@dataclass
class Dataset:
    name: str
    mode: str
    events: int
    seed: int
    symbols: int = 1
    symbol_base: int = 1
    note: str = ""
    benchmark_compatible: bool = True
    skip_reason: str = ""


# Local/manual performance corpus matrix. Seeds are explicit and fixed so every
# corpus is byte-for-byte reproducible. The 100k baseline is a fast smoke size;
# the 1M variants are the larger workloads. The multi-symbol corpus is recorded
# for completeness but skipped by the hot-path benchmark because the pooled hot
# path is single-symbol by construction.
DATASETS: List[Dataset] = [
    Dataset("baseline_100k", "balanced", 100_000, 2001,
            note="100k baseline balanced events"),
    Dataset("baseline_1m", "balanced", 1_000_000, 2002,
            note="1M baseline balanced events"),
    Dataset("high_cancellation_1m", "high-cancel", 1_000_000, 2003,
            note="1M high-cancellation events"),
    Dataset("replace_heavy_1m", "replace-heavy", 1_000_000, 2004,
            note="1M replace-heavy events"),
    Dataset("deep_book_1m", "deep-book", 1_000_000, 2005,
            note="1M deep-book events"),
    Dataset("bursty_1m", "bursty", 1_000_000, 2006,
            note="1M bursty events"),
    Dataset("wide_price_range_1m", "wide-price-range", 1_000_000, 2007,
            note="1M wide-price-range events"),
    Dataset("multi_symbol_grouped_1m", "multi-symbol", 1_000_000, 2008, symbols=8,
            note="optional multi-symbol-style grouped corpus",
            benchmark_compatible=False,
            skip_reason="single-symbol hot-path benchmark is not a multi-symbol router"),
]


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def run(cmd: List[str], **kwargs) -> subprocess.CompletedProcess:
    print("+", " ".join(str(c) for c in cmd), flush=True)
    return subprocess.run(cmd, check=True, **kwargs)


def command_to_string(cmd: List[str]) -> str:
    if sys.platform == "win32":
        return subprocess.list2cmdline(cmd)
    return shlex.join(cmd)


def generation_command(dataset: Dataset, out: Path, fmt: str, events: int) -> List[str]:
    return [
        sys.executable,
        str(GENERATOR),
        "--mode", dataset.mode,
        "--events", str(events),
        "--seed", str(dataset.seed),
        "--symbol", str(dataset.symbol_base),
        "--symbols", str(dataset.symbols),
        "--output", str(out),
        "--format", fmt,
    ]


def generate_corpus(dataset: Dataset, corpus_dir: Path, fmt: str, events: int,
                    skip_existing: bool) -> Dict:
    corpus_dir.mkdir(parents=True, exist_ok=True)
    suffix = ".bin" if fmt == "binary" else ".csv"
    out = corpus_dir / f"{dataset.name}{suffix}"
    cmd = generation_command(dataset, out, fmt, events)
    if not (skip_existing and out.exists()):
        run(cmd)
    checksum = sha256_file(out)
    return {
        "dataset": dataset.name,
        "mode": dataset.mode,
        "seed": dataset.seed,
        "event_count": events,
        "symbol_count": dataset.symbols,
        "format": fmt,
        "path": str(out),
        "size_bytes": out.stat().st_size,
        "sha256": checksum,
        "generation_command": command_to_string(cmd),
        "note": dataset.note,
    }


def benchmark_corpus(corpus_path: Path, build_dir: Path, results_dir: Path,
                     name: str, warmup: int, measure: int) -> dict:
    results_dir.mkdir(parents=True, exist_ok=True)
    bench_bin = build_dir / "asterion_benchmarks"
    if not bench_bin.exists():
        bench_bin = build_dir / "asterion_benchmarks.exe"
    if not bench_bin.exists():
        raise SystemExit(
            f"benchmark binary not found in {build_dir}; build asterion_benchmarks first")
    out_json = results_dir / f"{name}.benchmark.json"
    cmd = [
        str(bench_bin),
        "--dataset", str(corpus_path),
        "--only-hot-path",
        "--hot-path-warmup", str(warmup),
        "--hot-path-iterations", str(measure),
        "--json", str(out_json),
        "--no-text",
    ]
    run(cmd)
    return json.loads(out_json.read_text(encoding="utf-8"))


def _row(result: dict, name: str) -> Optional[dict]:
    for b in result.get("benchmarks", []):
        if b.get("name") == name:
            return b
    return None


def _metrics(row: Optional[dict]) -> Optional[dict]:
    if not row:
        return None
    alloc = row.get("allocations", 0)
    if isinstance(alloc, dict):
        allocation_count = alloc.get("count")
        allocation_bytes = alloc.get("bytes")
    else:
        allocation_count = alloc
        allocation_bytes = row.get("bytes_allocated")
    return {
        "warmup_iterations": row.get("warmup_iterations"),
        "measured_iterations": row.get("measured_iterations"),
        "measured_event_count": row.get("event_count"),
        "p50_ns": row.get("p50_ns"),
        "p95_ns": row.get("p95_ns"),
        "p99_ns": row.get("p99_ns"),
        "p99_9_ns": row.get("p99_9_ns", row.get("p999_ns")),
        "max_ns": row.get("max_ns"),
        "avg_ns": row.get("avg_ns"),
        "throughput_events_per_sec": row.get(
            "throughput_events_per_sec", row.get("throughput_events_per_second")),
        "allocation_count": allocation_count,
        "deallocation_count": row.get("deallocations"),
        "allocation_bytes": allocation_bytes,
        "guard_checksum": row.get("guard_checksum", row.get("guard")),
    }


def summarise(corpus_meta: dict, result: dict) -> dict:
    standard = _metrics(_row(result, STANDARD_ROW))
    pooled = _metrics(_row(result, POOLED_ROW))
    parity = None
    if standard and pooled:
        parity = standard["guard_checksum"] == pooled["guard_checksum"]
    pooled_alloc_free = None
    if pooled is not None and pooled["allocation_count"] is not None:
        pooled_alloc_free = (
            pooled["allocation_count"] == 0 and
            (pooled["allocation_bytes"] in (0, None))
        )
    return {
        "corpus": corpus_meta,
        "standard": standard,
        "pooled": pooled,
        "checksum_parity": parity,
        "pooled_steady_state_allocation_free": pooled_alloc_free,
    }


def environment(result: dict) -> dict:
    env = dict(result.get("environment", {}))
    env["python_version"] = platform.python_version()
    env["host_platform"] = platform.platform()
    return env


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR)
    p.add_argument("--corpus-dir", type=Path, default=DEFAULT_CORPUS_DIR)
    p.add_argument("--results-dir", type=Path, default=DEFAULT_RESULTS_DIR)
    p.add_argument("--warmup", type=int, default=5)
    p.add_argument("--measure", type=int, default=30)
    p.add_argument("--format", choices=("binary", "csv"), default="binary")
    p.add_argument("--datasets", nargs="*", help="optional subset of dataset names")
    p.add_argument("--events-cap", type=int, default=None,
                   help="cap each corpus to at most this many events (faster local runs)")
    p.add_argument("--skip-existing", action="store_true",
                   help="reuse an already-generated corpus file if present")
    p.add_argument("--summary-name", default="perf_evaluation_summary.json")
    p.add_argument("--manifest-name", default="corpus_manifest.json")
    p.add_argument("--list", action="store_true", help="list datasets and exit")
    return p.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)

    if args.list:
        for d in DATASETS:
            print(f"{d.name:24s} mode={d.mode:18s} events={d.events:>9d} "
                  f"seed={d.seed} symbols={d.symbols}  # {d.note}")
        return 0

    selected = DATASETS
    if args.datasets:
        wanted = set(args.datasets)
        unknown = wanted - {d.name for d in DATASETS}
        if unknown:
            raise SystemExit(f"unknown datasets: {sorted(unknown)}")
        selected = [d for d in DATASETS if d.name in wanted]

    manifest: List[dict] = []
    summary_entries: List[dict] = []
    skipped_entries: List[dict] = []
    env: Dict = {}

    for dataset in selected:
        events = dataset.events
        if args.events_cap is not None:
            events = min(events, args.events_cap)
        corpus_meta = generate_corpus(dataset, args.corpus_dir, args.format, events,
                                      args.skip_existing)
        manifest.append(corpus_meta)
        if not dataset.benchmark_compatible:
            skipped = {
                "corpus": corpus_meta,
                "reason": dataset.skip_reason,
            }
            skipped_entries.append(skipped)
            print(f"  {dataset.name}: generated only ({dataset.skip_reason})")
            continue
        result = benchmark_corpus(Path(corpus_meta["path"]), args.build_dir,
                                  args.results_dir, dataset.name, args.warmup,
                                  args.measure)
        if not env:
            env = environment(result)
        entry = summarise(corpus_meta, result)
        summary_entries.append(entry)
        _print_entry(entry)

    args.results_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = args.results_dir / args.manifest_name
    manifest_path.write_text(json.dumps(
        {"schema_version": 1, "corpora": manifest}, indent=2) + "\n",
        encoding="utf-8")
    summary_path = args.results_dir / args.summary_name
    summary_path.write_text(json.dumps({
        "schema_version": 1,
        "environment": env,
        "warmup_iterations": args.warmup,
        "measured_iterations": args.measure,
        "datasets": summary_entries,
        "skipped": skipped_entries,
    }, indent=2) + "\n", encoding="utf-8")
    print(f"\nmanifest written to {manifest_path}")
    print(f"summary written to {summary_path}")
    print(_markdown_table(summary_entries))
    return 0


def _fmt(v) -> str:
    if v is None:
        return "n/a"
    if isinstance(v, float):
        return f"{v:,.0f}"
    return str(v)


def _print_entry(entry: dict) -> None:
    c = entry["corpus"]
    s = entry["standard"] or {}
    p = entry["pooled"] or {}
    print(f"  {c['dataset']}: events={c['event_count']} sha256={c['sha256'][:12]}")
    print(f"    standard p50={_fmt(s.get('p50_ns'))}ns alloc={_fmt(s.get('allocation_count'))} "
          f"bytes={_fmt(s.get('allocation_bytes'))}")
    print(f"    pooled   p50={_fmt(p.get('p50_ns'))}ns alloc={_fmt(p.get('allocation_count'))} "
          f"bytes={_fmt(p.get('allocation_bytes'))}")
    print(f"    checksum_parity={entry['checksum_parity']} "
          f"pooled_alloc_free={entry['pooled_steady_state_allocation_free']}")


def _markdown_table(entries: List[dict]) -> str:
    lines = [
        "",
        "| corpus | events | path | p50 ns | p99 ns | max ns | thrpt evt/s | alloc cnt | alloc bytes |",
        "| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for e in entries:
        c = e["corpus"]
        for label, m in (("standard", e["standard"]), ("pooled", e["pooled"])):
            m = m or {}
            lines.append(
                f"| {c['dataset']} | {c['event_count']} | {label} | "
                f"{_fmt(m.get('p50_ns'))} | {_fmt(m.get('p99_ns'))} | {_fmt(m.get('max_ns'))} | "
                f"{_fmt(m.get('throughput_events_per_sec'))} | "
                f"{_fmt(m.get('allocation_count'))} | {_fmt(m.get('allocation_bytes'))} |")
    return "\n".join(lines)


if __name__ == "__main__":
    raise SystemExit(main())
