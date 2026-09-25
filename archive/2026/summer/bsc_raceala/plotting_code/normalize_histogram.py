#!/usr/bin/env python3
"""
Normalize an IPC histo to get rid of the clock_gettime() overhead
To modify: INPUT and OUTPUT files, potentially the target
"""

import sys
from pathlib import Path

import numpy as np


INPUT = "../measurements/ipc/histo"
OUTPUT = "../measurements/ipc/histo_norm"

TARGET_MEAN_NS = 3260.0   

BUCKET_WIDTH = 25.0       



def load_histogram(path: Path) -> np.ndarray:
    counts: list = []
    for lineno, raw in enumerate(path.read_text().splitlines(), start=1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        try:
            value = float(line)
        except ValueError:
            raise SystemExit(f"{path}:{lineno}: not a number: {raw!r}")
        if value < 0:
            raise SystemExit(f"{path}:{lineno}: negative count: {value}")
        if value != int(value):
            raise SystemExit(f"{path}:{lineno}: not a whole count: {value}")
        counts.append(int(value))
    if not counts:
        raise SystemExit(f"{path}: no bucket counts found")
    return np.asarray(counts, dtype=np.int64)


def mean_of(counts: np.ndarray) -> float:
    total = float(counts.sum())
    if total <= 0:
        raise SystemExit("histogram is empty (all buckets are zero)")
    centers = (np.arange(len(counts), dtype=np.float64) + 0.5) * BUCKET_WIDTH
    return float(np.dot(counts, centers) / total)


def percentile(counts: np.ndarray, q: float) -> float:
    total = float(counts.sum())
    cum = np.cumsum(counts)
    target = q / 100.0 * total
    i = int(np.searchsorted(cum, target, side="left"))
    i = min(i, len(counts) - 1)
    below = cum[i - 1] if i > 0 else 0.0
    frac = (target - below) / counts[i] if counts[i] > 0 else 0.0
    return (i + min(max(frac, 0.0), 1.0)) * BUCKET_WIDTH


def translate(counts: np.ndarray, buckets: int) -> np.ndarray:
    if buckets == 0:
        return counts.copy()
    if buckets > 0:
        return np.concatenate([np.zeros(buckets, dtype=np.int64), counts])

    k = -buckets
    if k >= len(counts):
        raise SystemExit("the target mean would move the whole histogram "
                         "below zero")
    lost = int(counts[:k].sum())
    out = counts[k:].copy()
    if lost:
        print(f"warning: {lost:,} samples fall below zero and were piled "
              f"into bucket 0; the achieved mean will not match the target",
              file=sys.stderr)
        out[0] += lost
    return out


def main() -> None:
    counts = load_histogram(Path(INPUT))
    current = mean_of(counts)

    delta = TARGET_MEAN_NS - current
    buckets = int(round(delta / BUCKET_WIDTH))
    applied = buckets * BUCKET_WIDTH

    out = translate(counts, buckets)
    nz = np.flatnonzero(out)
    out = out[: nz[-1] + 1] if nz.size else out[:1]

    achieved = mean_of(out)
    residual = achieved - TARGET_MEAN_NS

    Path(OUTPUT).write_text("\n".join(str(int(c)) for c in out) + "\n")

    print(f"\n  wrote {OUTPUT}")
    print(f"  target    {TARGET_MEAN_NS/1e3:,.3f} us")
    print(f"  mean      {current/1e3:,.3f} -> {achieved/1e3:,.3f} us")
    print(f"  shift     {applied:+,.0f} ns ({buckets:+d} buckets); "
          f"exact would be {delta:+,.1f} ns")
    print(f"  residual  {residual:+,.1f} ns "
          f"(one bucket = {BUCKET_WIDTH:g} ns)")
    print(f"  samples   {counts.sum():,} -> {out.sum():,}")
    print(f"  p50       {percentile(counts, 50)/1e3:,.3f} -> "
          f"{percentile(out, 50)/1e3:,.3f} us")
    print(f"  p99       {percentile(counts, 99)/1e3:,.3f} -> "
          f"{percentile(out, 99)/1e3:,.3f} us\n")


if __name__ == "__main__":
    main()
