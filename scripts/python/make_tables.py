#!/usr/bin/env python3
# usage: scripts/python/make_tables.py <outdir> [--mean]
# Reads <outdir>/raw/<bench>/*.p*.csv and writes, per benchmark,
# tables/<bench>.md and tables/<bench>.csv (variation, base, trans, trans-sync:
# median seconds, and % faster than base), and SUMMARY.md. Variations are
# sorted by trans % faster, best first. The .md tables end with an average row,
# the mean of the variations' medians, with its % faster than base.
# With --mean a variation's time in each mode is the mean of its runs, not the
# median. The Median columns of SUMMARY.md are always across variations.
# If <outdir>/rates.csv exists (bench,variation,slow_calls_per_s) the tables
# also show millions of slow-path interface calls per second.
# A mode with a failed run shows FAIL. "% faster" is 100 * (1 - mode / base).
import csv, glob, os, statistics as st, sys

OUT = sys.argv[1]
MODES = ["base", "trans", "trans-sync"]
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
    rows.sort(key=lambda r: -faster(r[1]["base"], r[1]["trans"])
              if r[1]["base"] and r[1]["trans"] else float("inf"))
    has_rates = any((b, name) in RATES for name, _ in rows)
    with open("%s/tables/%s.md" % (OUT, b), "w") as f:
        f.write("| %s | base | trans | trans-sync | trans faster | sync faster%s |\n"
                % (b, " | slow calls (M/s)" if has_rates else ""))
        f.write("|---|---:|---:|---:|---:|---:%s|\n"
                % ("|---:" if has_rates else ""))
        for name, t in rows:
            cells = ["%.3f s" % t[m] if t[m] is not None else "FAIL"
                     for m in MODES]
            cells += ["%.1f%%" % faster(t["base"], t[m])
                      if t["base"] and t[m] else "" for m in MODES[1:]]
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
        w.writerow(["variation", "base", "trans", "trans-sync",
                    "trans_faster_pct", "sync_faster_pct"]
                   + (["slow_calls_per_s"] if has_rates else []))
        for name, t in rows:
            pct = ["%.1f" % faster(t["base"], t[m])
                   if t["base"] and t[m] else "" for m in MODES[1:]]
            w.writerow([name] + ["%.3f" % t[m] if t[m] is not None else "FAIL"
                                 for m in MODES] + pct
                       + (["%.0f" % RATES.get((b, name), 0)] if has_rates else []))
    full = [t for _, t in rows if None not in t.values()]
    fails = {m: sum(t[m] is None for _, t in rows) for m in MODES}
    tr = [faster(t["base"], t["trans"]) for t in full]
    sy = [faster(t["base"], t["trans-sync"]) for t in full]
    summary.append((b, len(rows), fails, full, tr, sy))

with open(OUT + "/SUMMARY.md", "w") as f:
    f.write("| Benchmark | Variations | Failed runs (base/trans/sync) | Average base | Average trans | Average trans-sync | Trans vs base (average %) | Trans-sync vs base (average %) | Median base | Median trans | Median trans-sync | Trans faster (median %) | Trans-sync faster (median %) | Best trans % | Worst trans % |\n")
    f.write("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n")
    for b, n, fails, full, tr, sy in summary:
        if not full:
            f.write("| %s | %d | %d/%d/%d | | | | | | | | | | | | |\n"
                    % (b, n, fails["base"], fails["trans"], fails["trans-sync"]))
            continue
        avg = [st.mean(t[m] for t in full) for m in MODES]
        med = [st.median(t[m] for t in full) for m in MODES]
        f.write("| %s | %d | %d/%d/%d | %.3f | %.3f | %.3f | %.1f | %.1f | %.3f | %.3f | %.3f | %.1f | %.1f | %.1f | %.1f |\n"
                % (b, n, fails["base"], fails["trans"], fails["trans-sync"],
                   avg[0], avg[1], avg[2], faster(avg[0], avg[1]),
                   faster(avg[0], avg[2]), med[0], med[1], med[2],
                   st.median(tr), st.median(sy), max(tr), min(tr)))
