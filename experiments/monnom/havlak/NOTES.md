# Havlak

Paul Havlak's loop recognition over a control flow graph, as in Hundt, "Loop Recognition in C++/Java/Go/Scala" (2011). Ported from Are We Fast Yet (MIT licence).

**This one does not show an IMT gain.** It is kept as a correct real program, and as a case where the optimization does not help. The cause has not been profiled.

**Check:** 6102 loops and 5213 nodes for 1500 dummy loops, the published values. `main.mn` runs 300 find iterations in place of 50 to reach about 1 s; the check does not depend on that count.

**Input:** at 15000 dummy loops (published result 46602 loops, which this port also gives) configurations take 12-47 s and showed gains up to 38%, but single runs also swung to -36%. There, the time goes to linear scans of the root loop's growing child list. The original uses a hash set for these, so that figure would measure this port's list, and it is not used.

**Differences from the original:** `NoBlock`, `NoLoop` and `NoNode` stand in for null, the block-to-number map is an array indexed by block name, sets are linear scans, and edge objects are not kept.

## Results, 2026-11-02

Base against `--trans`, median of 3 runs per configuration, 4 jobs at a time, each pinned to two P-cores. All 162 configurations returned the right result. Positive is faster.

| Measure | Value |
|---|---:|
| Mean time, base / trans | 1.114 s / 1.115 s |
| Median gain | -0.3% |
| Best / worst gain | 6.6% / -6.0% |
| Configurations at 20% or more | 0 of 162 |
| Configurations at -5% or worse | 1 of 162 |

Target configuration: (none: no configuration reaches 10%).

The gain appears when a typed file makes interface calls on record objects. Configurations with all classes, or with the interfaces removed, take other paths and stay within noise.
