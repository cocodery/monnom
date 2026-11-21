#!/usr/bin/env python3
# usage: scripts/python/make_tables.py <outdir> [--mean]
# Reads <outdir>/raw/<bench>/*.p*.csv and writes, per benchmark,
# tables/<bench>.md and tables/<bench>.csv (variation, a column per mode in
# MODES: median seconds, and % faster than base), and SUMMARY.md. Variations
# are sorted by the second mode's % faster, best first. The .md tables end
# with an average row, the mean of the variations' medians, with its % faster
# than base.
# With --mean a variation's time in each mode is the mean of its runs, not the
# median. The Median columns of SUMMARY.md are always across variations.
# If <outdir>/rates.csv exists (bench,variation,slow_calls_per_s) the tables
# also show millions of slow-path interface calls per second.
# A mode with a failed run shows FAIL. "% faster" is 100 * (1 - mode / base).
# Runs of modes not in MODES (e.g. from older measurements) are ignored.
import csv, glob, os, statistics as st, sys

OUT = sys.argv[1]
# the modes run-gtp.sh and run-benchmark.sh run; the async "trans" mode is
# commented out there for now
# MODES = ["base", "trans", "trans-sync"]
MODES = ["base", "trans-sync"]
OTHERS = MODES[1:]
PER_RUN = st.mean if "--mean" in sys.argv[2:] else st.median
RATES = {}
if os.path.exists(OUT + "/rates.csv"):
    RATES = {(r[0], r[1]): float(r[2]) for r in csv.reader(open(OUT + "/rates.csv"))}


def faster(base, t):
    return 100 * (1 - t / base)


def load(paths):
    runs = {m: [] for m in MODES}
    failed = set()
    for path in paths:
        for name, mode, npass, code, ok, secs in csv.reader(open(path)):
            if mode not in runs:
                continue
            if code != "0" or ok != "1" or not secs:
                failed.add(mode)
            else:
                runs[mode].append(float(secs))
    return {m: None if m in failed or not runs[m] else PER_RUN(runs[m])
            for m in MODES}


os.makedirs(OUT + "/tables", exist_ok=True)
summary = []
for bdir in sorted(glob.glob(OUT + "/raw/*/")):
    b = os.path.basename(bdir.rstrip("/"))
    paths = {}
    for path in glob.glob(bdir + "*.p*.csv"):
        paths.setdefault(os.path.basename(path).split(".p")[0], []).append(path)
    rows = [(name, load(paths[name])) for name in sorted(paths)]
    rows.sort(key=lambda r: -faster(r[1]["base"], r[1][OTHERS[0]])
              if r[1]["base"] and r[1][OTHERS[0]] else float("inf"))
    has_rates = any((b, name) in RATES for name, _ in rows)
    with open("%s/tables/%s.md" % (OUT, b), "w") as f:
        f.write("| %s | %s | %s%s |\n"
                % (b, " | ".join(MODES), " | ".join(m + " faster" for m in OTHERS),
                   " | slow calls (M/s)" if has_rates else ""))
        f.write("|---|%s%s\n"
                % ("---:|" * (len(MODES) + len(OTHERS)), "---:|" if has_rates else ""))
        for name, t in rows:
            cells = ["%.3f s" % t[m] if t[m] is not None else "FAIL"
                     for m in MODES]
            cells += ["%.1f%%" % faster(t["base"], t[m])
                      if t["base"] and t[m] else "" for m in OTHERS]
            if has_rates:
                cells.append("%.1f" % (RATES.get((b, name), 0) / 1e6))
            f.write("| %s | %s |\n" % (name, " | ".join(cells)))
        complete = [t for _, t in rows if None not in t.values()]
        if complete:
            avg = [st.mean(t[m] for t in complete) for m in MODES]
            cells = ["**%.3f s**" % a for a in avg]
            cells += ["**%.1f%%**" % faster(avg[0], a) for a in avg[1:]]
            if has_rates:
                cells.append("")
            f.write("| **average** | %s |\n" % " | ".join(cells))
    with open("%s/tables/%s.csv" % (OUT, b), "w") as f:
        w = csv.writer(f)
        w.writerow(["variation"] + MODES
                   + [m.replace("-", "_") + "_faster_pct" for m in OTHERS]
                   + (["slow_calls_per_s"] if has_rates else []))
        for name, t in rows:
            pct = ["%.1f" % faster(t["base"], t[m])
                   if t["base"] and t[m] else "" for m in OTHERS]
            w.writerow([name] + ["%.3f" % t[m] if t[m] is not None else "FAIL"
                                 for m in MODES] + pct
                       + (["%.0f" % RATES.get((b, name), 0)] if has_rates else []))
    full = [t for _, t in rows if None not in t.values()]
    fails = {m: sum(t[m] is None for _, t in rows) for m in MODES}
    pcts = {m: [faster(t["base"], t[m]) for t in full] for m in OTHERS}
    summary.append((b, len(rows), fails, full, pcts))

columns = (["Benchmark", "Variations", "Failed runs (%s)" % "/".join(MODES)]
           + ["Average %s" % m for m in MODES]
           + ["%s vs base (average %%)" % m for m in OTHERS]
           + ["Median %s" % m for m in MODES]
           + ["%s faster (median %%)" % m for m in OTHERS]
           + ["Best %s %%" % OTHERS[0], "Worst %s %%" % OTHERS[0]])
with open(OUT + "/SUMMARY.md", "w") as f:
    f.write("| %s |\n" % " | ".join(columns))
    f.write("|---|%s\n" % ("---:|" * (len(columns) - 1)))
    for b, n, fails, full, pcts in summary:
        cells = [b, "%d" % n, "/".join("%d" % fails[m] for m in MODES)]
        if not full:
            f.write("| %s |\n" % " | ".join(cells + [""] * (len(columns) - 3)))
            continue
        avg = [st.mean(t[m] for t in full) for m in MODES]
        med = [st.median(t[m] for t in full) for m in MODES]
        cells += ["%.3f" % a for a in avg]
        cells += ["%.1f" % faster(avg[0], a) for a in avg[1:]]
        cells += ["%.3f" % m for m in med]
        cells += ["%.1f" % st.median(pcts[m]) for m in OTHERS]
        cells += ["%.1f" % max(pcts[OTHERS[0]]), "%.1f" % min(pcts[OTHERS[0]])]
        f.write("| %s |\n" % " | ".join(cells))
