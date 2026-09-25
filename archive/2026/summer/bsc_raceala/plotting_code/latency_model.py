#!/usr/bin/env python3
"""
Worst-case burst validation (section 6.5.2)
To modify: LAT_DIR (wc_burst measurement location), SEND_HISTO (dpdk send() histo location), IPC_HISTO (IPC normalized histo location)
"""
import math
import os
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter, MaxNLocator

LAT_DIR = "../measurements/dpdk/wc_burst/"
LAT_PATTERN = "latencies_{w}_windows_delay"
SEND_HISTO = "../measurements/send/send_dpdk_histo.txt"
IPC_HISTO = "../measurements/ipc/histo_norm"

IPC_COUNT = 4
WINDOWS = [1, 2, 3, 4, 5]
OUTDIR = "."

BW = 25.0            
PCTS = [1, 5, 10, 25, 50, 75, 90, 95, 99, 99.9, 99.99]
STATS_PCTS = [50, 99, 99.9, 99.99]   

UNIT_SCALE = 1.0     
UNIT_LABEL = "ns"

WIDTH = 6.0          
FONTSIZE = 9.0       
PLOT_RATIO = 2.0     
XMIN_PCT = 0.05
XMAX_PCT = 99.9
CDF_POINTS = 3000

MEAS_STYLE = dict(color="#1F3B57", ls="-", lw=2.8, alpha=0.30, zorder=2,
                  solid_capstyle="round")
PRED_STYLES = [
    dict(color="#4E8A57", ls=(0, (1.2, 1.4)), lw=1.3, zorder=3),    
    dict(color="#2F6096", ls=(0, (5, 1.6, 1, 1.6)), lw=1.2, zorder=5),  
]

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


def thousands(x, _pos=None):
    if abs(x) >= 1e9:
        return f"{x/1e9:g}G"
    if abs(x) >= 1e6:
        return f"{x/1e6:g}M"
    if abs(x) >= 1e3:
        return f"{x/1e3:g}k"
    return f"{x:g}"


def load_latencies(path):
    return np.frombuffer(open(path, "rb").read(), dtype="<u8").astype(np.float64)


def load_histogram(path):
    counts = np.array([int(l) for l in open(path) if l.strip()], dtype=float)
    nz = np.nonzero(counts)[0]
    lo, hi = (nz[0], nz[-1] + 1) if nz.size else (0, len(counts))
    counts = counts[lo:hi]
    centers = (np.arange(lo, hi) + 0.5) * BW
    return centers, counts / counts.sum()


def hstats(centers, pmf):
    mean = np.sum(centers * pmf)
    var = np.sum((centers - mean) ** 2 * pmf)
    return mean, var, np.sqrt(var)


def sample(centers, pmf, n, rng):
    idx = rng.choice(len(pmf), size=n, p=pmf)
    return centers[idx] + rng.uniform(-BW / 2, BW / 2, size=n)


def predict(w, baseline, pure_step, send=None, ipc=None):
    if w == 1:
        return np.sort(baseline)
    rng = np.random.default_rng(0)
    n = baseline.size
    add = np.full(n, pure_step * (w - 1))
    for _ in range(w - 1):
        if send is not None:
            c, p = send
            add += sample(c, p, n, rng)
        if ipc is not None:
            c, p = ipc
            for _ in range(IPC_COUNT):
                add += sample(c, p, n, rng)
    return np.sort(baseline + add)


def trimmed_var(x):
    lo, hi = np.percentile(x, [0.1, 100 - 0.1])
    return x[(x >= lo) & (x <= hi)].var()


def pct_delta(meas, pred, q):
    return np.percentile(pred, q) - np.percentile(meas, q)


def delta_decimals(lat, preds, stats):
    biggest = 0.0
    for w in lat:
        for p in preds[w]:
            for q in stats:
                biggest = max(biggest,
                              abs(pct_delta(lat[w], p, q)) / UNIT_SCALE)
    return 0 if biggest >= 100 else (1 if biggest >= 10 else 2)


def supported_pcts(qs, n):
    qmax = 100.0 * (1.0 - 1.0 / n)
    keep = [q for q in qs if q <= qmax]
    for q in qs:
        if q > qmax:
            print(f"warning: {n:,} samples cannot resolve p{q:g} "
                  f"(deepest resolvable is p{qmax:.4g}); dropping it")
    return keep


