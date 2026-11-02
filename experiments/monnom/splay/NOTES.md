# Splay

The top-down splay tree of Sleator and Tarjan. Ported from Octane's `splay.js` (BSD licence).

**Check:** there is no published value for this input, so `expected.txt` comes from an independent Python model (a sorted list) running the same operations. Size and order-sensitive checksum agree, and the tree checks its own key order.

**Files:** `types.mn` the node interface, `node.mn` the nodes, `tree.mn` the tree, `main.mn` the driver: build 8000 keys, then 1.5 million rounds of insert, find-greatest-less-than and remove.

**Differences from the original:**
- The payload trees are left out. They exist to stress the garbage collector, and would measure allocation here.
- Keys are Ints from a Park-Miller generator, in place of `Math.random`.
- `NoNode` stands in for null.
- `Checksum` and `Disorder` are in the tree so that no `ArrayList` crosses the boundary between `main.mn` and `tree.mn`: generics are invariant, so an `ArrayList<dyn>` from an untyped side fails the cast on the typed side.

## Results, 2026-11-02

Base against `--trans`, median of 3 runs per configuration, 4 jobs at a time, each pinned to two P-cores. All 54 configurations returned the right result. Positive is faster.

| Measure | Value |
|---|---:|
| Mean time, base / trans | 0.878 s / 0.843 s |
| Median gain | -0.6% |
| Best / worst gain | 31.3% / -7.3% |
| Configurations at 20% or more | 4 of 54 |
| Configurations at -5% or worse | 2 of 54 |

Target configuration: ICSCS: nodes as records under a typed tree.

The gain appears when a typed file makes interface calls on record objects. Configurations with all classes, or with the interfaces removed, take other paths and stay within noise.
