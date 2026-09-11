"""Capture unauthenticated Binance REST depth snapshots as JSONL replay fixtures.

Polls the public endpoint ``GET /api/v3/depth`` (no key, no signature) at a
bounded interval with capped exponential backoff, and stops on a duration or
message limit. Connection errors, timeouts, malformed payloads and Ctrl-C are
handled without losing the capture metadata. The standard library only.

Each response is written as one JSONL line inside an envelope carrying the symbol
and capture time, which the REST depth response omits::

    {"_captured_at_ns": <ns>, "symbol": "BTCUSDT",
     "endpoint": "https://api.binance.com/api/v3/depth?symbol=BTCUSDT&limit=20",
     "data": {"lastUpdateId": <id>, "bids": [[p, q], ...], "asks": [[p, q], ...]}}

The first line is a ``_meta`` header (symbol, capture window, source, stream type,
tool version, git commit) and the same fields are written to a sidecar
``<output>.meta.json``. Feed the result to
``scripts/normalise_binance_depth_to_asterion.py``.

Capture is manual and requires network access; importing this module performs no
network I/O.

    python scripts/capture_binance_depth.py --symbol BTCUSDT --duration 20 \
        --max-events 40 --output data/captures/btcusdt_depth.raw.jsonl
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

TOOL_VERSION = "1.0.0"
DEFAULT_BASE_URL = "https://api.binance.com"
DEPTH_PATH = "/api/v3/depth"
STREAM_TYPE = "rest_depth_snapshot_poll"
USER_AGENT = f"asterion-capture-binance-depth/{TOOL_VERSION} (+public-market-data)"

# Client-side pacing limits.
MIN_INTERVAL_SECONDS = 0.5
MAX_BACKOFF_SECONDS = 30.0


def _git_commit() -> str:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--short=12", "HEAD"],
            capture_output=True,
            text=True,
            check=False,
            cwd=str(Path(__file__).resolve().parent.parent),
        )
        if result.returncode == 0:
            return result.stdout.strip()
    except (OSError, ValueError):
        pass
    return "unknown"


def _now_ns() -> int:
    return time.time_ns()


def fetch_depth(base_url: str, symbol: str, limit: int, timeout: float) -> tuple[str, dict]:
    """Fetch one depth snapshot. Returns (endpoint, payload)."""
    query = urllib.parse.urlencode({"symbol": symbol.upper(), "limit": limit})
    endpoint = f"{base_url}{DEPTH_PATH}?{query}"
    request = urllib.request.Request(endpoint, headers={"User-Agent": USER_AGENT}, method="GET")
    with urllib.request.urlopen(request, timeout=timeout) as response:
        raw = response.read().decode("utf-8")
    payload = json.loads(raw)
    if not isinstance(payload, dict):
        raise ValueError("unexpected depth payload (not a JSON object)")
    return endpoint, payload


def _write_meta(meta_path: Path, meta: dict) -> None:
    meta_path.write_text(json.dumps(meta, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def capture(
    symbol: str,
    duration: float,
    max_events: int,
    output: Path,
    *,
    base_url: str = DEFAULT_BASE_URL,
    interval: float = 1.0,
    limit: int = 20,
    timeout: float = 10.0,
) -> int:
    """Poll the depth endpoint and append envelopes to ``output``.

    Returns the number of captured messages. Transient network, HTTP and JSON
    errors back off and retry; Ctrl-C stops cleanly and still finalises metadata.
    """
    symbol = symbol.upper()
    interval = max(interval, MIN_INTERVAL_SECONDS)
    output.parent.mkdir(parents=True, exist_ok=True)
    meta_path = output.with_suffix(output.suffix + ".meta.json")

    started_ns = _now_ns()
    deadline = time.monotonic() + duration if duration > 0 else None
    captured = 0
    errors = 0
    backoff = interval

    base_meta = {
        "_meta": "Binance public REST depth capture (GET /api/v3/depth, unauthenticated).",
        "symbol": symbol,
        "source": base_url,
        "endpoint_path": DEPTH_PATH,
        "stream_type": STREAM_TYPE,
        "depth_limit": limit,
        "tool": "capture_binance_depth.py",
        "tool_version": TOOL_VERSION,
        "git_commit": _git_commit(),
        "capture_start_ns": started_ns,
        "capture_start_iso": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(started_ns / 1e9)),
    }

    print(f"capturing depth for {symbol} from {base_url}{DEPTH_PATH} (limit={limit})", file=sys.stderr)

    try:
        with output.open("w", encoding="utf-8", newline="\n") as handle:
            handle.write(json.dumps(base_meta, sort_keys=True) + "\n")
            handle.flush()
            while True:
                if max_events > 0 and captured >= max_events:
                    break
                if deadline is not None and time.monotonic() >= deadline:
                    break
                try:
                    endpoint, payload = fetch_depth(base_url, symbol, limit, timeout)
                except KeyboardInterrupt:
                    raise
                except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError, OSError, ValueError) as exc:
                    errors += 1
                    backoff = min(backoff * 2, MAX_BACKOFF_SECONDS)
                    print(f"warning: fetch failed ({exc}); backing off {backoff:.1f}s", file=sys.stderr)
                    time.sleep(backoff)
                    continue

                envelope = {
                    "_captured_at_ns": _now_ns(),
                    "symbol": symbol,
                    "endpoint": endpoint,
                    "data": payload,
                }
                handle.write(json.dumps(envelope, sort_keys=True) + "\n")
                handle.flush()
                captured += 1
                backoff = interval
                time.sleep(interval)
    except KeyboardInterrupt:
        print("\ninterrupted; finalising capture metadata", file=sys.stderr)

    finished_ns = _now_ns()
    meta = dict(base_meta)
    meta.update(
        {
            "capture_end_ns": finished_ns,
            "capture_end_iso": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(finished_ns / 1e9)),
            "captured_messages": captured,
            "error_count": errors,
            "output": str(output),
        }
    )
    _write_meta(meta_path, meta)
    print(f"captured_messages={captured} errors={errors}", file=sys.stderr)
    print(f"output={output}", file=sys.stderr)
    print(f"meta={meta_path}", file=sys.stderr)
    return captured


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--symbol", default="BTCUSDT", help="symbol (default BTCUSDT)")
    parser.add_argument("--duration", type=float, default=20.0, help="capture duration seconds (0 = no limit)")
    parser.add_argument("--max-events", type=int, default=40, help="max messages to capture (0 = no limit)")
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("data/captures/binance_depth_capture.raw.jsonl"),
        help="raw JSONL output path",
    )
    parser.add_argument("--interval", type=float, default=1.0, help=f"seconds between polls (min {MIN_INTERVAL_SECONDS})")
    parser.add_argument("--limit", type=int, default=20, help="order-book depth levels per side")
    parser.add_argument("--timeout", type=float, default=10.0, help="per-request timeout seconds")
    parser.add_argument("--base-url", default=DEFAULT_BASE_URL, help="REST base URL")
    args = parser.parse_args(argv)

    if args.max_events <= 0 and args.duration <= 0:
        parser.error("set a positive --duration or --max-events so capture terminates")

    try:
        capture(
            symbol=args.symbol,
            duration=args.duration,
            max_events=args.max_events,
            output=args.output,
            base_url=args.base_url,
            interval=args.interval,
            limit=args.limit,
            timeout=args.timeout,
        )
    except KeyboardInterrupt:
        return 130
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
