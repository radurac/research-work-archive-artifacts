#!/usr/bin/env python3
"""
One VPAC different stream counts VM cost plot (section 6.5.1)
To modify: SYSTEMS (the location of the histograms for the two setups)
"""

import sys
from pathlib import Path

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  
from matplotlib.lines import Line2D  
from matplotlib.transforms import offset_copy  
from matplotlib.ticker import (FixedLocator, FuncFormatter,  
                               NullFormatter)


SYSTEMS = [
    ("../measurements/vm_cap/monitor/histograms/", "monitor",
     dict(color="#4878A8", marker="o", ls="-")),
    ("../measurements/vm_cap/no_monitor/histograms/", "no-monitor",
     dict(color="#B4453C", marker="s", ls=(0, (4.5, 2.0)))),
]

PROJECT_BASE = "no-monitor"
PROJECT_SLOPE = 3.26

PATTERN = "processing_histo_{i}_streams.txt"   
POINTS = [1, 2, 3, 4, 6, 8, 11, 16, 23, 32, 45, 64, 91, 128]

OUT_FILES = ["scaling_compare.pdf"]


BUCKET_WIDTH = 25.0   

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

LOSS_OFFSET = 16.0  

MEASURED_SUFFIX = "measured"
PREDICTION_SUFFIX = "prediction"

DEADLINE_COLOR = "#222222"
MARK_GREY = "#555555"

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


def hist_mean(counts: np.ndarray):
    total = float(counts.sum())
    if total <= 0:
        raise SystemExit("histogram is empty (all buckets are zero)")
    centers = (np.arange(len(counts), dtype=np.float64) + 0.5) * BUCKET_WIDTH
    return float(np.dot(counts, centers) / total), total


def collect(directory: str, name: str, style: dict) -> dict:
    mean: dict = {}
    samples: dict = {}
    missing: list = []
    for n in POINTS:
        path = Path(directory) / PATTERN.format(i=n)
        if not path.exists():
            missing.append(n)
            continue
        mean[n], samples[n] = hist_mean(load_histogram(path))
    if missing:
        print(f"note: {name}: no file for "
              + ", ".join(f"{n}" for n in missing)
              + " streams; treating as packet loss", file=sys.stderr)

    label = f"{name} ({MEASURED_SUFFIX})"
    return {"raw": name, "label": label, "plain": label, "style": style,
            "derived": False, "points": sorted(mean), "mean": mean,
            "samples": samples, "missing": missing}


def project(systems: list) -> dict:
    base = next((d for d in systems if d["raw"] == PROJECT_BASE), None)
    if base is None:
        raise SystemExit(f"no system named {PROJECT_BASE!r}; have "
                         + ", ".join(repr(d["raw"]) for d in systems))
    slope_ns = PROJECT_SLOPE * UNIT_SCALE
    return {
        "raw": None,
        "label": f"{PROJECT_BASE} $+$ {PROJECT_SLOPE:g} {UNIT_LABEL}/stream "
                 f"({PREDICTION_SUFFIX})",
        "plain": f"{PROJECT_BASE} + {PROJECT_SLOPE:g} {UNIT_NAME}/stream "
                 f"({PREDICTION_SUFFIX})",
        "style": base["style"],
        "derived": True,
        "base": PROJECT_BASE,
        "points": list(base["points"]),
        "mean": {n: base["mean"][n] + slope_ns * n for n in base["points"]},
        "samples": {},
        "missing": list(base["missing"]),
    }



def fmt_val(v: float) -> str:
    if abs(v) >= 1000:
        return f"{v:,.0f}"
    if abs(v) >= 10:
        return f"{v:.1f}"
    return f"{v:.2f}"


def crossing(system: dict):
    ok, first_bad = None, None
    for n in system["points"]:
        if system["mean"][n] <= DEADLINE_NS:
            if first_bad is None:
                ok = n
        elif first_bad is None:
            first_bad = n
    est = None
    if ok is not None and first_bad is not None:
        y0, y1 = system["mean"][ok], system["mean"][first_bad]
        if y1 > y0:
            t = (DEADLINE_NS - y0) / (y1 - y0)
            est = 2.0 ** (np.log2(ok) + t * (np.log2(first_bad) - np.log2(ok)))
    return ok, first_bad, est



