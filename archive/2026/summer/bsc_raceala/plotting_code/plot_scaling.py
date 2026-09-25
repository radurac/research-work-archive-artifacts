#!/usr/bin/env python3
"""
Host-side CPU usage plot (section 6.3)
To modify SYSTEMS (location of the measurements for the different systems)
"""

import sys
from pathlib import Path

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt 
from matplotlib.ticker import (FixedLocator, FuncFormatter,  
                               NullFormatter)

SYSTEMS = [
    ("../measurements/dpdk/throughput/", "DPDK Host-Cache",
     dict(color="#4878A8", marker="o", ls="-")),
    ("../measurements/af_xdp/throughput/",
     "AF_XDP Host-Cache",
     dict(color="#B4453C", marker="s", ls=(0, (4.5, 2.0)))),
    ("../measurements/shared-nothing/throughput/1_core/",
     "Shared-Nothing 1 core",
     dict(color="#4E8A57", marker="^", ls=(0, (5, 1.6, 1, 1.6)))),
    ("../measurements/shared-nothing/throughput/2_core/",
     "Shared-Nothing 2 core",
     dict(color="#8C6BB1", marker="D", ls=(0, (1.2, 1.4)))),
    ("../measurements/xdp-plain/1_core/", "XDP plain 1 core",
     dict(color="#D08C3F", marker="v", ls=(0, (6, 1.5, 1, 1.5, 1, 1.5)))),
    ("../measurements/xdp-plain/2_core/", "XDP plain 2 core",
     dict(color="#4C4C4C", marker="P", ls=(0, (3, 1.3)))),
]

PATTERN = "latencies_{i}_stream"   
POINTS = [2, 3, 4, 6, 8, 11, 16, 23, 32, 45, 64, 91, 128]
BASELINE_POINT = 2          

OUT_FILES = ["scaling_compare.pdf"]


DTYPE = "<u8"         
SAMPLES = 100_000     

DEADLINE_US = 250.0   

UNIT_SCALE = 1e3      
UNIT_NAME = "us"      
UNIT_LABEL = r"$\mu$s"

DEADLINE_NS = DEADLINE_US * UNIT_SCALE

WIDTH = 4.8 
HEIGHT = 3.0          
FONTSIZE = 9.0        

LEGEND_LOC = "upper left"
LEGEND_COLS = 1

DEADLINE_COLOR = "#222222"

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
    "xtick.minor.width": 0.4,
    "ytick.minor.width": 0.4,
    "xtick.major.size": 2.5,
    "ytick.major.size": 2.5,
    "xtick.direction": "out",
    "ytick.direction": "out",
    "lines.linewidth": 1.1,
    "lines.markersize": 3.4,
    "lines.markeredgewidth": 0.0,
    "axes.grid": True,
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



def load_samples(path: Path) -> np.ndarray:
    samples = np.fromfile(path, dtype=DTYPE).astype(np.float64)
    if samples.size == 0:
        raise SystemExit(f"{path}: no samples")
    if SAMPLES and samples.size != SAMPLES:
        print(f"warning: {path.name} holds {samples.size:,} samples, "
              f"expected {SAMPLES:,}", file=sys.stderr)
    return samples


def collect(directory: str, name: str, style: dict) -> dict:
    means = {n: float(load_samples(Path(directory) / PATTERN.format(i=n)).mean())
             for n in POINTS}
    base = means[BASELINE_POINT]
    return {"name": name, "style": style, "baseline": base,
            "delta": {n: means[n] - base for n in POINTS}}



def fmt_val(v: float) -> str:
    if abs(v) >= 1000:
        return f"{v:,.0f}"
    if abs(v) >= 10:
        return f"{v:.1f}"
    return f"{v:.2f}"


def crossing(system: dict):
    ok, first_bad = None, None
    for n in POINTS:
        if system["delta"][n] <= DEADLINE_NS:
            if first_bad is None:
                ok = n
        elif first_bad is None:
            first_bad = n
    est = None
    if ok is not None and first_bad is not None:
        y0, y1 = system["delta"][ok], system["delta"][first_bad]
        if y1 > y0:
            t = (DEADLINE_NS - y0) / (y1 - y0)
            est = 2.0 ** (np.log2(ok) + t * (np.log2(first_bad) - np.log2(ok)))
    return ok, first_bad, est


