#!/usr/bin/env python3
"""
Multiple VPAC VM cost plot (section 6.5.1)
To modify: DIR (histograms location) 
"""

import sys
from pathlib import Path

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  
from matplotlib.ticker import FuncFormatter  


DIR = "../measurements/multiple_vpacs/histograms"                           
PATTERN = "64_{i}_vpacs.txt"         
POINTS = [1, 2, 3, 4, 5, 6, 7, 8]

OUT_FILES = ["vpacs_mean.pdf"]       


BUCKET_WIDTH = 25.0   

X_LABEL = "VPACs"
Y_LABEL = "mean latency"

BAR_WIDTH = 0.68
COLOR = "#4878A8"
EDGE = "#2F4F6F"

WIDTH = 4.6           
HEIGHT = 2.8          
FONTSIZE = 9.0        

UNIT_SCALE = 1e3                    
UNIT_LABEL = r"$\mu$s"

STYLE = {
    "font.family": "serif",
    "font.serif": ["cmr10", "Computer Modern Roman", "STIX Two Text",
                   "DejaVu Serif"],
    "mathtext.fontset": "cm",
    "axes.formatter.use_mathtext": True,
    "axes.unicode_minus": False,
    "font.size": FONTSIZE,
    "axes.titlesize": FONTSIZE,
    "axes.labelsize": FONTSIZE,
    "xtick.labelsize": FONTSIZE - 1,
    "ytick.labelsize": FONTSIZE - 1,
    "legend.fontsize": FONTSIZE - 1,
    "axes.linewidth": 0.6,
    "xtick.major.width": 0.6,
    "ytick.major.width": 0.6,
    "xtick.major.size": 2.5,
    "ytick.major.size": 2.5,
    "xtick.direction": "out",
    "ytick.direction": "out",
    "axes.grid": True,
    "axes.grid.axis": "y",      
    "grid.color": "#CCCCCC",
    "grid.linewidth": 0.4,
    "grid.linestyle": "-",
    "axes.axisbelow": True,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "legend.frameon": False,
    "figure.dpi": 200,
    "savefig.dpi": 400,
    "savefig.bbox": "tight",
    "savefig.pad_inches": 0.02,
    "pdf.fonttype": 42,
    "ps.fonttype": 42,
    "pdf.compression": 6,
}


def load_histogram(path):
    counts = []
    for lineno, raw in enumerate(Path(path).read_text().splitlines(), start=1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        try:
            value = float(line)
        except ValueError:
            raise SystemExit(f"{path}:{lineno}: not a number: {raw!r}")
        if value < 0:
            raise SystemExit(f"{path}:{lineno}: negative count: {value}")
        counts.append(value)
    if not counts:
        raise SystemExit(f"{path}: no bucket counts found")
    return np.asarray(counts, dtype=np.float64)


def hist_stats(counts):
    total = float(counts.sum())
    if total <= 0:
        raise SystemExit("histogram is empty (all buckets are zero)")
    centers = (np.arange(len(counts), dtype=np.float64) + 0.5) * BUCKET_WIDTH
    return float(np.dot(counts, centers) / total), total



def fmt_val(v):
    if abs(v) >= 1000:
        return f"{v:,.0f}"
    if abs(v) >= 10:
        return f"{v:.1f}"
    return f"{v:.2f}"


def baseline_mean(data):
    for n, mean, _ in data:
        if n == 1:
            return mean
    raise SystemExit(
        f"need the 1-VPAC baseline ({PATTERN.format(i=1)}), "
        f"but only {[n for n, _, _ in data]} were found")


def plot(data):
    fig, ax = plt.subplots(figsize=(WIDTH, HEIGHT), layout="constrained")
    fig.get_layout_engine().set(w_pad=0.04, h_pad=0.04)

    ref = baseline_mean(data)
    x = [n for n, _, _ in data]
    y = [(m - ref) / UNIT_SCALE for _, m, _ in data]

    ax.bar(x, y, width=BAR_WIDTH, color=COLOR, edgecolor=EDGE, linewidth=0.5,
           zorder=3)

    ax.set_ylim(0.0, max(y) * 1.14)
    ax.set_xticks(POINTS)
    ax.set_xlim(min(POINTS) - 0.6, max(POINTS) + 0.6)
    ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:g}"))

    for xi, yi in zip(x, y):
        ax.annotate(f"{yi:+.2f}" if yi else f"{yi:.2f}",
                    xy=(xi, max(yi, 0.0)), xytext=(0, 2.5),
                    textcoords="offset points", ha="center", va="bottom",
                    fontsize=FONTSIZE - 2)

    ax.set_xlabel(X_LABEL)
    ax.set_ylabel(f"{Y_LABEL} increase over 1 VPAC [{UNIT_LABEL}]")
    return fig


def report(data):
    print(f"\n  mean latency in us, from {BUCKET_WIDTH:g} ns buckets\n")
    print(f"  {'VPACs':>6}  {'samples':>10}  {'mean':>9}")
    print(f"  {'-'*6}  {'-'*10}  {'-'*9}")
    for n, mean, total in data:
        print(f"  {n:>6}  {total:>10,.0f}  {fmt_val(mean/UNIT_SCALE):>9}")
    means = [m for _, m, _ in data]
    print(f"\n  spread: {fmt_val(min(means)/UNIT_SCALE)} to "
          f"{fmt_val(max(means)/UNIT_SCALE)} us "
          f"({100*(max(means)-min(means))/min(means):.1f}% of the lowest)")
    print(f"  plotted as the increase over 1 VPAC "
          f"(= {fmt_val(baseline_mean(data)/UNIT_SCALE)} us)")
    print()


def main():
    data = []
    for n in POINTS:
        path = Path(DIR) / PATTERN.format(i=n)
        if not path.exists():
            print(f"warning: {path} not found, skipping", file=sys.stderr)
            continue
        mean, total = hist_stats(load_histogram(path))
        data.append((n, mean, total))
    if not data:
        raise SystemExit("no input files found -- check DIR and PATTERN")

    with plt.rc_context(STYLE):
        fig = plot(data)
        for out in OUT_FILES:
            fig.savefig(out)
            print(f"wrote {out}", file=sys.stderr)
        plt.close(fig)

    report(data)


if __name__ == "__main__":
    main()