def plot(systems: list) -> plt.Figure:
    fig, ax = plt.subplots(figsize=(WIDTH, HEIGHT), layout="constrained")
    fig.get_layout_engine().set(w_pad=0.04, h_pad=0.04)

    loss_rank = {id(d): i for i, d in enumerate(
        d for d in systems if d["missing"] and not d["derived"])}
    lost_any = False
    for k, sysd in enumerate(systems):
        style = dict(sysd["style"])
        if sysd["derived"]:
            style.update(ls=(0, (1.2, 1.4)), lw=0.9, marker=style["marker"],
                         markerfacecolor="white", markeredgewidth=0.8,
                         markeredgecolor=style["color"], markersize=3.0)
        y = [sysd["mean"][n] / UNIT_SCALE if n in sysd["mean"] else np.nan
             for n in POINTS]
        ax.plot(POINTS, y, label=sysd["label"], clip_on=False, zorder=3 + k,
                **style)

        if sysd["missing"] and not sysd["derived"]:
            lost_any = True
            rank = loss_rank[id(sysd)]
            trans = offset_copy(ax.get_xaxis_transform(), fig=fig,
                                y=-(LOSS_OFFSET + 9.0 * rank), units="points")
            ax.plot(sysd["missing"], [0.0] * len(sysd["missing"]),
                    transform=trans, ls="none", marker="x", ms=4.5, mew=1.1,
                    color=style["color"], clip_on=False, zorder=6)

    dl = DEADLINE_NS / UNIT_SCALE
    ax.axhline(dl, color=DEADLINE_COLOR, lw=0.9, ls=(0, (6, 2, 1, 2)),
               zorder=2, label=f"deadline = {fmt_val(dl)} {UNIT_LABEL}")

    xs = np.array(POINTS, dtype=float)
    ax.set_xscale("log", base=2)
    ax.xaxis.set_major_locator(FixedLocator(xs))
    ax.xaxis.set_minor_formatter(NullFormatter())
    ax.set_xlim(xs.min() * 0.92, xs.max() * 1.08)
    ax.xaxis.set_major_formatter(FuncFormatter(lambda v, _p: f"{v:g}"))

    ymax = max(max(d["mean"].values()) for d in systems) / UNIT_SCALE
    ax.set_ylim(0, max(ymax, dl) * 1.08)

    pad = (LOSS_OFFSET + 9.0 * max(loss_rank.values(), default=0) - 4.0
           if lost_any else None)
    ax.set_xlabel("SV streams", labelpad=pad)
    ax.set_ylabel(f"mean latency [{UNIT_LABEL}]")

    handles, hlabels = ax.get_legend_handles_labels()
    if lost_any:
        handles.append(Line2D([], [], ls="none", marker="x", ms=4.5, mew=1.1,
                              color=MARK_GREY))
        hlabels.append("packet loss")
    ax.legend(handles=handles, labels=hlabels, loc=LEGEND_LOC,
              ncol=LEGEND_COLS, frameon=True, facecolor="white",
              edgecolor="none", framealpha=0.85, borderaxespad=0.3,
              labelspacing=0.3, borderpad=0.35, handletextpad=0.6,
              handlelength=2.2, columnspacing=1.2)
    return fig


def report(systems: list) -> None:
    s = UNIT_SCALE
    names = [d["plain"] for d in systems]
    points = sorted({n for d in systems for n in d["points"]})

    print(f"\n  mean latency, in {UNIT_NAME}; "
          f"deadline = {fmt_val(DEADLINE_NS/s)} {UNIT_NAME}")
    print(f"  estimates from {BUCKET_WIDTH:g} ns buckets; "
          f"'loss' = no file, i.e. the point could not be measured\n")

    head = ["streams"] + names
    table = []
    for n in points:
        row = [f"{n}"]
        for d in systems:
            if n in d["mean"]:
                v = fmt_val(d["mean"][n] / s)
                row.append(v if d["mean"][n] <= DEADLINE_NS else v + "*")
            else:
                row.append("loss")
        table.append(row)

    widths = [max(len(h), *(len(r[c]) for r in table))
              for c, h in enumerate(head)]
    print("  " + "  ".join(h.rjust(w) for h, w in zip(head, widths)))
    print("  " + "  ".join("-" * w for w in widths))
    for row in table:
        print("  " + "  ".join(c.rjust(w) for c, w in zip(row, widths)))
    print("\n  * exceeds the deadline\n")

    print("  samples per point:")
    varying = []
    for d in systems:
        if d["derived"]:
            print(f"    {d['plain']}: derived from {d['base']}")
            continue
        counts = set(d["samples"].values())
        if not counts:
            print(f"    {d['plain']}: no measured point")
        elif len(counts) == 1:
            print(f"    {d['plain']}: {next(iter(counts)):,.0f} at every "
                  f"measured point ({len(d['samples'])} points, "
                  f"{sum(d['samples'].values()):,.0f} total)")
        else:
            varying.append(d)
            print(f"    {d['plain']}: varies, "
                  f"{min(counts):,.0f} to {max(counts):,.0f} "
                  f"({sum(d['samples'].values()):,.0f} total)")

    if varying:
        vhead = ["streams"] + [d["plain"] for d in varying]
        vtable = []
        for n in points:
            row = [f"{n}"]
            for d in varying:
                row.append(f"{d['samples'][n]:,.0f}" if n in d["samples"]
                           else "loss")
            vtable.append(row)
        vw = [max(len(h), *(len(r[c]) for r in vtable))
              for c, h in enumerate(vhead)]
        print()
        print("  " + "  ".join(h.rjust(w) for h, w in zip(vhead, vw)))
        print("  " + "  ".join("-" * w for w in vw))
        for row in vtable:
            print("  " + "  ".join(c.rjust(w) for c, w in zip(row, vw)))
    print()

    for d in systems:
        if d["missing"] and not d["derived"]:
            print(f"  {d['plain']}: packet loss at "
                  + ", ".join(f"{n}" for n in d["missing"]) + " streams")
    for d in systems:
        ok, bad, est = crossing(d)
        if ok is None:
            print(f"  {d['plain']}: misses the deadline at every point")
        elif bad is None:
            print(f"  {d['plain']}: meets the deadline at every point "
                  f"(up to {d['points'][-1]} streams)")
        else:
            extra = f", crossing near {est:.0f}" if est else ""
            print(f"  {d['plain']}: highest passing point {ok} streams, "
                  f"first miss at {bad}{extra}")
    print()



def main() -> None:
    for d, _, _ in SYSTEMS:
        if not Path(d).is_dir():
            raise SystemExit(f"{d}: not a directory")

    systems = [collect(d, name, style) for d, name, style in SYSTEMS]
    systems.append(project(systems))

    with plt.rc_context(STYLE):
        fig = plot(systems)
        for path in OUT_FILES:
            fig.savefig(path)
            print(f"wrote {path}", file=sys.stderr)
        plt.close(fig)

    report(systems)


if __name__ == "__main__":
    main()
