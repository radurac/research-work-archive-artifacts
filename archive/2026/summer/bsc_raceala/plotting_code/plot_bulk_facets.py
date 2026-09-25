#!/usr/bin/env python3
"""
RTT plot (section 6.4)
To modify: SYSTEMS (the location of the measurements for the different systems)
"""

import sys
from pathlib import Path

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt 
from matplotlib.ticker import FuncFormatter, MaxNLocator

SYSTEMS = [
    ("../measurements/dpdk/latency/latencies", "DPDK Host-Cache",
     dict(facecolor="#4878A8", edgecolor="#2F4F6F")),
    ("../measurements/af_xdp/latency/latencies",
     "AF_XDP Host-Cache",
     dict(facecolor="#B4453C", edgecolor="#7E2F29")),
    ("../measurements/shared-nothing/latency/latencies", "Shared-Nothing",
     dict(facecolor="#4E8A57", edgecolor="#35603C")),
]

OUT_FILES = ["bulk_facets.pdf"]


DTYPE = "<u8"         

SPLIT = 99.0          
XMIN_PCT = 0.05       
STATS = [50, 99, 99.9, 99.99, 100]   

BIN_WIDTH = 25.0      

UNIT_SCALE = 1e3      
UNIT_NAME = "us"      
UNIT_LABEL = r"$\mu$s"

WIDTH = 6.0           
PANEL_HEIGHT = 2.2    
BLOCK_HEIGHT = 0.81   
HEADROOM = 0.06       
FONTSIZE = 9.0        

PLOT_RATIO = PANEL_HEIGHT / BLOCK_HEIGHT
HEIGHT = PANEL_HEIGHT + BLOCK_HEIGHT

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
    return np.sort(samples, kind="stable")


def percentile(sorted_s: np.ndarray, q: float) -> float:
    n = sorted_s.size
    if n == 1:
        return float(sorted_s[0])
    pos = min(max((q / 100.0) * (n - 1), 0.0), float(n - 1))
    lo = int(np.floor(pos))
    hi = min(lo + 1, n - 1)
    frac = pos - lo
    return float(sorted_s[lo] * (1.0 - frac) + sorted_s[hi] * frac)



def fmt_val(v: float) -> str:
    if abs(v) >= 1000:
        return f"{v:,.0f}"
    if abs(v) >= 10:
        return f"{v:.1f}"
    return f"{v:.2f}"


def thousands(x, _pos=None) -> str:
    if abs(x) >= 1e9:
        return f"{x/1e9:g}G"
    if abs(x) >= 1e6:
        return f"{x/1e6:g}M"
    if abs(x) >= 1e3:
        return f"{x/1e3:g}k"
    return f"{x:g}"


def stat_label(q: float) -> str:
    return "max" if q >= 100 else f"$p_{{{q:g}}}$"


def edges_for(lo: float, hi: float, width: float) -> np.ndarray:
    start = np.floor(lo / width) * width
    n = int(np.ceil((hi - start) / width)) + 1
    return start + np.arange(n + 1, dtype=np.float64) * width



def stat_rows(sysd: dict) -> list:
    rows = [("mean", float(sysd["s"].mean()) / UNIT_SCALE)]
    for q in STATS:
        rows.append((stat_label(q), percentile(sysd["s"], q) / UNIT_SCALE))
    return rows


def stats_panel(tx, sysd: dict) -> None:
    tx.set_axis_off()
    tx.plot([0, 1], [1, 1], transform=tx.transAxes, color="#BBBBBB",
            lw=0.5, clip_on=False, zorder=1)
    entries = stat_rows(sysd)
    rows = len(entries)
    for j, (name, value) in enumerate(entries):
        y = 1.0 - (j + 0.8) / rows
        tx.text(0.06, y, name, transform=tx.transAxes, ha="left",
                va="center", fontsize=FONTSIZE - 1)
        tx.text(0.94, y, f"{fmt_val(value)} {UNIT_LABEL}",
                transform=tx.transAxes, ha="right", va="center",
                fontsize=FONTSIZE - 1)


