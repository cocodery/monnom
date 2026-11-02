# Richards

Martin Richards' operating system kernel simulation: a scheduler running idle, worker, handler and device tasks that pass packets between them. Ported from the Are We Fast Yet suite (Marr, Daloze, Mössenböck, DLS 2016, MIT licence, https://github.com/smarr/are-we-fast-yet).

**Check:** one run must give queuePacketCount 23246 and holdCount 9297, the published values. The program runs it 1000 times and prints the totals and the number of bad runs (0).

**Files:** `types.mn` interfaces, `data.mn` packets and task state, `tcb.mn` task control blocks, `kernel.mn` the scheduler and the task logic, `main.mn` the driver.

**Differences from the original:**
- MonNom has no null, so `NoPacket` and `NoTcb` stand in for it, and `NO_WORK == x` becomes `x.IsNone()`. This adds interface calls that the original does not have.
- There is no inheritance, so the `RBObject` and `TaskControlBlock` logic (`append`, `addInputAndCheckPriority`, `runTask`) is static typed code in `Queue`.
- A task function holds its data record in a field, instead of receiving it as an untyped argument.
- A packet's four data words are fields.

**Open question:** with `data.mn` and `tcb.mn` both records under a typed kernel (e.g. `SCSCISCIIIIIIIDSSSCCSSC`) the run takes about 6 s in both modes and `--trans` gains nothing. This should be the best case. The cause has not been found.

## Results, 2026-11-02

Base against `--trans`, median of 3 runs per configuration, 4 jobs at a time, each pinned to two P-cores. All 162 configurations returned the right result. Positive is faster.

| Measure | Value |
|---|---:|
| Mean time, base / trans | 2.766 s / 2.632 s |
| Median gain | 0.6% |
| Best / worst gain | 35.5% / -7.7% |
| Configurations at 20% or more | 14 of 162 |
| Configurations at -5% or worse | 2 of 162 |

Target configuration: CCCCICCIIIIIIICCSCCCSCC: `tcb.mn` as records under a typed kernel.

The gain appears when a typed file makes interface calls on record objects. Configurations with all classes, or with the interfaces removed, take other paths and stay within noise.
