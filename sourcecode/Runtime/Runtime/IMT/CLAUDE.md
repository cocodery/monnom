# Runtime/IMT — CLAUDE.md

This folder builds Interface Method Tables (IMTs) at runtime for dynamic objects. \
The repo-root `.claude/CLAUDE.md` describes the concept (the call-tag dispatch chain, monotonicity, IMT graphs). \
This file covers how the code here works and what to watch for when changing it.

Everything here is under `Nom::Runtime`. \
The compile helpers are in `Nom::Runtime::IMTUtils`.

## Files

| File | Contents |
|------|----------|
| `NomVMIMTInterface.h/.cpp` | `extern "C"` bridge called from JIT'd code, plus `GetIMTTransition`, which declares it in an LLVM module |
| `NomIMTransition.h/.cpp` | `NomIMTNode` and `NomIMTGraph`: the transition data structures |
| `NomIMTUtils.h/.cpp` | `BuildIMTEntryModule`, which builds a new IMT entry's IR, and `CompileIMTEntryFunction`, which builds and JIT-compiles it synchronously |
| `NomIMTAsyncCompiler.h/.cpp` | Background compilation of IMT entries, the default mode |

The build picks up sources with `GLOB_RECURSE`, and `CMakeLists.txt` already lists this folder as an include directory. If you add a file, re-run `cmake` so the glob sees it.

## Runtime flow

IMT transitions are off by default. `nom --trans` turns them on (`RTConfig_EnableTransition`); without it the record call-tag never calls the bridge, and nothing in this folder runs.

When an interface call reaches a dynamic object whose IMT slot does not cover the call-tag, the call falls through to the record call-tag. JIT'd code in `NomRecordCallTag::createLLVMElement` (`IR/CallTags/NomRecordCallTag.cpp`) then calls `CPP_NOM_GetIMTTransition`, and afterwards serves the call itself through `GenerateFindDynamicDispatcherPair`. The triggering call always goes through dynamic dispatch, because the interface call-tag has already packed its arguments and IMT entries expect them unpacked. The bridge only prepares the IMT for later calls:

1. Links any entries the background compiler has finished into their graphs (`InstallFinishedIMTEntries`), so the lookups below can find them.
2. Returns early if the receiver has no `NomRecord`, or if the record has no method for the call-tag (`CheckCallTagMethodExist` is false, e.g. a field holding a lambda).
3. Gets `imtIndex` from `callTag->GetMethod()->GetIMTIndex()` and reads the current entry from the receiver's IMT.
4. Gets the record's graph with `NomIMTGraph::GetIMTGraph(record)`.
5. Gets the node for the current entry with `GetIMTNode(imtIndex, imtEntry)`. The first lookup for a slot creates its root node.
6. Handles the first case below that applies.

| Case | Test | Action |
|------|------|--------|
| Hit | `CheckNodeValidity` | Leaves the slot as it is. |
| Known transition | `CheckTransition` | Moves the object to the descriptor holding the child's entry; compiles nothing. |
| Pending | `IsIMTEntryPending` | Returns; a job for the slot is already running, and this call uses dynamic dispatch. |
| Miss, async (default) | `UseAsyncIMTCompile` | Queues the build with `CompileIMTEntryFunctionAsync` and returns. |
| Miss, sync | otherwise | `CompileIMTEntryFunction`, then `CreateTransitionNode` and `GenerateTransitionRelation`, then moves the object to the descriptor holding the new entry. |

The pending check comes after the known-transition check, so an object can follow a finished edge while another job runs on the same slot.

The node is always found from the slot's **current** contents, not from whichever entry the call passed through. So a miss on an installed entry builds the child under that entry's node.

### Who owns what

All records with the same structure share one `NomRecord`, and so one graph. A record object's IMT belongs to the descriptor it points at. Two objects share a descriptor only while every slot holds the same entry, so an object's transitions are invisible to objects that have not made them. This is what will allow two records from one site to hold different lambdas.

Objects start on the descriptor in the `NomRecord`'s global. Installing an entry goes through `MoveToNextDescriptor` (`NomVMIMTInterface.cpp`), which does the following:
1. Looks up `(current descriptor, slot, new entry)` with `NomIMTGraph::FindDescriptor`. If it finds a descriptor, it uses that one and copies nothing.
2. Otherwise copies the `RTRecord` descriptor into `malloc` memory, keeping the low 5 bits of the vtable pointer, because bits 3 and 4 say whether the value is invokable and whether it is nominal. It writes the new entry into the copy and records it with `AddDescriptor`.
3. Stores the descriptor's address in the object's header.