def plot(systems: list) -> plt.Figure:
    n = len(systems)
    fig = plt.figure(figsize=(WIDTH, HEIGHT), layout="constrained")
    fig.get_layout_engine().set(w_pad=0.03, h_pad=0.03, wspace=0.03,
                                hspace=0.04)

    gs = fig.add_gridspec(2, n, height_ratios=[PLOT_RATIO, 1.0])
    axes = [fig.add_subplot(gs[0, i]) for i in range(n)]
    text_axes = [fig.add_subplot(gs[1, i]) for i in range(n)]

    for i, (ax, sysd) in enumerate(zip(axes, systems)):
        bulk = sysd["s"][:max(sysd["cut"], 1)]
        edges = edges_for(sysd["lo"], sysd["hi"], BIN_WIDTH)
        counts, _ = np.histogram(bulk, bins=edges)

        ax.stairs(counts, edges / UNIT_SCALE, fill=True, linewidth=0.3,
                  **sysd["colour"])

        x_lo, x_hi = sysd["lo"] / UNIT_SCALE, sysd["hi"] / UNIT_SCALE
        pad = 0.02 * (x_hi - x_lo) if x_hi > x_lo else 1.0
        ax.set_xlim(x_lo - pad, x_hi)
        ax.set_ylim(0, float(counts.max()) * (1.0 + HEADROOM)
                    if counts.size else 1.0)

        ax.set_title(sysd["name"].replace("_", r"$\_$"), pad=4)
        ax.xaxis.set_major_formatter(FuncFormatter(thousands))
        ax.yaxis.set_major_formatter(FuncFormatter(thousands))
        ax.yaxis.set_major_locator(MaxNLocator(nbins=4))
        ax.xaxis.set_major_locator(MaxNLocator(nbins=4))

        stats_panel(text_axes[i], sysd)

        ax.set_xlabel(f"latency [{UNIT_LABEL}]")
        if i == 0:
            ax.set_ylabel("samples per bin")

    return fig



def report(systems: list) -> None:
    s = UNIT_SCALE
    head = (["system", "samples", "bin", "mean"]
            + [f"p{q:g}" if q < 100 else "max" for q in STATS])
    table = []
    for d in systems:
        row = [d["name"], f"{d['s'].size:,}", f"{BIN_WIDTH/s:g}",
               fmt_val(float(d["s"].mean()) / s)]
        row += [fmt_val(percentile(d["s"], q) / s) for q in STATS]
        table.append(row)

    widths = [max(len(h), *(len(r[c]) for r in table))
              for c, h in enumerate(head)]
    print(f"\n  latencies in {UNIT_NAME}; panels span p{XMIN_PCT:g} to "
          f"p{SPLIT:g}\n")
    print("  " + "  ".join(h.ljust(w) if c == 0 else h.rjust(w)
                           for c, (h, w) in enumerate(zip(head, widths))))
    print("  " + "  ".join("-" * w for w in widths))
    for row in table:
        print("  " + "  ".join(c.ljust(w) if i == 0 else c.rjust(w)
                               for i, (c, w) in enumerate(zip(row, widths))))
    print()



def main() -> None:
    systems = []
    for f, name, colour in SYSTEMS:
        path = Path(f)
        if not path.is_file():
            raise SystemExit(f"{path}: not a file")
        samples = load_samples(path)
        cut = int(np.searchsorted(samples, percentile(samples, SPLIT),
                                  side="left"))
        systems.append({"name": name, "colour": colour, "s": samples,
                        "cut": cut, "lo": percentile(samples, XMIN_PCT),
                        "hi": percentile(samples, SPLIT)})

    with plt.rc_context(STYLE):
        fig = plot(systems)
        for path in OUT_FILES:
            fig.savefig(path)
            print(f"wrote {path}", file=sys.stderr)
        plt.close(fig)

    report(systems)


if __name__ == "__main__":
    main()