def stats_panel(tx, w, lat, preds, labels, stats, delta_dec):
    tx.set_axis_off()
    tx.plot([0, 1], [1, 1], transform=tx.transAxes, color="#BBBBBB",
            lw=0.5, clip_on=False, zorder=1)

    nvar = len(preds[w])
    label_w = 0.30
    colw = (1.0 - label_w) / nvar
    right = [label_w + (j + 1) * colw for j in range(nvar)]
    fs = FONTSIZE - 2
    rows = len(stats) + 1                       
    ytop = 1.0

    def yof(k):
        return ytop - (k + 0.75) / rows

    corner = f"$\\Delta$ [{UNIT_LABEL}]"
    for j, lb in enumerate(labels):
        tx.text(right[j], yof(0), lb, transform=tx.transAxes, ha="right",
                va="center", fontsize=fs, color=PRED_STYLES[j]["color"])
    tx.text(0.0, yof(0), corner, transform=tx.transAxes, ha="left",
            va="center", fontsize=fs, color="#555555")

    for k, q in enumerate(stats, start=1):
        tx.text(0.0, yof(k), f"$p_{{{q:g}}}$", transform=tx.transAxes,
                ha="left", va="center", fontsize=fs)
        for j, p in enumerate(preds[w]):
            txt = f"{pct_delta(lat[w], p, q)/UNIT_SCALE:+,.{delta_dec}f}"
            tx.text(right[j], yof(k), txt, transform=tx.transAxes,
                    ha="right", va="center", fontsize=fs)


def grid_cols(n):
    return 2 if n == 4 else min(3, n)


def _thin(values, npoints):
    cdf = np.arange(1, values.size + 1) / values.size
    if values.size <= npoints:
        return values, cdf
    idx = np.unique(np.linspace(0, values.size - 1, npoints).astype(int))
    return values[idx], cdf[idx]


