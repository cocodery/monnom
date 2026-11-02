# DeltaBlue

The incremental dataflow constraint solver of Freeman-Benson, Maloney and Borning. Ported from Are We Fast Yet (MIT licence) and Octane (BSD licence).

**Check:** the chain and projection tests verify every computed value themselves. The program prints the iteration count, the problem size and the number of bad values (0).

**Files:** `types.mn` interfaces, `objects.mn` strengths, variables and vectors, `constraints.mn` the four constraint kinds, `planner.mn` the planner and the shared constraint logic, `main.mn` the two tests.

**Differences from the original:**
- MonNom has no null, so `NoConstraint` stands in for it.
- There is no inheritance, so the `AbstractConstraint`, `UnaryConstraint` and `BinaryConstraint` code is static typed code in `Ops`, `Unary` and `Binary`, or in `Planner`. The constraint classes hold the state.
- Constraints are compared by an `Id()`.
- `ConstraintVector` and `VariableVector` are project classes, not a generic library class: nombench generates invalid configurations for a library generic instantiated with a project interface, because its validity check does not look inside the type arguments.
- `Ops.Detach` uses `break`: nombench crashes on a bare `return;`.

## Results, 2026-11-02

Base against `--trans`, median of 3 runs per configuration, 4 jobs at a time, each pinned to two P-cores. All 162 configurations returned the right result. Positive is faster.

| Measure | Value |
|---|---:|
| Mean time, base / trans | 1.732 s / 1.688 s |
| Median gain | -0.6% |
| Best / worst gain | 25.6% / -3.8% |
| Configurations at 20% or more | 9 of 162 |
| Configurations at -5% or worse | 0 of 162 |

Target configuration: CSCCIIIIICSCCCCCSCSS: `objects.mn` as records under a typed planner.

The gain appears when a typed file makes interface calls on record objects. Configurations with all classes, or with the interfaces removed, take other paths and stay within noise.
