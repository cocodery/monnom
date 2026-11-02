#!/usr/bin/env python3
# usage: scripts/python/lattice.py <out.png> <benchmark dir>
#            [--save-baseline FILE | --baseline FILE] [--failed NAME...]
# Draws the performance lattice of Takikawa et al., "Is Sound Gradual Typing
# Dead?" (POPL 2016) from <benchmark dir>/results.csv: one node per
# configuration, fully untyped at the bottom, fully typed at the top, and an
# edge for each step that adds types to one unit. A node shows its running time
# (mean of its runs) relative to the fully untyped configuration. Writes
# <out.png> and, for hovering over large lattices, <out>.html.
# --save-baseline stores the fully untyped time in FILE and --baseline measures
# against the time in FILE instead, which puts several modes on one scale.
#
# A unit is a group of letters of the configuration name that always change
# together: with nombench --byfile, a file's classes or its interfaces. Its
# versions are ordered C < D < S,L < T,M,U for classes and I < J < K for
# interfaces, most typed first, and a step moves one unit to the next version.
# Only the configurations in plotconfig.json's mapping are drawn, the ones
# newplot.py charts. A configuration is failed when its results are incomplete
# or differ from the majority's, or when it is named after --failed; it stays
# in the lattice, marked as failed, so the rest of a long run is not lost.
import collections, csv, json, math, os, statistics as st, sys
import plotly.graph_objects as go

LETTER_RANK = dict(C=0, I=0, D=1, J=1, S=2, L=2, K=2, T=3, M=3, U=3)
# above this many nodes the cells and numbers no longer fit
MAX_DETAILED = 64
# colours are fixed at 0.5x to 2x so that charts can be compared
COLOUR_LIMIT = 1.0
SLOWER, UNCHANGED, FASTER = (200, 65, 58), (240, 239, 236), (31, 111, 196)
INK, MUTED, EDGE, BORDER = "#1f1f1d", "#6b6a66", "#c9c8c3", "#a9a8a3"
CELL_COLOURS = ["#1f1f1d", "#8c8b86", "#ffffff"]


def load_results(bench, failed):
    config = json.load(open(bench + "/plotconfig.json"))
    lines, timecol = config["lines"], config["time"]
    rows = {}
    for row in open(bench + "/results.csv"):
        cols = row.rstrip("\n").split(",")
        rows[cols[0][len(".BM_"):]] = cols[1:]
    if config.get("mapping"):
        with open(bench + "/" + config["mapping"]) as f:
            mapped = {r[0] for r in list(csv.reader(f))[1:]}
        rows = {n: v for n, v in rows.items() if n in mapped}
    width = collections.Counter(len(v) for v in rows.values()).most_common(1)[0][0]
    times, answers = {}, {}
    for name, vals in rows.items():
        try:
            if len(vals) == width and width % lines == 0:
                times[name] = [float(v) for i, v in enumerate(vals)
                               if i % lines == timecol]
                answers[name] = tuple(v.strip() for i, v in enumerate(vals)
                                      if i % lines != timecol)
        except ValueError:
            pass
    if not answers:
        sys.exit("lattice.py: no configuration in %s has complete results" % bench)
    common = collections.Counter(answers.values()).most_common(1)[0][0]
    failed = set(failed) | (rows.keys() - times.keys())
    failed |= {n for n, a in answers.items() if a != common}
    runs = width // lines
    return sorted(rows), {n: t for n, t in times.items() if n not in failed}, runs


def find_units(names):
    """Groups the letter positions that vary into units: position i joins j
    when i's letter determines j's in every configuration."""
    live = [i for i in range(len(names[0])) if len({n[i] for n in names}) > 1]
    parent = {i: i for i in live}

    def root(i):
        while parent[i] != i:
            i = parent[i]
        return i

    for i in live:
        for j in live:
            if i != j and len({(n[i], n[j]) for n in names}) == len({n[i] for n in names}):
                parent[root(i)] = root(j)
    units = collections.defaultdict(list)
    for i in live:
        units[root(i)].append(i)
    return sorted(units.values())


def unit_ranks(names, units):
    """Maps each configuration to its version of each unit, 0 for most typed."""
    versions = []
    for unit in units:
        found = {"".join(n[i] for i in unit) for n in names}
        ordered = sorted(found, key=lambda v: sum(LETTER_RANK[c] for c in v))
        versions.append({v: rank for rank, v in enumerate(ordered)})
    return {n: tuple(versions[u]["".join(n[i] for i in unit)]
                     for u, unit in enumerate(units)) for n in names}, versions