def plot_cdf(lat, preds, labels, outfile, stats, height):
    s, unit = UNIT_SCALE, UNIT_LABEL
    ws = sorted(lat)
    if len(ws) > 1:
        ws = ws[1:]
    ncols = min(grid_cols(len(ws)), len(ws))
    nrows = math.ceil(len(ws) / ncols)
    delta_dec = delta_decimals(lat, preds, stats)

    fig = plt.figure(figsize=(WIDTH, height), layout="constrained")
    fig.get_layout_engine().set(w_pad=0.03, h_pad=0.03, wspace=0.02, hspace=0.04)

    gs = fig.add_gridspec(2 * nrows, ncols,
                          height_ratios=[PLOT_RATIO, 1.0] * nrows)
    cells = [(gs[2 * (i // ncols), i % ncols],
              gs[2 * (i // ncols) + 1, i % ncols]) for i in range(len(ws))]
    blanks = [(gs[2 * (i // ncols), i % ncols],
               gs[2 * (i // ncols) + 1, i % ncols])
              for i in range(len(ws), nrows * ncols)]

    flat, text_axes, share = [], [], None
    for cell, tcell in cells:
        ax = fig.add_subplot(cell, sharey=share)
        share = share or ax
        flat.append(ax)
        text_axes.append(fig.add_subplot(tcell))

    for i, (ax, w) in enumerate(zip(flat, ws)):
        tx = text_axes[i]
        meas = np.sort(lat[w])
        x, y = _thin(meas, CDF_POINTS)
        ax.plot(x / s, y, label="measured", **MEAS_STYLE)
        for style, p, lb in zip(PRED_STYLES, preds[w], labels):
            px, py = _thin(np.asarray(p), CDF_POINTS)
            ax.plot(px / s, py, label=lb, **style)
        ax.set_title(f"$w = {w}$", pad=4)
        ax.set_ylim(0, 1)
        ax.set_yticks([0, 0.25, 0.5, 0.75, 1.0])
        ax.set_xlim(np.percentile(lat[w], XMIN_PCT) / s,
                    np.percentile(lat[w], XMAX_PCT) / s)
        ax.xaxis.set_major_locator(MaxNLocator(nbins=5))
        ax.xaxis.set_major_formatter(FuncFormatter(thousands))
        if i % ncols == 0:
            ax.set_ylabel("CDF")
        else:
            ax.tick_params(labelleft=False)
        if i + ncols >= len(ws):
            ax.set_xlabel(f"latency [{unit}]")
        stats_panel(tx, w, lat, preds, labels, stats, delta_dec)
    for cell, tcell in blanks:
        for c in (cell, tcell):
            fig.add_subplot(c).set_visible(False)
    handles, hlabels = flat[0].get_legend_handles_labels()
    handles[0] = plt.Line2D([], [], color=MEAS_STYLE["color"], lw=2.4, alpha=0.55)
    fig.legend(handles, hlabels, loc="outside lower center",
               ncol=len(hlabels), frameon=False, borderaxespad=0.2,
               handlelength=2.6, columnspacing=1.6, handletextpad=0.5)
    fig.savefig(outfile)
    plt.close(fig)


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    ws = list(WINDOWS)
    lat = {w: load_latencies(os.path.join(LAT_DIR,
                                          LAT_PATTERN.format(w=w))) for w in ws}
    send = load_histogram(SEND_HISTO)
    ipc = load_histogram(IPC_HISTO)

    stats = supported_pcts(sorted(STATS_PCTS),
                           min(v.size for v in lat.values()))
    shown = len(ws) - (1 if len(ws) > 1 else 0)
    rows = math.ceil(shown / min(grid_cols(shown), shown))
    block = 0.13 * (len(stats) + 1) + 0.10
    height = rows * (1 + PLOT_RATIO) * block

    meds = np.array([np.median(lat[w]) for w in ws])
    step, intercept = np.polyfit(ws, meds, 1)
    sm, sv, ss = hstats(*send)
    cm, cv, cs = hstats(*ipc)
    pure_step = step - sm - IPC_COUNT * cm
    print(f"STEP (median increment) = {step:.1f} ns/window")
    print(f"  send mean          = {sm:.1f}")
    print(f"  {IPC_COUNT} x IPC mean        = {IPC_COUNT*cm:.1f}")
    print(f"  => pure fixed step  = {pure_step:.1f} ns/window")
    print(f"intercept = {intercept:.1f}")
    print(f"send: mean={sm:.1f} std={ss:.1f} var={sv:.0f}")
    print(f"IPC x{IPC_COUNT}: mean={cm:.1f} std={cs:.1f} var={cv:.0f}")

    baseline = lat[ws[0]]
    labels = ["constant", "+send+IPC"]
    preds = {}
    for w in ws:
        preds[w] = [
            predict(w, baseline, step),
            predict(w, baseline, pure_step, send=send, ipc=ipc),
        ]

    out = []
    out.append(f"Latency model  (STEP={step:.1f} = pure {pure_step:.1f} + send {sm:.0f} "
               f"+ {IPC_COUNT}xIPC {IPC_COUNT*cm:.0f}, intercept={intercept:.1f})")
    out.append(f"send mean={sm:.0f} std={ss:.1f}   IPC mean={cm:.0f} std={cs:.1f} "
               f"x{IPC_COUNT}/window\n")
    for w in ws[1:]:
        meas = lat[w]
        out.append(f"=== w={w}  (measured median {np.median(meas):.0f} ns) ===")
        out.append(f"{'pct':>7} {'measured':>11} " +
                   " ".join(f"{l:>11}" for l in labels) + " | " +
                   " ".join(f"{'d_'+l:>11}" for l in labels))
        for q in PCTS:
            mq = np.percentile(meas, q)
            vals = [np.percentile(p, q) for p in preds[w]]
            out.append(f"{q:>7} {mq:>11.0f} " +
                       " ".join(f"{v:>11.0f}" for v in vals) + " | " +
                       " ".join(f"{mq-v:>+11.0f}" for v in vals))
        out.append("")

    out.append("=== Variance explained (trimmed 0.1%) ===")
    out.append(f"{'w':>3} {'measured':>12} {'constant':>10} {'+send+IPC':>11}")
    vb = trimmed_var(baseline)
    for w in ws:
        vm = trimmed_var(lat[w])
        c = 100 * vb / vm
        e = 100 * (vb + (w - 1) * (sv + IPC_COUNT * cv)) / vm
        out.append(f"{w:>3} {vm:>12.0f} {c:>9.1f}% {e:>10.1f}%")

    print("\n" + "\n".join(out))

    target = os.path.join(OUTDIR, "model_cdf.pdf")
    with plt.rc_context(STYLE):
        plot_cdf(lat, preds, labels, target, stats, height)
    print(f"\nsaved model_cdf.pdf to {OUTDIR}")


if __name__ == "__main__":
    main()
