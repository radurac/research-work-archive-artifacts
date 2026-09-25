#!/usr/bin/env python3
"""
Microbenchmark plots (section 6.1.5)
To modify: SYSTEMS (location of the histograms for the different costs, use the unnormalized IPC histo)
"""


import sys
from pathlib import Path

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt 
from matplotlib.ticker import FuncFormatter, MaxNLocator  

SYSTEMS = [
    ("../measurements/send/send_histo.txt", "vhost-net send()",
     dict(facecolor="#4878A8", edgecolor="#2F4F6F")),
    ("../measurements/send/send_dpdk_histo.txt", "DPDK vhost-user send()",
     dict(facecolor="#B4453C", edgecolor="#7E2F29")),
    ("../measurements/ipc/histo", "IPC Protocol",
     dict(facecolor="#4E8A57", edgecolor="#35603C")),
]

OUT_FILES = ["bulk_facets.pdf"]


BUCKET_WIDTH = 25.0 

SPLIT = 99.0          
XMIN_PCT = 0.05       
STATS = [50, 99, 99.9, 99.99, 100]   

UNIT_SCALE = 1e3      
UNIT_NAME = "us"      
UNIT_LABEL = r"$\mu$s"

WIDTH = 6.0           
PANEL_HEIGHT = 2.2    
HEADROOM = 0.06       
FONTSIZE = 9.0        

BLOCK_HEIGHT = 0.81 
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
        counts.append(value)
    if not counts:
        raise SystemExit(f"{path}: no bucket counts found")
    return np.asarray(counts, dtype=np.float64)


class Hist:

    def __init__(self, counts: np.ndarray, width: float):
        self.counts = counts
        self.width = width
        self.edges = np.arange(len(counts) + 1, dtype=np.float64) * width
        self.centers = self.edges[:-1] + width / 2.0
        self.cum = np.cumsum(counts)
        self.total = float(self.cum[-1])
        if self.total <= 0:
            raise SystemExit("histogram is empty (all buckets are zero)")
        nz = np.flatnonzero(counts)
        self.first_nz, self.last_nz = int(nz[0]), int(nz[-1])
        self.n_nonempty = int(nz.size)

    def bucket_of(self, q: float) -> int:
        target = q / 100.0 * self.total
        if target <= 0:
            return self.first_nz
        return min(int(np.searchsorted(self.cum, target, side="left")),
                   self.last_nz)

    def percentile(self, q: float) -> float:
        if q >= 100:
            return float(self.edges[self.last_nz + 1])
        target = q / 100.0 * self.total
        if target <= 0:
            return float(self.edges[self.first_nz])
        i = self.bucket_of(q)
        below = self.cum[i - 1] if i > 0 else 0.0
        in_bucket = self.cum[i] - below
        frac = (target - below) / in_bucket if in_bucket > 0 else 1.0
        return float(self.edges[i] + min(max(frac, 0.0), 1.0) * self.width)

    @property
    def mean(self) -> float:
        return float(np.dot(self.counts, self.centers) / self.total)


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


def bulk(sysd: dict):
    h: Hist = sysd["h"]
    sub = h.counts[sysd["i_lo"]: sysd["i_hi"] + 1]
    edges = h.edges[sysd["i_lo"]] + np.arange(len(sub) + 1) * h.width
    return sub, edges



def stat_rows(sysd: dict) -> list:
    rows = [("mean", sysd["h"].mean / UNIT_SCALE)]
    for q in STATS:
        rows.append((stat_label(q), sysd["h"].percentile(q) / UNIT_SCALE))
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
        counts, edges = bulk(sysd)
        sysd["bin_width"] = sysd["h"].width

        ax.stairs(counts, edges / UNIT_SCALE, fill=True, linewidth=0.3,
                  **sysd["colour"])

        x_lo, x_hi = sysd["lo"] / UNIT_SCALE, sysd["hi"] / UNIT_SCALE
        pad = 0.02 * (x_hi - x_lo) if x_hi > x_lo else 1.0
        ax.set_xlim(x_lo - pad, x_hi)
        ax.set_ylim(0, float(counts.max()) * (1.0 + HEADROOM)
                    if counts.size else 1)

        ax.set_title(sysd["name"], pad=4)
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
    head = (["system", "samples", "buckets", "bin", "mean"]
            + [f"p{q:g}" if q < 100 else "max" for q in STATS])
    table = []
    for d in systems:
        h: Hist = d["h"]
        row = [d["name"], f"{h.total:,.0f}", f"{h.n_nonempty:,}",
               f"{d['bin_width']/s:g}", fmt_val(h.mean / s)]
        row += [fmt_val(h.percentile(q) / s) for q in STATS]
        table.append(row)

    widths = [max(len(hd), *(len(r[c]) for r in table))
              for c, hd in enumerate(head)]
    print(f"\n  latencies in {UNIT_NAME}; panels span p{XMIN_PCT:g} to "
          f"p{SPLIT:g}; source buckets {BUCKET_WIDTH:g} ns")
    print("  estimates from binned data; max is the last non-empty "
          "bucket's upper edge\n")
    print("  " + "  ".join(hd.ljust(w) if c == 0 else hd.rjust(w)
                           for c, (hd, w) in enumerate(zip(head, widths))))
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
        h = Hist(load_histogram(path), BUCKET_WIDTH)
        systems.append({"name": name, "colour": colour, "h": h,
                        "lo": h.percentile(XMIN_PCT), "hi": h.percentile(SPLIT),
                        "i_lo": h.bucket_of(XMIN_PCT),
                        "i_hi": h.bucket_of(SPLIT)})

    with plt.rc_context(STYLE):
        fig = plot(systems)
        for path in OUT_FILES:
            fig.savefig(path)
            print(f"wrote {path}", file=sys.stderr)
        plt.close(fig)

    report(systems)


if __name__ == "__main__":
    main()