So a copy is made once per IMT state, not once per object. Descriptors are immutable once published, like hidden classes: a transition changes the object's header pointer and never a slot in a descriptor another object could be on. The dispatcher table pointers in a copy keep pointing into the shared global.

The lookup is keyed on the edge, not on the table's contents, so objects that fill slots in different orders can end up on separate descriptors with equal contents. That is harmless and avoids hashing every slot on each transition. A slot's `NomIMTNode` cannot be the key, because it says nothing about the other slots.

Descriptors live for the whole process, outside the GC. The graph holds them, and it is plain memory the GC does not scan, so a `gcalloc` block could be freed while still in the graph. They hold only pointers to globals, JIT code and the `NomRecord`, so the GC never needs to scan them.

One object compiles a transition, and every other object follows it from the graph with one fallback per slot: no compile and no copy, but each new object starts on the root entries and takes that fallback. So under `--trans` a program that creates many short-lived records pays one bridge call per record per slot.

`experiments/monnom/function/imttransition` checks this.

### Arguments to the bridge

`GetIMTTransition(mod)` declares the bridge with four `i64` parameters and a `void` return. The C++ side receives them as `void *`:

| Arg | Built in `NomRecordCallTag.cpp` as | Meaning |
|-----|-------------------------------------|---------|
| `callTagAddr` | invariant load from `((i64*)callTag)[-1]` | the `NomInterfaceCallTag *` object, which sits in the prefix data just before the function |
| `callTagFunAddr` | `ptrtoint callTag` | the call-tag's function address, which the new IMT entry compares the incoming call-tag against |
| `nomRecordAddr` | `RTVTable::GenerateReadNomObjPtr` | the `NomRecord *` that owns the graph |
| `receiver` | `ptrtoint receiver` | the object, whose first word is its vtable pointer; the bridge reads the IMT from it and may replace it |

If you change the signature, update the C function, the `FunctionType` in `GetIMTTransition()`, and the call site, and keep all three in sync.

## Generated IMT entries

The record's original entry for each slot (`IR/Data/NomRecord.cpp`) becomes the root node of that slot. It has branches only for **record** call-tags, and otherwise must-tail-calls the incoming call-tag.

`BuildIMTEntryModule` builds one entry function named `MONNOM_RT_RECORDIMT_<record>_<slot>_<n>`, where `n` is a global counter. The module has the same name; read it back with `mod->getModuleIdentifier()`. The function has the IMT function type (`GetIMTFunctionType()`) and the `NOMCC` calling convention.

- **Branches:** one compare-and-branch per call-tag, first the new one and then the parent node's. Each compares the incoming call-tag with that tag's function address. Every call-tag must have a record method; `GenerateCallTagBranch` asserts this.
- **On a match:** it calls the record method directly. The address comes from the `LookupRecordMethodAddr` cache. Before the call it packs primitive arguments and casts arguments to the method's parameter types. Afterwards it converts the result to the interface method's representation. It does **not** cast the result to the return type. With `--trans` the call site does that for reference results (`Instructions/CallCheckedInstanceMethod.cpp`), and `NomInterfaceCallTag` skips its own cast for them, so both paths cast exactly once. The cast is required: for a dynamic result it writes the result's cast data, which the call-tag reads on the next interface call that misses the IMT.
- **No match:** it must-tail-calls the incoming call-tag (`argCallTag`), which leads to the record call-tag and the bridge.

Only **interface** call-tags reach IMT slots. The only IMT-slot call site is `Instructions/CallCheckedInstanceMethod.cpp`, which passes an ICT. The ICT calls the record call-tag directly, not through the slot. The commented-out code in `NomInterfaceCallTag.cpp` that dispatched RCTs through the slot is the original design, and it is no longer used. That is why the no-match path can skip the parent chain: it would only repeat branches that already failed, and the root's RCT branches never match. **If RCTs are ever dispatched through IMT slots again, the no-match path must call the parent entry instead**, because the bridge casts `callTagAddr` to `NomInterfaceCallTag *`.

The sync path verifies the function and throws if verification fails; the async path does not verify.

## Methods

**`NomIMTGraph::GetIMTGraph(record)`** (static): returns the record's graph, creating it on first use. The graph is stored in `NomRecord::IMTGraph` and lives for the whole process. Each graph has one node list per IMT slot.

