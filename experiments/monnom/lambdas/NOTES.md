# Lambdas

A benchmark written for MonNom that spends its time calling function values. It exercises the three ways a lambda gets called:
- **Function objects** (`funs.mn`): `AddN`, `MulN`, `Compose`, `Plus`, `MaxOf` and `IsMultiple` each have only a `this` method. Map, filter and fold over a list of 1000 numbers call them through `IIntFun`, `IIntFun2` and `IIntPred`. In the `L` configurations the generator turns these classes into lambdas, so these calls become typed raw invokes on lambdas.
- **Strategy records** (`ops.mn`): `MakeOp` and `MakeDictOp` return records whose `IOp` methods are lambdas, in declared fields and in the record's dictionary respectively. The lambdas capture the argument, so the records of one site hold different lambdas. Every iteration creates new records. With `--trans` these interface calls go through the IMT entries for lambda fields.
- **Closures** (`main.mn`): typed `Fun<…>` lambdas that capture locals, called in tight loops.

**Check:** the program runs 3000 iterations and prints one checksum per part. Every configuration must print `expected.txt`.

**Files:** `types.mn` interfaces, `funs.mn` function objects, `ops.mn` the list wrapper, the list functions and the strategy records, `main.mn` the three parts and the driver.

**Design notes:**
- Lists cross files as `IIntList`, a wrapper around `ArrayList<Int>`. Passing the `ArrayList<Int>` itself fails a cast whenever an untyped file built it, because it is then an `ArrayList<dyn>`.
- When the generator makes a lambda of a class, it substitutes the constructor arguments into the lambda's body. A nested `new AddN(...)` inside `new Compose(...)` then survives as a reference to the removed class, and would be evaluated on every call. So the arguments are locals.
- Dictionary strategies get 100 calls per record against 1000 for declared fields, so more of their cost is building the record and its dictionary. Each dictionary call also does a lookup and a freeze in C, in every mode: about 5.5 ns per call through an IMT entry, against 2.6 ns for a declared field.
- Generated with `--byfile`, which gives 54 configurations.

**Results** (Debug runtime, pinned to CPUs 0,1, all 54 configurations correct in every mode):
- `run-benchmark.sh lambdas --compare -r 3`: average 0.638 s base, 0.630 s `--trans`, 0.632 s `--trans --sync`.
- Before and after lambda-field IMT entries, `--trans`, median of 3 runs: 12 configurations go from about 1.93 s to 0.61 s (−68%). The other 42 change by less than 1%. Average 0.924 s → 0.631 s.
- The 12 are those with a typed `ops.mn` and typed or untyped interfaces. There `RunOp`'s `op.Apply` and `op.Combine` are interface calls on records whose methods are lambda fields. Before, each such call went through the record call-tag and a C call to the IMT bridge, which installed nothing. That made them about 3× slower than without `--trans` (0.65 s). Now they go through an IMT entry and are about 6% faster than without `--trans`.
- In the other configurations those calls are dynamic calls from untyped code, which never use the IMT.
