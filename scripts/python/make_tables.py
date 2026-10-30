#!/usr/bin/env python3
# usage: bugs/1031/make_tables.py <outdir>
# Reads <outdir>/raw/<bench>/*.p*.csv (run_perf.sh) and writes, per benchmark,
# tables/<bench>.md (variation, base, trans, trans-sync: median seconds) and
# tables/<bench>.csv (the same plus % faster than base), and SUMMARY.md.
# A mode with a failed run shows FAIL. "% faster" is 100 * (1 - mode / base).
import csv, glob, os, statistics as st, sys

OUT = sys.argv[1]
MODES = ["base", "trans", "trans-sync"]


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
    return {m: None if m in failed or not runs[m] else st.median(runs[m])
            for m in MODES}


os.makedirs(OUT + "/tables", exist_ok=True)
summary = []
for bdir in sorted(glob.glob(OUT + "/raw/*/")):
    b = os.path.basename(bdir.rstrip("/"))
    paths = {}
    for path in glob.glob(bdir + "*.p*.csv"):
        paths.setdefault(os.path.basename(path).split(".p")[0], []).append(path)
    rows = [(name, load(paths[name])) for name in sorted(paths)]
    with open("%s/tables/%s.md" % (OUT, b), "w") as f:
        f.write("| %s | base | trans | trans-sync |\n" % b)
        f.write("|---|---:|---:|---:|\n")
        for name, t in rows:
            cells = ["%.3f s" % t[m] if t[m] is not None else "FAIL"
                     for m in MODES]
            f.write("| %s | %s |\n" % (name, " | ".join(cells)))
    with open("%s/tables/%s.csv" % (OUT, b), "w") as f:
        w = csv.writer(f)
        w.writerow(["variation", "base", "trans", "trans-sync",
                    "trans_faster_pct", "sync_faster_pct"])
        for name, t in rows:
            pct = ["%.1f" % faster(t["base"], t[m])
                   if t["base"] and t[m] else "" for m in MODES[1:]]
            w.writerow([name] + ["%.3f" % t[m] if t[m] is not None else "FAIL"
                                 for m in MODES] + pct)
    full = [t for _, t in rows if None not in t.values()]
    fails = {m: sum(t[m] is None for _, t in rows) for m in MODES}
    tr = [faster(t["base"], t["trans"]) for t in full]
    sy = [faster(t["base"], t["trans-sync"]) for t in full]
    summary.append((b, len(rows), fails, full, tr, sy))

with open(OUT + "/SUMMARY.md", "w") as f:
    f.write("| Benchmark | Variations | Failed runs (base/trans/sync) | Median base | Median trans | Median trans-sync | Trans faster (median %) | Trans-sync faster (median %) | Best trans % | Worst trans % |\n")
    f.write("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n")
    for b, n, fails, full, tr, sy in summary:
        if not full:
            f.write("| %s | %d | %d/%d/%d | | | | | | | |\n"
                    % (b, n, fails["base"], fails["trans"], fails["trans-sync"]))
            continue
        med = [st.median(t[m] for t in full) for m in MODES]
        f.write("| %s | %d | %d/%d/%d | %.3f | %.3f | %.3f | %.1f | %.1f | %.1f | %.1f |\n"
                % (b, n, fails["base"], fails["trans"], fails["trans-sync"],
                   med[0], med[1], med[2], st.median(tr), st.median(sy),
                   max(tr), min(tr)))