def find_edges(ranks):
    by_ranks = {r: n for n, r in ranks.items()}
    edges = []
    for name, r in ranks.items():
        for u in range(len(r)):
            typed = r[:u] + (r[u] - 1,) + r[u + 1:]
            if typed in by_ranks:
                edges.append((name, by_ranks[typed]))
    return edges


def place_nodes(ranks, versions, edges):
    """Puts each node on the level of its steps from fully untyped, ordering
    each level by the positions of the nodes below it to limit crossings."""
    top = sum(len(v) - 1 for v in versions)
    levels = collections.defaultdict(list)
    for name, r in ranks.items():
        levels[top - sum(r)].append(name)
    below = collections.defaultdict(list)
    for lower, upper in edges:
        below[upper].append(lower)
    pos = {}
    for level in sorted(levels):
        def key(name):
            xs = [pos[n][0] for n in below[name] if n in pos]
            return (st.mean(xs) if xs else 0, ranks[name])
        nodes = sorted(levels[level], key=key)
        for i, name in enumerate(nodes):
            pos[name] = (i - (len(nodes) - 1) / 2, level)
    return pos, max(len(v) for v in levels.values())


def colour_position(overhead):
    """-1 at twice as fast as untyped or better, 1 at twice as slow or worse."""
    return max(-1, min(1, math.log2(overhead) / COLOUR_LIMIT))


def overhead_colour(overhead):
    t = colour_position(overhead)
    pole = SLOWER if t > 0 else FASTER
    return tuple(round(m + abs(t) * (p - m)) for m, p in zip(UNCHANGED, pole))


def rgb(c):
    return "rgb(%d,%d,%d)" % c


def text_colour(c):
    return "#ffffff" if 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2] < 140 else INK


def untyped_baseline(pos, times):
    return min(times, key=lambda n: (pos[n][1], n))


