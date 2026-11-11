# Runtime/IMT — CLAUDE.md

This folder builds Interface Method Tables (IMTs) at runtime for dynamic objects. \
The repo-root `.claude/CLAUDE.md` describes the concept (the call-tag dispatch chain, monotonicity, IMT graphs). \
This file covers how the code here works and what to watch for when changing it.

Everything here is under `Nom::Runtime`. \
The compile helpers are in `Nom::Runtime::IMTUtils`, and the background compiler's interface (`NomIMTAsyncCompiler.h`) is in `Nom::Runtime::NomIMTAsync`.

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

When an interface call reaches a dynamic object whose IMT slot does not cover the call-tag, the call falls through to the record call-tag. JIT'd code in `NomRecordCallTag::createLLVMElement` (`IR/CallTags/NomRecordCallTag.cpp`) then finds the target with `GenerateFindDynamicDispatcherPair`, which freezes a lambda field it finds and aborts if the name is missing or not a lambda. Only then does it call `CPP_NOM_GetIMTTransition`, and afterwards it calls the target itself. The bridge freezes nothing. Lambda-field entries rely on this freeze, because they never reach the start descriptor (see "Who owns what"). The triggering call always goes through dynamic dispatch, because the interface call-tag has already packed its arguments and IMT entries expect them unpacked. The bridge only prepares the IMT for later calls:

1. Links any entries the background compiler has finished into their graphs (`InstallFinishedIMTEntries`), so the lookups below can find them.
2. Returns early if the receiver has no `NomRecord` (a lambda receiver). Every call-tag on a record gets an entry, whether a record method, a declared lambda field or a dictionary lambda field implements it. The record call-tag passes what its dynamic dispatch found (`DispatchTargetValues`: a kind and the target address, the first element of the dispatcher pair), and the compile paths record it with `SetCallTagTarget` (see "Generated IMT entries").
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

All records with the same structure share one `NomRecord`, and so one graph. A record object's IMT belongs to the descriptor it points at. Two objects share a descriptor only while every slot holds the same entry, so an object's transitions are invisible to objects that have not made them.

A record's first objects start on the descriptor in the `NomRecord`'s global. New objects start on `NomRecord::StartDescriptor` instead; the constructor loads it, and uses the global while it is null (`RecordHeader::GenerateConstructorCode`). `MoveToNextDescriptor` sets it to the receiver's descriptor on the record's first move, when no object has moved yet, so it starts as the record's own descriptor. Installing an entry goes through `MoveToNextDescriptor` (`NomVMIMTInterface.cpp`), which does the following:
1. Looks up `(current descriptor, slot, new entry)` with `NomIMTGraph::FindDescriptor`. If it finds a descriptor, it uses that one and copies nothing.
2. Otherwise copies the `RTRecord` descriptor into `malloc` memory, keeping the low 5 bits of the vtable pointer, because bits 3 and 4 say whether the value is invokable and whether it is nominal. It writes the new entry into the copy and records it with `AddDescriptor`.
3. Stores the descriptor's address in the object's header.
4. If the object was on the start descriptor and the new call-tag is a record method (`targetKind`), makes the new descriptor the start descriptor. The start descriptor therefore only ever gains entries.

So a copy is made once per IMT state, not once per object. Descriptors are immutable once published, like hidden classes: a transition changes the object's header pointer and never a slot in a descriptor another object could be on. The dispatcher table pointers in a copy keep pointing into the shared global.

The lookup is keyed on the edge, not on the table's contents, so objects that fill slots in different orders can end up on separate descriptors with equal contents. That is harmless and avoids hashing every slot on each transition. A slot's `NomIMTNode` cannot be the key, because it says nothing about the other slots.

Descriptors live for the whole process, outside the GC. The graph holds them, and it is plain memory the GC does not scan, so a `gcalloc` block could be freed while still in the graph. They hold only pointers to globals, JIT code and the `NomRecord`, so the GC never needs to scan them.

One object compiles a transition. Objects created afterwards start with that entry and never reach the bridge for it. Objects created before it follow it from the graph with one fallback per slot: no compile and no copy. Without the start descriptor every new object took those fallbacks, and suffixtree spent 20% of its run in the bridge.

