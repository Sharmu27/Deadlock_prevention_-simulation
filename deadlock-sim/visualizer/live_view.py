"""
Deadlock Simulator - LIVE Dashboard
------------------------------------
Run the C simulator in one terminal and this window in another.
Every action you perform in the simulator is reflected here within a
fraction of a second - no re-exporting, no re-running.

    python visualizer/live_view.py                 (live window)
    python visualizer/live_view.py --snapshot a.png  (save one frame, no window)

Panels:
    Resource Allocation Graph  - who holds / waits for what (dots = instances)
    Wait-For Graph             - cycle highlighted when a deadlock exists
    Live Allocation per Resource - stacked bars, update as resources move
    Process table + Event log
"""

import argparse
import json
import math
import os
import sys
import textwrap
import time

import matplotlib

if "--snapshot" in sys.argv:
    matplotlib.use("Agg")

import matplotlib.pyplot as plt
from matplotlib.gridspec import GridSpec
from matplotlib.patches import Circle, FancyArrowPatch, FancyBboxPatch

# ---------------------------------------------------------------- theme
BG = "#1C1F24"
CARD = "#2B2F36"
TEXT = "#E4E6E9"
MUTED = "#8A8F98"
TEAL = "#029E8C"
CORAL = "#E8604C"
AMBER = "#F0B429"
GOLD = "#FFD166"
PROC_COLORS = ["#2EC4B6", "#4C9BE8", "#B07CE8", "#6BCB77", "#FF8FB1",
               "#90E0EF", "#C77DFF", "#A8DADC", "#F4A261", "#8D99AE"]
STATE_NAMES = {0: "ACTIVE", 1: "DONE", 2: "KILLED"}

DEFAULT_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "..", "system_state.json")


def pcolor(i):
    return PROC_COLORS[i % len(PROC_COLORS)]