def main():
    out, bench = sys.argv[1], sys.argv[2].rstrip("/")
    args = sys.argv[3:]
    baseline_option = args[:2] if args[:1] in (["--save-baseline"], ["--baseline"]) else []
    args = args[len(baseline_option):]
    failed = args[1:] if args[:1] == ["--failed"] else []
    names, times, runs = load_results(bench, failed)
    if not times:
        sys.exit("lattice.py: no configuration in %s passed" % bench)
    units = find_units(names)
    ranks, versions = unit_ranks(names, units)
    edges = find_edges(ranks)
    pos, widest = place_nodes(ranks, versions, edges)
    top = max(p[1] for p in pos.values())
    base = untyped_baseline(pos, times)
    base_time = st.mean(times[base])
    base_label = "fully untyped" if pos[base][1] == 0 else base
    if baseline_option[:1] == ["--save-baseline"]:
        with open(baseline_option[1], "w") as f:
            f.write("%r base mode %s" % (base_time, base_label))
    elif baseline_option and os.path.exists(baseline_option[1]):
        with open(baseline_option[1]) as f:
            saved_time, base_label = f.read().split(" ", 1)
        base_time = float(saved_time)
    elif baseline_option:
        print("WARNING: no saved baseline, using this mode's own %s" % base_label)
    overhead = {n: st.mean(t) / base_time for n, t in times.items()}
    detailed = len(names) <= MAX_DETAILED
    px_x, px_y = (104, 92) if detailed else (15, 70)

    fig = go.Figure()
    ex, ey = [], []
    # edges stop at the boxes, so they do not cross the numbers
    inset = 27 if detailed else 0
    for a, b in edges:
        ex += [pos[a][0] * px_x, pos[b][0] * px_x, None]
        ey += [pos[a][1] * px_y + inset, pos[b][1] * px_y - inset, None]
    fig.add_trace(go.Scatter(x=ex, y=ey, mode="lines", hoverinfo="skip",
                             showlegend=False,
                             line=dict(color=EDGE, width=1 if detailed else 0.5)))

    passed = [n for n in names if n in overhead]
    if detailed:
        for name in names:
            x, y = pos[name][0] * px_x, pos[name][1] * px_y
            fill = rgb(overhead_colour(overhead[name])) if name in overhead else "#ffffff"
            fig.add_shape(type="rect", x0=x - 44, x1=x + 44, y0=y - 27, y1=y + 27,
                          fillcolor=fill, layer="between",
                          line=dict(color=BORDER, width=1,
                                    dash="solid" if name in overhead else "dash"))
            colour = overhead_colour(overhead[name]) if name in overhead else (255, 255, 255)
            fig.add_annotation(x=x, y=y - 9, showarrow=False,
                               text="%.2f×" % overhead[name] if name in overhead else "failed",
                               font=dict(size=14, color=text_colour(colour)))
        cx, cy, cc = [], [], []
        for name in names:
            x, y = pos[name][0] * px_x, pos[name][1] * px_y
            for u, r in enumerate(ranks[name]):
                span = len(versions[u]) - 1
                cx.append(x + (u - (len(units) - 1) / 2) * 11)
                cy.append(y + 11)
                cc.append(CELL_COLOURS[2 * r // span])
        fig.add_trace(go.Scatter(x=cx, y=cy, mode="markers", hoverinfo="skip",
                                 showlegend=False,
                                 marker=dict(symbol="square", size=9, color=cc,
                                             line=dict(color=BORDER, width=1))))

    def hover(name):
        steps = "<br>".join("unit %d: %s" % (u + 1, "".join(name[i] for i in unit))
                            for u, unit in enumerate(units))
        if name not in overhead:
            return "%s<br>failed<br>%s" % (name, steps)
        return "%s<br>%.3f s, %.2f× untyped<br>%s" % (
            name, st.mean(times[name]), overhead[name], steps)

    # also carries the colour bar; invisible when the boxes show the values
    fig.add_trace(go.Scatter(
        x=[pos[n][0] * px_x for n in passed], y=[pos[n][1] * px_y for n in passed],
        mode="markers", showlegend=False, hoverinfo="text",
        hovertext=[hover(n) for n in passed],
        marker=dict(symbol="circle", size=46 if detailed else 9,
                    opacity=0.001 if detailed else 1,
                    color=[colour_position(overhead[n]) for n in passed],
                    cmin=-1, cmax=1,
                    colorscale=[[0, rgb(FASTER)], [0.5, rgb(UNCHANGED)], [1, rgb(SLOWER)]],
                    line=dict(color=BORDER, width=0.5),
                    colorbar=dict(title=dict(text="Time vs fully untyped", side="right"),
                                  tickvals=[-1, -0.5, 0, 0.5, 1], thickness=12, len=0.5,
                                  ticktext=["≤0.5×", "0.71×", "1×", "1.41×", "≥2×"],
                                  outlinewidth=0)))
    )
    bad = [n for n in names if n not in overhead]
    if bad and not detailed:
        fig.add_trace(go.Scatter(
            x=[pos[n][0] * px_x for n in bad], y=[pos[n][1] * px_y for n in bad],
            mode="markers", name="failed", hoverinfo="text",
            hovertext=[hover(n) for n in bad],
            marker=dict(symbol="x-thin-open", size=10, color=INK, line=dict(width=1.5))))

    half = (widest + 1) / 2 * px_x
    fig.add_annotation(x=0, y=top * px_y + 44, showarrow=False, text="fully typed",
                       font=dict(size=13, color=MUTED))
    fig.add_annotation(x=0, y=-44, showarrow=False, text="fully untyped",
                       font=dict(size=13, color=MUTED))
    if detailed:
        fig.add_annotation(x=0, y=-68, showarrow=False, font=dict(size=12, color=MUTED),
                           text="Each cell is one unit: black typed, grey untyped, "
                                "white structural or removed")
    ratios = overhead.values()
    title = os.path.basename(out)[:-len(".png")]
    subtitle = ("%d variations, %d run(s) each · baseline %s %.3f s · %d faster than it,"
                " %d within 2×, worst %.2f×%s"
                % (len(names), runs, base_label, base_time, sum(r < 1 for r in ratios), sum(r <= 2 for r in ratios),
                   max(ratios), " · %d failed" % len(bad) if bad else ""))
    width = max(760, int(2 * half) + 160)
    height = (top + 1) * px_y + 200
    fig.update_layout(
        width=width, height=height, plot_bgcolor="#fcfcfb", paper_bgcolor="#fcfcfb",
        title=dict(text="%s<br><sup>%s</sup>" % (title, subtitle), x=0.02,
                   font=dict(size=20, color=INK)),
        margin=dict(l=20, r=90, t=90, b=20), hovermode="closest",
        legend=dict(x=0, y=1),
        xaxis=dict(visible=False, range=[-half, half]),
        yaxis=dict(visible=False, range=[-86, top * px_y + 60]))
    fig.write_image(out, width=width, height=height, scale=2)
    fig.write_html(out[:-len(".png")] + ".html", include_plotlyjs="cdn")
    print("%d nodes, %d edges, %d units" % (len(names), len(edges), len(units)))


main()