This relies on entries being valid for every object of the record. A **lambda-field entry is not**, so it never reaches the start descriptor. A new object's field may hold a different lambda, and stays writable until its own first interface call. Only the record call-tag freezes it, and the entry does not freeze.
- **Start descriptor:** `MoveToNextDescriptor` advances `StartDescriptor` only when the call-tag the new entry adds is a record method. Nothing needs marking: the start descriptor only advances from itself, so a descriptor holding a lambda-field entry, and every copy made from it, is never the start descriptor. A slot never loses call-tags, so such copies still hold the lambda-field entry. This relies on `StartDescriptor` never being null once an object has moved; with null, a copy of a lambda descriptor could become the start descriptor.
- **Result:** every object running a lambda branch reached it through the record call-tag for that call-tag, so its field is frozen.
- **Cost:** each new object pays one fallback per lambda slot. It follows the known transition and reuses the descriptor through `FindDescriptor`, so nothing is compiled or copied. The entry also reads the field from the receiver on every call instead of embedding a lambda, so it serves every object of the record.

`experiments/monnom/function/imttransition` checks this.

### Arguments to the bridge

`GetIMTTransition(mod)` declares the bridge with six `i64` parameters and a `void` return. The C++ side receives the first four as `void *` and the last two as `uint64_t`:

| Arg | Built in `NomRecordCallTag.cpp` as | Meaning |
|-----|-------------------------------------|---------|
| `callTagAddr` | invariant load from `((i64*)callTag)[-1]` | the `NomInterfaceCallTag *` object, which sits in the prefix data just before the function |
| `callTagFunAddr` | `ptrtoint callTag` | the call-tag's function address, which the new IMT entry compares the incoming call-tag against |
| `nomRecordAddr` | `RTVTable::GenerateReadNomObjPtr` | the `NomRecord *` that owns the graph |
| `receiver` | `ptrtoint receiver` | the object, whose first word is its vtable pointer; the bridge reads the IMT from it and may replace it |
| `targetKind` | `DispatchTargetValues::kind` from `GenerateFindDynamicDispatcherPair` | what the dispatch found (`DispatchTargetKind`): a method, a declared field or a dictionary field |
| `targetAddr` | `DispatchTargetValues::addr` | the target's address: for a method, its dispatcher `NOMMON_RT_DD_<meth>`, whose `RecordDDPrefix` holds the `NomRecordMethod *` and the method's address; for a lambda field, the invoke of the lambda this receiver holds. An entry is shared by the record's objects, so a lambda-field entry does not use it; `SetCallTagTarget` finds the declared field by name |

If you change the signature, update the C function, the `FunctionType` in `GetIMTTransition()`, and the call site, and keep all three in sync.

## Generated IMT entries

The record's original entry for each slot (`IR/Data/NomRecord.cpp`) becomes the root node of that slot. It has branches only for **record** call-tags, and otherwise must-tail-calls the incoming call-tag.

`BuildIMTEntryModule` builds one entry function named `MONNOM_RT_RECORDIMT_<record>_<slot>_<n>`, where `n` is a global counter. The module has the same name; read it back with `mod->getModuleIdentifier()`. The function has the IMT function type (`GetIMTFunctionType()`) and the `NOMCC` calling convention.