def load_state(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None          # missing / being written - try again next tick


def spread(k, lo=1.0, hi=9.0):
    if k == 1:
        return [5.0]
    return [hi - i * (hi - lo) / (k - 1) for i in range(k)]


def spacing(k, lo=1.0, hi=9.0):
    return (hi - lo) if k == 1 else (hi - lo) / (k - 1)


def arrow(ax, a, b, pa, pb, color, lw=1.8, ls="-", rad=0.12, z=2):
    ax.add_patch(FancyArrowPatch(
        a, b, arrowstyle="-|>", mutation_scale=13, lw=lw, color=color, ls=ls,
        connectionstyle=f"arc3,rad={rad}", patchA=pa, patchB=pb,
        shrinkA=0, shrinkB=0, zorder=z))


def arc_mid(a, b, rad):
    """Midpoint of an arc3 curve (used to place edge labels)."""
    mx, my = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
    dx, dy = b[0] - a[0], b[1] - a[1]
    return mx + 0.5 * rad * dy, my - 0.5 * rad * dx


def panel_title(ax, text):
    ax.set_title(text, color=TEXT, fontsize=11, fontweight="bold", loc="left", pad=6)


# ---------------------------------------------------------- derived info
def rag_cycle_edges(st):
    """RAG edges that belong to the deadlock cycle (only when deadlocked)."""
    req, asg = set(), set()
    cyc = st["cycle"]
    if not st["deadlock"] or len(cyc) < 2:
        return req, asg
    A, R = st["allocation"], st["request"]
    for a, b in zip(cyc, cyc[1:]):
        for j in range(st["num_resources"]):
            if R[a][j] > 0 and A[b][j] > 0:
                req.add((a, j))
                asg.add((j, b))
    return req, asg


def changed_processes(st, prev):
    if not prev or prev.get("process_names") != st["process_names"]:
        return set()
    out = set()
    for i in range(st["num_processes"]):
        if (st["allocation"][i] != prev["allocation"][i]
                or st["request"][i] != prev["request"][i]
                or st["process_state"][i] != prev["process_state"][i]):
            out.add(i)
    return out


def proc_status(st, i):
    s = st["process_state"][i]
    if s == 1:
        return "DONE", MUTED
    if s == 2:
        return "KILLED", CORAL
    if i in st["deadlocked"]:
        return "BLOCKED", CORAL
    if any(v > 0 for v in st["request"][i]):
        return "WAITING", AMBER
    return "ACTIVE", TEAL


# ---------------------------------------------------------------- panels
def draw_banner(ax, st):
    ax.axis("off")
    names = st["process_names"]
    active = [i for i in range(st["num_processes"]) if st["process_state"][i] == 0]

    if st["deadlock"]:
        color = CORAL
        msg = "DEADLOCK DETECTED   |   blocked: " + ", ".join(names[i] for i in st["deadlocked"])
    elif not active:
        color = TEAL
        msg = "ALL PROCESSES COMPLETE   |   every resource has been returned"
    elif not st["safe"]:
        color = AMBER
        msg = "UNSAFE STATE   |   no safe sequence exists (deadlock may follow)"
    else:
        color = TEAL
        seq = [names[i] for i in st["safe_sequence"] if st["process_state"][i] == 0]
        msg = "SAFE STATE   |   safe sequence:  " + "  \u2192  ".join(seq)

    ax.add_patch(FancyBboxPatch((0, 0.05), 1, 0.9, boxstyle="round,pad=0,rounding_size=0.06",
                                fc=color, ec="none", transform=ax.transAxes))
    ax.text(0.015, 0.5, msg, transform=ax.transAxes, color="#10131A" if color == AMBER else "white",
            fontsize=15, fontweight="bold", va="center")
    pol = st["policies"]
    sub = (f"Prevention:  Hold&Wait {'ON' if pol['no_hold_and_wait'] else 'off'}   "
           f"Ordering {'ON' if pol['resource_ordering'] else 'off'}")
    ax.text(0.985, 0.30, sub, transform=ax.transAxes, color="white", alpha=0.85,
            fontsize=9, ha="right", va="center")
    return ax.text(0.985, 0.70, "\u25CF LIVE", transform=ax.transAxes, color="white",
                   fontsize=11, fontweight="bold", ha="right", va="center")


def draw_rag(ax, st, changed):
    n, m = st["num_processes"], st["num_resources"]
    names, rnames = st["process_names"], st["resource_names"]
    A, R, T, pst = st["allocation"], st["request"], st["total"], st["process_state"]
    cyc_req, cyc_asg = rag_cycle_edges(st)

    ax.set_xlim(0, 14)
    ax.set_ylim(-0.9, 10)
    ax.set_aspect("equal")
    ax.axis("off")
    panel_title(ax, "Resource Allocation Graph")

    PX, RX, BW = 1.8, 10.4, 2.9
    ys_p, ys_r = spread(n), spread(m)
    pr = min(0.62, 0.36 * spacing(n))
    bh = min(1.15, 0.72 * spacing(m))

    # resources: boxes with one dot per instance (filled = held by that process)
    rbox = {}
    for j in range(m):
        y = ys_r[j]
        box = FancyBboxPatch((RX - BW / 2, y - bh / 2), BW, bh,
                             boxstyle="round,pad=0,rounding_size=0.15",
                             fc=CARD, ec="#6B7078", lw=1.6, zorder=3)
        ax.add_patch(box)
        rbox[j] = box
        ax.text(RX + BW / 2 + 0.2, y, rnames[j], color=TEXT, fontsize=11,
                fontweight="bold", va="center", ha="left")
        t = T[j]
        holders = [i for i in range(n) for _ in range(A[i][j])]
        slots = holders + [None] * (t - len(holders))
        if t <= 12:
            rows = 1 if t <= 6 else 2
            per_row = t if rows == 1 else 6
            dr = min(0.14, bh * 0.16)
            for k, owner in enumerate(slots):
                r, c = divmod(k, per_row)
                cnt = min(per_row, t - r * per_row)
                x = RX + (c - (cnt - 1) / 2) * 0.44
                yy = y if rows == 1 else y + (0.2 if r == 0 else -0.2)
                ax.add_patch(Circle((x, yy), dr, fc=pcolor(owner) if owner is not None else "none",
                                    ec=pcolor(owner) if owner is not None else "#8A8F98",
                                    lw=1.2, zorder=4))
        else:
            ax.text(RX, y, f"{t - len(holders)} / {t} free", color=TEXT, fontsize=9,
                    ha="center", va="center", zorder=4)

    # processes
    pnode = {}
    for i in range(n):
        s = pst[i]
        if s == 0:
            fc, ec, tc = pcolor(i), "white", "#10131A"
        elif s == 1:
            fc, ec, tc = "#33373F", "#6B7078", MUTED
        else:
            fc, ec, tc = "#33373F", CORAL, MUTED
        lw = 1.6
        if i in changed:
            ec, lw = GOLD, 3.6
        node = Circle((PX, ys_p[i]), pr, fc=fc, ec=ec, lw=lw, zorder=4)
        ax.add_patch(node)
        pnode[i] = node
        ax.text(PX, ys_p[i], names[i], color=tc, fontsize=11 if len(names[i]) <= 3 else 8,
                fontweight="bold", ha="center", va="center", zorder=5)
        if n <= 6:
            label, lc = proc_status(st, i)
            ax.text(PX, ys_p[i] - pr - 0.22, label.lower(), color=lc, fontsize=8,
                    ha="center", va="top")

    # edges
    for i in range(n):
        for j in range(m):
            if A[i][j] > 0:
                a, b = (RX, ys_r[j]), (PX, ys_p[i])
                hot = (j, i) in cyc_asg
                arrow(ax, a, b, rbox[j], pnode[i], CORAL if hot else pcolor(i),
                      lw=2.6 if hot else 1.8)
                if A[i][j] > 1:
                    lx, ly = arc_mid(a, b, 0.12)
                    ax.text(lx, ly, f"x{A[i][j]}", color=TEXT, fontsize=8, ha="center",
                            va="center", zorder=6,
                            bbox=dict(boxstyle="round,pad=0.15", fc=BG, ec="none"))
            if R[i][j] > 0:
                a, b = (PX, ys_p[i]), (RX, ys_r[j])
                hot = (i, j) in cyc_req
                arrow(ax, a, b, pnode[i], rbox[j], CORAL if hot else "#D6D9DD",
                      lw=2.6 if hot else 1.5, ls="--")
                if R[i][j] > 1:
                    lx, ly = arc_mid(a, b, 0.12)
                    ax.text(lx, ly, f"x{R[i][j]}", color=TEXT, fontsize=8, ha="center",
                            va="center", zorder=6,
                            bbox=dict(boxstyle="round,pad=0.15", fc=BG, ec="none"))

    # legend
    ax.plot([0.2, 1.0], [-0.55, -0.55], color=TEAL, lw=2)
    ax.text(1.15, -0.55, "assigned (R\u2192P)", color=MUTED, fontsize=8, va="center")
    ax.plot([5.2, 6.0], [-0.55, -0.55], color="#D6D9DD", lw=1.5, ls="--")
    ax.text(6.15, -0.55, "waiting request (P\u2192R)", color=MUTED, fontsize=8, va="center")
    ax.add_patch(Circle((11.2, -0.55), 0.12, fc="none", ec="#8A8F98", lw=1.2))
    ax.text(11.45, -0.55, "free instance", color=MUTED, fontsize=8, va="center")


def draw_wfg(ax, st, changed):
    n = st["num_processes"]
    names, pst, W = st["process_names"], st["process_state"], st["wfg"]
    cyc = st["cycle"]
    cyc_edges = set(zip(cyc, cyc[1:])) if len(cyc) > 1 else set()
    hot_color = CORAL if st["deadlock"] else AMBER

    ax.set_xlim(0, 10)
    ax.set_ylim(0, 10)
    ax.set_aspect("equal")
    ax.axis("off")
    panel_title(ax, "Wait-For Graph")

    R = 3.3
    pos = []
    for i in range(n):
        if n == 1:
            pos.append((5.0, 5.0))
        else:
            ang = math.pi / 2 - 2 * math.pi * i / n
            pos.append((5 + R * math.cos(ang), 5 + R * math.sin(ang)))
    nr = 0.7 if n <= 6 else 0.5

    nodes = {}
    for i in range(n):
        s = pst[i]
        if s == 0:
            fc, ec, tc = pcolor(i), "white", "#10131A"
        elif s == 1:
            fc, ec, tc = "#33373F", "#6B7078", MUTED
        else:
            fc, ec, tc = "#33373F", CORAL, MUTED
        lw = 1.6
        if i in changed:
            ec, lw = GOLD, 3.6
        if i in cyc and st["deadlock"] and s == 0:
            ec, lw = CORAL, 3.2
        c = Circle(pos[i], nr, fc=fc, ec=ec, lw=lw, zorder=4)
        ax.add_patch(c)
        nodes[i] = c
        ax.text(pos[i][0], pos[i][1], names[i], color=tc, fontsize=10 if len(names[i]) <= 3 else 8,
                fontweight="bold", ha="center", va="center", zorder=5)

    any_edge = False
    for i in range(n):
        for k in range(n):
            if W[i][k]:
                any_edge = True
                hot = (i, k) in cyc_edges
                arrow(ax, pos[i], pos[k], nodes[i], nodes[k], hot_color if hot else "#8A8F98",
                      lw=3.0 if hot else 1.5, rad=0.2)
    if not any_edge:
        ax.text(5, 5, "no process is\ncurrently waiting", color=MUTED, fontsize=11,
                ha="center", va="center", style="italic", zorder=6)
    elif cyc_edges and not st["deadlock"]:
        ax.text(5, 0.2, "cycle exists, but spare instances\nlet a process finish: no deadlock",
                color=AMBER, fontsize=8, ha="center", va="bottom")
    elif st["deadlock"]:
        ax.text(5, 0.2, "cycle  " + " \u2192 ".join(names[i] for i in cyc),
                color=CORAL, fontsize=9, ha="center", va="bottom", fontweight="bold")


def draw_bars(ax, st, changed):
    n, m = st["num_processes"], st["num_resources"]
    A, T, av = st["allocation"], st["total"], st["available"]
    names, rnames = st["process_names"], st["resource_names"]

    ax.set_facecolor(BG)
    for sp in ("top", "right"):
        ax.spines[sp].set_visible(False)
    for sp in ("left", "bottom"):
        ax.spines[sp].set_color("#4A4F58")
    ax.tick_params(colors=MUTED, labelsize=9)
    panel_title(ax, "Live Allocation  (hatched = free)")

    for j in range(m):
        y = m - 1 - j
        left = 0
        for i in range(n):
            c = A[i][j]
            if c > 0:
                ax.barh(y, c, left=left, height=0.6, color=pcolor(i), ec=BG, lw=1.2)
                if c >= 1:
                    ax.text(left + c / 2, y, names[i], ha="center", va="center",
                            fontsize=8, color="#10131A", fontweight="bold")
                left += c
        if av[j] > 0:
            ax.barh(y, av[j], left=left, height=0.6, fc=CARD, ec="#6B7078",
                    hatch="///", lw=1.0)
        ax.text(T[j] + 0.2, y, f"{av[j]} free", va="center", fontsize=8.5,
                color=TEAL if av[j] > 0 else CORAL)

    ax.set_yticks(range(m))
    ax.set_yticklabels(list(reversed(rnames)), color=TEXT, fontsize=10, fontweight="bold")
    ax.set_xlim(0, max(T) * 1.3 + 0.5)
    ax.set_ylim(-0.6, m - 0.4)


def draw_table(ax, st, changed):
    ax.axis("off")
    n = st["num_processes"]
    panel_title(ax, "Process State   (vectors ordered " + " ".join(st["resource_names"]) + ")")

    def v(x):
        return " ".join(str(c) for c in x)

    rows = []
    for i in range(n):
        label, _ = proc_status(st, i)
        rows.append([st["process_names"][i], label, v(st["allocation"][i]), v(st["max"][i]),
                     v(st["need"][i]), v(st["request"][i]) if any(st["request"][i]) else "-"])
    cols = ["Process", "State", "Allocation", "Max", "Need", "Waiting for"]
    tbl = ax.table(cellText=rows, colLabels=cols, cellLoc="center",
                   colWidths=[0.11, 0.14, 0.19, 0.19, 0.19, 0.18], bbox=[0, 0, 1, 0.92])
    tbl.auto_set_font_size(False)
    tbl.set_fontsize(10 if n <= 6 else 8)
    for (r, c), cell in tbl.get_celld().items():
        cell.set_edgecolor(BG)
        txt = cell.get_text()
        if r == 0:
            cell.set_facecolor("#343841")
            txt.set_color("white")
            txt.set_fontweight("bold")
            continue
        i = r - 1
        cell.set_facecolor("#33384A" if i in changed else (CARD if r % 2 else "#252930"))
        txt.set_color(TEXT)
        if c == 0:
            txt.set_color(pcolor(i))
            txt.set_fontweight("bold")
        if c == 1:
            txt.set_color(proc_status(st, i)[1])
            txt.set_fontweight("bold")
        if i in changed:
            cell.set_edgecolor(GOLD)


def draw_events(ax, st):
    ax.axis("off")
    panel_title(ax, "Event Log  (newest first)")
    y = 0.93
    for k, ev in enumerate(st["events"]):
        lines = textwrap.wrap(ev, 46) or [""]
        if y - 0.075 * len(lines) < 0:
            break
        col = TEXT if k == 0 else MUTED
        for li, line in enumerate(lines):
            prefix = "\u203A " if li == 0 else "   "
            ax.text(0.01, y, prefix + line, transform=ax.transAxes, color=col, fontsize=8.5,
                    family="monospace", va="top", fontweight="bold" if k == 0 else "normal")
            y -= 0.075
        y -= 0.02


# ------------------------------------------------------------- dashboard
def build_dashboard(fig, st, prev, stamp):
    fig.clf()
    fig.set_facecolor(BG)
    gs = GridSpec(3, 3, figure=fig, height_ratios=[0.55, 3.3, 2.3],
                  width_ratios=[1.5, 1.0, 1.1], left=0.025, right=0.985,
                  top=0.975, bottom=0.04, hspace=0.28, wspace=0.12)
    changed = changed_processes(st, prev)

    ax_ban = fig.add_subplot(gs[0, :])
    live = draw_banner(ax_ban, st)
    draw_rag(fig.add_subplot(gs[1, 0]), st, changed)
    draw_wfg(fig.add_subplot(gs[1, 1]), st, changed)
    draw_bars(fig.add_subplot(gs[1, 2]), st, changed)
    draw_table(fig.add_subplot(gs[2, 0:2]), st, changed)
    draw_events(fig.add_subplot(gs[2, 2]), st)

    fig.text(0.985, 0.012, f"update #{st['version']}  \u00b7  {stamp}", color=MUTED,
             fontsize=8, ha="right")
    fig.text(0.025, 0.012, "CS23501 Operating Systems", color=MUTED, fontsize=8, ha="left")
    label = st.get("label", "")
    if label:
        fig.text(0.5, 0.012, label, color=TEXT, fontsize=10, fontweight="bold", ha="center")
    return live


def draw_waiting(fig, path):
    fig.clf()
    fig.set_facecolor(BG)
    fig.text(0.5, 0.56, "Waiting for the simulator...", color=TEXT, fontsize=24,
             fontweight="bold", ha="center")
    fig.text(0.5, 0.46, "Start simulator.exe from the project folder in another terminal.",
             color=MUTED, fontsize=13, ha="center")
    fig.text(0.5, 0.40, f"Watching: {os.path.abspath(path)}", color=MUTED, fontsize=9, ha="center")


class Viewer:
    HIGHLIGHT_SECONDS = 2.5

    def __init__(self, fig, path):
        self.fig, self.path = fig, path
        self.state = self.prev = None
        self.live = None
        self.stamp = ""
        self.changed_at = 0.0
        self.highlight_on = False
        self.beat = -1
        self.waiting_drawn = False
        self.redraws = 0

    def _redraw(self, with_highlight):
        prev = self.prev if with_highlight else None
        self.live = build_dashboard(self.fig, self.state, prev, self.stamp)
        self.highlight_on = with_highlight and prev is not None
        self.redraws += 1

    def tick(self):
        now = time.time()
        dirty = False
        st = load_state(self.path)

        if st is None:
            if self.state is None and not self.waiting_drawn:
                draw_waiting(self.fig, self.path)
                self.waiting_drawn = True
                dirty = True
        elif st != self.state:
            self.prev, self.state = self.state, st
            self.changed_at = now
            self.stamp = time.strftime("%H:%M:%S")
            self._redraw(True)
            dirty = True
        elif self.highlight_on and now - self.changed_at > self.HIGHLIGHT_SECONDS:
            self._redraw(False)             # fade the "just changed" outline
            dirty = True

        beat = int(now) % 2
        if self.live is not None and beat != self.beat:
            self.beat = beat
            self.live.set_alpha(1.0 if beat else 0.35)
            dirty = True

        if dirty:
            self.fig.canvas.draw_idle()


def main():
    ap = argparse.ArgumentParser(description="Live dashboard for the deadlock simulator")
    ap.add_argument("file", nargs="?", default=DEFAULT_FILE,
                    help="state file written by the simulator (default: ../system_state.json)")
    ap.add_argument("--snapshot", metavar="PNG", help="save one frame to PNG and exit")
    ap.add_argument("--interval", type=int, default=300, help="poll interval in ms")
    args = ap.parse_args()

    fig = plt.figure(figsize=(14, 8), dpi=100, facecolor=BG)
    viewer = Viewer(fig, args.file)

    if args.snapshot:
        viewer.tick()
        fig.savefig(args.snapshot, dpi=110, facecolor=BG)
        print(f"Saved {args.snapshot}")
        return

    try:
        fig.canvas.manager.set_window_title("Deadlock Simulator - Live Dashboard")
    except Exception:
        pass
    timer = fig.canvas.new_timer(interval=args.interval)
    timer.add_callback(viewer.tick)
    timer.start()
    viewer.tick()
    plt.show()


if __name__ == "__main__":
    main()