def plot(systems: list[dict]) -> plt.Figure:
    fig, ax = plt.subplots(figsize=(WIDTH, HEIGHT), layout="constrained")
    fig.get_layout_engine().set(w_pad=0.04, h_pad=0.04)

    for k, sysd in enumerate(systems):
        y = [sysd["delta"][n] / UNIT_SCALE for n in POINTS]
        ax.plot(POINTS, y, label=sysd["name"].replace("_", r"$\_$"),
                clip_on=False, zorder=3 + k, **sysd["style"])

    dl = DEADLINE_NS / UNIT_SCALE
    ax.axhline(dl, color=DEADLINE_COLOR, lw=0.9, ls=(0, (6, 2, 1, 2)),
               zorder=2, label=f"deadline = {fmt_val(dl)} {UNIT_LABEL}")

    xs = np.array(POINTS, dtype=float)
    ax.set_xscale("log", base=2)
    ax.xaxis.set_major_locator(FixedLocator(xs))
    ax.xaxis.set_minor_formatter(NullFormatter())
    ax.set_xlim(xs.min() * 0.92, xs.max() * 1.08)
    ax.xaxis.set_major_formatter(FuncFormatter(lambda v, _p: f"{v:g}"))

    ymax = max(max(d["delta"].values()) for d in systems) / UNIT_SCALE
    ax.set_ylim(0, max(ymax, dl) * 1.08)

    ax.set_xlabel("SV streams")
    ax.set_ylabel(f"mean latency increase over {BASELINE_POINT} "
                  f"streams [{UNIT_LABEL}]")
    ax.legend(loc=LEGEND_LOC, ncol=LEGEND_COLS, frameon=True,
              facecolor="white", edgecolor="none", framealpha=0.85,
              borderaxespad=0.3, labelspacing=0.3, borderpad=0.35,
              handletextpad=0.6, handlelength=2.2, columnspacing=1.2)
    return fig



def report(systems: list[dict]) -> None:
    s = UNIT_SCALE
    names = [d["name"] for d in systems]

    print(f"\n  mean latency increase over {BASELINE_POINT} streams, "
          f"in {UNIT_NAME}; deadline = {fmt_val(DEADLINE_NS/s)} "
          f"{UNIT_NAME}\n")
    print("  absolute mean at the baseline point:")
    for d in systems:
        print(f"    {d['name']}: {fmt_val(d['baseline']/s)} {UNIT_NAME}")
    print()

    head = ["streams"] + names
    table = []
    for n in POINTS:
        row = [f"{n}"]
        for d in systems:
            v = fmt_val(d["delta"][n] / s)
            row.append(v if d["delta"][n] <= DEADLINE_NS else v + "*")
        table.append(row)

    widths = [max(len(h), *(len(r[c]) for r in table))
              for c, h in enumerate(head)]
    print("  " + "  ".join(h.rjust(w) for h, w in zip(head, widths)))
    print("  " + "  ".join("-" * w for w in widths))
    for row in table:
        print("  " + "  ".join(c.rjust(w) for c, w in zip(row, widths)))
    print("\n  * exceeds the deadline\n")

    for d in systems:
        ok, bad, est = crossing(d)
        if ok is None:
            print(f"  {d['name']}: misses the deadline at every point")
        elif bad is None:
            print(f"  {d['name']}: meets the deadline at every point "
                  f"(up to {POINTS[-1]} streams)")
        else:
            extra = f", crossing near {est:.0f}" if est else ""
            print(f"  {d['name']}: highest passing point {ok} streams, "
                  f"first miss at {bad}{extra}")
    print()



def main() -> None:
    missing = [str(Path(d) / PATTERN.format(i=n))
               for d, _, _ in SYSTEMS for n in POINTS
               if not (Path(d) / PATTERN.format(i=n)).exists()]
    if missing:
        raise SystemExit("missing input files:\n  " + "\n  ".join(missing))

    systems = [collect(d, name, style) for d, name, style in SYSTEMS]

    with plt.rc_context(STYLE):
        fig = plot(systems)
        for path in OUT_FILES:
            fig.savefig(path)
            print(f"wrote {path}", file=sys.stderr)
        plt.close(fig)

    report(systems)


if __name__ == "__main__":
    main()