- **Branches:** one compare-and-branch per call-tag, first the new one and then the parent node's. Each compares the incoming call-tag with that tag's function address. What a match calls depends on `GetCallTagTarget`, which only reads the target `SetCallTagTarget` recorded per `(NomRecord, call-tag)` from the record call-tag's dispatch (`RTVTable::GenerateFindDynamicDispatcherPair` with `DispatchTargetValues`). The dispatch follows the record's dispatch list: a record method with the interface method's name, else a declared `REFTYPE` field with that name, else the record's dictionary. For a method, the dispatcher `NOMMON_RT_DD_<meth>` carries a `RecordDDPrefix` (the `NomRecordMethod*` and its address) as prefix data. If that method's arity differs from the interface method's, `SetCallTagTarget` aborts with the error its dispatcher would report on the call that follows, so the output is the same in every mode. Whether a dictionary holds the field is only known per object.
- **On a match, method:** it calls the record method directly. The address comes from the dispatcher's `RecordDDPrefix` and is kept in the recorded target (`CallTagTarget::methodAddr`), so it is neither looked up in the JIT nor cached separately. Before the call it packs primitive arguments and casts arguments to the method's parameter types. Afterwards it casts the result if needed and converts it to the interface method's representation. Call sites do not cast results, so class objects pay nothing and each record call casts once, in the entry or in `NomInterfaceCallTag`.
- **Result casts:** there are three cases, chosen by `ClosedResultCastType`. If the record method declares a subtype of the interface's return type, there is no cast. If the return type is closed, the entry calls one compiled function per return type (`LookupResultCastAddr`). If the return type has type variables, the entry calls the interface call-tag's own cast function, `MONNOM_RT_ICT_CAST_<key>` (`NomInterfaceCallTag::GetResultCastFunction`, address cached by `LookupCallTagResultCastAddr`), because only the receiver's cast data says what they stand for. The interface call-tag calls the same function, so this full cast (`GenerateResultCast`) is compiled once per call-tag rather than once per branch. Calling it out of line from the call-tag costs base mode a little (sort: +0.7% geomean, up to +6.6% in `JJSSSDC`, no change under `--trans`); this was accepted to keep a single copy. Its address is found with a JIT lookup, which works because the bridge only runs after a call has gone through that call-tag, and so through its module. The cast is required for primitive results too: without it a `dyn` method returning a string through `fun Get() : Int` gives a garbage integer. Closed casts are not inlined because each new entry rebuilds the branches of all its call-tags on the main thread; inlining them cost quadT about 11% in compile time. The full cast is much slower than a closed one, so do not use it for closed types: that made quadT 65% slower.
- **On a match, lambda field** (`GenerateLambdaFieldCall`): it does what the record call-tag's dynamic dispatch does for a field, without the bridge call and the name search. Objects of a record can hold different lambdas, so on every call it:
  1. Reads the field: a declared field at its index (`RecordHeader::GenerateReadField`), a dictionary field by name (`InstanceDictionary::GenerateLookup`, after a null check on the dictionary pointer, which `GenerateReadStructDictionary` does not create).
  2. Does not freeze it. Objects reach the entry only through the record call-tag, which froze the field (see "Who owns what").
  3. Checks that the value is a reference with a raw invoke, the same test as `checkLambdaExists` in `RTVTable::GenerateFindDynamicDispatcherPair`. A missing field or a non-lambda value aborts with the same error that dispatch reports. This cannot happen, since the field was checked and frozen before the object reached the entry, so the check is only a safety net.
  4. Packs a copy of the arguments, the same packing the interface call-tag does, and calls the lambda with its lambda call-tag: through its raw invoke with lambda optimization, through `GetFindInvokeDispatcherFunction` without. The lambda body matches on the first try.
  5. Casts the result like a method's. A lambda returns `dyn`, so a closed return type is always cast (`ClosedResultCastType` has no subtype shortcut for lambda targets).

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

**`NomIMTAsync::CompileIMTEntryFunctionAsync`**: on the main thread, under the context lock, it builds the module with `BuildIMTEntryModule`, serializes it to bitcode, and frees it. It then queues the bitcode as a job and marks the slot pending. The worker then:
1. Parses the bitcode into a private `LLVMContext`.
2. Optimizes it with `OptimizeIMTEntryModule` with no debug or profiling instrumentation.
3. Generates an object with its own `TargetMachine`.

**`NomIMTAsync::InstallFinishedIMTEntries`**: for each finished job, it does the following on the main thread:
1. Clears the pending mark.
2. Adds the object with `NomJIT::addObjectFile` and looks up the entry.
3. Creates the child node and links it into the graph.

It writes no slot. Objects reach the entry through the known-transition case, including the one that started the job: `InstallFinishedIMTEntries` runs at the top of the bridge, so that object's next fallback takes the new edge in the same call. A job must not hold an object, because jobs are not GC-scanned and the object could be collected while the job is queued.

It throws if compiling, linking or the lookup failed.

**`NomIMTAsync::UseAsyncIMTCompile`**: false if the runtime was started with `--sync` (`RTConfig_AsyncIMTCompile`). It is also false when instrumentation that `optimizeModule` adds to modules in the shared context is on: verbose output, `-d`/`-d0`/`-d1`, `NomStatsLevel > 1`, or function timing level 3 (`-d2`).

**`NomIMTAsync::ShutdownIMTCompiler`**: drops queued jobs, waits for the job being compiled, and joins the worker. `main` (`Runtime.cpp`) and `NOM_RT_Fail` (`RTOutput.cpp`) call it before exiting. Any new exit path that can run while the worker is alive must call it too. The compiler object is heap-allocated and deliberately never destroyed, because joining the worker from a static destructor can hang: LLVM function-local statics that the worker first touched are destroyed before it.

## Invariants

Keep these true when changing the code:

- **Threads.** JIT'd code, the bridge, the graphs and the nodes run on the main thread only; never give the worker a graph or a node. The worker touches only its job, its private `LLVMContext` and its `TargetMachine`. It must not use `LLVMCONTEXT`, the runtime's type data or anything that allocates from the GC heap, because it is not registered with Boehm. Do not register it: that needs `GC_allow_register_threads`, which locks the allocator for the whole run. `--trans` would then run with a different GC from the base mode, and comparing the two would measure the GC as well as the IMT. `main` starts the parallel markers in every mode with `GC_start_mark_threads`, which does not lock the allocator.
- **No JIT lookups under the context lock.** `BuildIMTEntryModule` gets method addresses from the recorded targets (`SetCallTagTarget`, from the dispatcher's prefix data) and result-cast addresses from their caches. `CompileIMTEntryFunction` and `CompileIMTEntryFunctionAsync` fill the result-cast caches (`CacheCallTagTargetCastFun`) before they take the lock. `GetCallTagTarget` only reads the recorded targets.
- **Slots.** Only `MoveToNextDescriptor` writes IMT slots or `StartDescriptor`, only on the main thread, and only in a new copy before `AddDescriptor` publishes it. A published descriptor never changes, because other objects may be on it. Compiled entries must stay valid for every object holding them. A descriptor with a lambda-field entry must never become `StartDescriptor`: lambda-field entries do not freeze, and rely on the record call-tag having frozen the receiver's field. Each graph slot has at most one async job at a time. A job records the node it extends, so it does not depend on what any object's slot holds.
- **Slot reads.** With `--trans`, call sites read IMT slots with a plain load (`RTVTable::GenerateReadInterfaceMethodTableEntry`); without it the load is invariant. Never make it invariant under `--trans`: LLVM would hoist the read out of loops, so a loop that started before its entry was installed keeps calling the old entry and falls back on every call.
- **Vtable reads.** With `--trans`, `RefValueHeader::GenerateReadVTablePointer` is a plain load; without it the load is invariant. The vtable pointer of a record changes on every transition, so an invariant load under `--trans` would let a loop keep reading the old descriptor and fall back on every call.
- **Start-descriptor reads.** The constructor reads `StartDescriptor` with a plain load, for the same reason as the vtable reads: it changes during the run.
- **Call-tags in slots.** Only interface call-tags reach IMT slots (see "Generated IMT entries").
- **Memory.** Nodes and graphs are allocated with plain `new` (not GC), and descriptors with `malloc`; all of them live for the whole process. Compiled entries embed raw method and call-tag addresses.

## Testing

Build following the root CLAUDE.md, then run the IMT tests. `imttransition` checks that records share an entry:

```bash
source setpath.sh   # from repo root
cd experiments/monnom/function/imttransition
nomc --project IMTTransition --path .
nom --trans --sync -s IMTTransition
```

It should print `10`, `10`, `12`, `12`, `20` and `30`, one per line, and the IMT stats should read:

```
#IMT Entries Compiled: 1
#IMT Entries Installed: 1
#IMT Graph Transitions: 0
#Record IMT Copies: 1
```

`#IMT Graph Transitions` is 0 because records created after the entry start with it, and `#Record IMT Copies` shows that they shared one descriptor. These counts are only exact with `--sync`. In async mode how far the worker gets depends on timing, so check the printed values only. `-s` prints the counters; program output cannot tell a shared entry from a recompile.

`imtshare` checks that dispatch stays correct across many records:

```bash
cd experiments/monnom/function/imtshare
nomc --project IMTShare --path .
nom --trans IMTShare          # async (default)
nom --trans --sync IMTShare   # sync
```

Both should print exactly `68010` and a newline (`680` then `10`; `String.Print` writes no separator), and nothing else. Any LLVM verifier message ("Global is external…", "Alias must point to a definition") means generated IR is invalid. Only the sync path verifies IMT entries, so such a message can make the two modes' output differ.

In this test the program finishes before any background compile does, so in async mode every call takes the fallback path. To see entries being installed, raise `count` in a scratch copy of `Main.mn` to around 200000; the output should then be `2666786667600000` followed by `10`. A longer benchmark such as `experiments/monnom/sort/.BM_IISSSCC` also installs entries; run `nom --trans Sort` from inside that folder.

`imtlambda`'s `TestLambda` covers lambda-field entries, for a declared field and for a dictionary field, each from its own site:
- Two records from the site capture different values. The first compiles the entry; the second follows the transition in the IMT node to the same entry and still calls its own closure.
- A record created after the entry, whose field was reassigned before its first call. It starts without the entry, because lambda-field descriptors never become the start descriptor. Its first call goes through the record call-tag, which freezes the field, and then it follows the transition.

With `--trans --sync -s` the whole test reports `#IMT Entries Compiled: 4` and `#IMT Graph Transitions: 7`. Run with only `TestLambda` in `Main`, it reports 2 compiled, 6 installed, 4 graph transitions and 2 copies. Each late record adds one transition and no copy. The commented-out reassignments must abort with the "invoked through an interface" errors in every mode.

`--imtsize 1` puts every call-tag in one slot, which exercises nodes with several call-tags and the extra edges between them.

Without `--trans` every run takes plain dynamic dispatch, so a test that passes without it says nothing about this folder.

Check that you are running the binary you just built (`sourcecode/Runtime/nom`).