**`NomIMTGraph::GetIMTNode(imtIndex, imtEntry)`**: returns the node in the slot whose `imtEntry` matches. If the slot has no nodes, it creates a root node (no call-tags, no transitions) and returns it. If no node matches, it throws `std::exception`; that can only happen if something outside this folder wrote the slot.

**`NomIMTNode::CheckNodeValidity(callTag)`**: true if `callTag` is in the node's `callTags`. The search is linear and compares pointers.

**`NomIMTNode::CheckTransition(callTag)`**: the target node's entry if the node has an edge for `callTag`, otherwise `nullptr`. Together with the node check, this means an entry is compiled only once per (node, call-tag) pair.

**`NomIMTNode::CreateTransitionNode(parent, callTag, callTagFunAddr, imtEntry)`** (static): returns a new node with the parent's call-tags plus `(callTag, callTagFunAddr)`. It does not add the parent → child edge; `GenerateTransitionRelation` does.

**`NomIMTGraph::GenerateTransitionRelation(imtIndex, node)`**: compares `node` with every node already in the slot:
- If `node` has one fewer call-tag, it adds `node → other`.
- If `node` has one more call-tag, it adds `other → node`. This is how the parent → child edge is created.
- Otherwise it adds nothing.

Each edge is labelled with the tag from `FindDiffCallTag`, and is added only if the source has no edge for that tag yet. It then appends `node` to the slot. The extra edges let objects that reach the same call-tag set in different orders share a node. For example, if `{a}` → `{a, b}` exists and `{b}` is created later, it adds `{b}` → `{a, b}` under `a`.

**`NomIMTGraph::FindDiffCallTag(from, to)`** (private static): the single tag that is in `to` but not in `from`, or `nullptr` if more than one differs (e.g. `{a}` and `{b, c}`). It asserts that `to` has exactly one more tag than `from`. Tags are compared by pointer.

**`IMTUtils::CompileIMTEntryFunction`** (sync): builds the module under the context lock, adds it to `NomJIT`, and returns the entry's address. `NomJIT::optimizeModule` recognises the module by its name (`IsIMTEntryModule`) and runs `OptimizeIMTEntryModule` on it instead of the full pipeline. It throws if adding or looking up fails.

**`IMTUtils::CompileIMTEntryFunctionAsync`**: on the main thread it copies the record, slot, call-tag and the node's call-tags into a job, queues it, and marks the slot pending. The worker then:
1. Under the context lock, builds the module with `BuildIMTEntryModule`, serializes it to bitcode, and frees it.
2. Without the lock, parses the bitcode into a private `LLVMContext`.
3. Optimizes it with `OptimizeIMTEntryModule` with no debug or profiling instrumentation.
4. Generates an object with its own `TargetMachine`.

**`IMTUtils::InstallFinishedIMTEntries`**: for each finished job, it does the following on the main thread:
1. Clears the pending mark.
2. Adds the object with `NomJIT::addObjectFile` and looks up the entry.
3. Creates the child node and links it into the graph.

It writes no slot. Objects reach the entry through the known-transition case, including the one that started the job: `InstallFinishedIMTEntries` runs at the top of the bridge, so that object's next fallback takes the new edge in the same call. A job must not hold an object, because jobs are not GC-scanned and the object could be collected while the job is queued.

It throws if compiling, linking or the lookup failed.

**`IMTUtils::UseAsyncIMTCompile`**: false if the runtime was started with `--sync` (`RTConfig_AsyncIMTCompile`). It is also false when instrumentation that `optimizeModule` adds to modules in the shared context is on: verbose output, `-d`/`-d0`/`-d1`, `NomStatsLevel > 1`, or function timing level 3 (`-d2`).

**`IMTUtils::ShutdownIMTCompiler`**: drops queued jobs, waits for the job being compiled, and joins the worker. `main` (`Runtime.cpp`) and `NOM_RT_Fail` (`RTOutput.cpp`) call it before exiting. Any new exit path that can run while the worker is alive must call it too. The compiler object is heap-allocated and deliberately never destroyed, because joining the worker from a static destructor can hang: LLVM function-local statics that the worker first touched are destroyed before it.

## Invariants

Keep these true when changing the code:

- **Threads.** JIT'd code, the bridge, the graphs and the nodes run on the main thread only; never give the worker a graph or a node. The worker builds IR in `LLVMCONTEXT` and reads and extends the runtime's type data, always under `NomJIT::getContextLock()`. Anything else that does either while the worker may run must hold that lock too. ORC takes it when compiling IR modules, and so do `NOM_RTInstantiateClass` and `NOM_RTInstantiateMaybe`. A new runtime entry point that instantiates types or builds IR must take it as well. The worker registers itself with the Boehm GC, because building IR can allocate from the GC heap.
- **No JIT calls under the context lock.** `BuildIMTEntryModule` gets method addresses only from the `LookupRecordMethodAddr` cache. The main thread fills that cache (`CacheRecordMethodAddr`, called by `CompileIMTEntryFunction` and `CompileIMTEntryFunctionAsync` before the lock) before any entry covering that call-tag is built. `CheckCallTagMethodExist` only checks that the record has the method.
- **Slots.** Only `MoveToNextDescriptor` writes IMT slots, only on the main thread, and only in a new copy before `AddDescriptor` publishes it. A published descriptor never changes, because other objects may be on it. Compiled entries must stay valid for every object holding them; an entry that depends on one object's data, such as a specific lambda, needs its own entry address, which then gets its own descriptor. Each graph slot has at most one async job at a time. A job records the node it extends, so it does not depend on what any object's slot holds.
- **Slot reads.** With `--trans`, call sites read IMT slots with a plain load (`RTVTable::GenerateReadInterfaceMethodTableEntry`); without it the load is invariant. Never make it invariant under `--trans`: LLVM would hoist the read out of loops, so a loop that started before its entry was installed keeps calling the old entry and falls back on every call.
- **Vtable reads.** With `--trans`, `RefValueHeader::GenerateReadVTablePointer` is a plain load; without it the load is invariant. The vtable pointer of a record changes on every transition, so an invariant load under `--trans` would let a loop keep reading the old descriptor and fall back on every call.
- **Descriptor layout.** `GetRecordDescriptorLayout` must run before any job is queued, which is why the bridge calls it first. `DataLayout` caches struct layouts without a lock, so a later first lookup could race the worker.
- **Call-tags in slots.** Only interface call-tags reach IMT slots (see "Generated IMT entries").
- **Memory.** Nodes and graphs are allocated with plain `new` (not GC), and descriptors with `malloc`; all of them live for the whole process. Compiled entries embed raw method and call-tag addresses.

## Testing

Build following the root CLAUDE.md, then run the IMT tests. `imttransition` checks that records share transitions through the graph:

```bash
source setpath.sh   # from repo root
cd experiments/monnom/function/imttransition
nomc --project IMTTransition --path .
nom --trans --sync -s IMTTransition
```

It should print `10`, `10`, `12`, `12`, `20` and `30`, one per line, and the IMT stats should read:

```
#IMT Entries Compiled: 1
#IMT Entries Installed: 3
#IMT Graph Transitions: 2
#Record IMT Copies: 1
```

`#IMT Graph Transitions` shows that records found the entry in the graph, and `#Record IMT Copies` shows that they shared one descriptor. These counts are only exact with `--sync`. In async mode how far the worker gets depends on timing, so check the printed values only. `-s` prints the counters; program output cannot tell a graph hit from a recompile.

`imtshare` checks that dispatch stays correct across many records:

```bash
cd experiments/monnom/function/imtshare
nomc --project IMTShare --path .
nom --trans IMTShare          # async (default)
nom --trans --sync IMTShare   # sync
```

Both should print exactly `68010` and a newline (`680` then `10`; `String.Print` writes no separator), and nothing else. Any LLVM verifier message ("Global is external…", "Alias must point to a definition") means generated IR is invalid. Only the sync path verifies IMT entries, so such a message can make the two modes' output differ.

In this test the program finishes before any background compile does, so in async mode every call takes the fallback path. To see entries being installed, raise `count` in a scratch copy of `Main.mn` to around 200000; the output should then be `2666786667600000` followed by `10`. A longer benchmark such as `experiments/monnom/sort/.BM_IISSSCC` also installs entries; run `nom --trans Sort` from inside that folder.

`--imtsize 1` puts every call-tag in one slot, which exercises nodes with several call-tags and the extra edges between them.

Without `--trans` every run takes plain dynamic dispatch, so a test that passes without it says nothing about this folder.

Check that you are running the binary you just built (`sourcecode/Runtime/nom`).
