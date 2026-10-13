# Runtime/IMT — CLAUDE.md

This folder builds Interface Method Tables (IMTs) at runtime for dynamic objects. \
The repo-root `.claude/CLAUDE.md` describes the concept (the call-tag dispatch chain, monotonicity, IMT graphs). \
This file covers how the code here works and what to watch for when changing it.

Everything here is under `Nom::Runtime`. \
The compile helpers are in `Nom::Runtime::IMTUtils`.

## Files

| File | Contents |
|------|----------|
| `NomVMIMTInterface.h/.cpp` | `extern "C"` bridges called from JIT'd code, plus the `Get*` helpers that declare them in an LLVM module |
| `NomIMTransition.h/.cpp` | `NomIMTNode` and `NomIMTGraph`: the transition data structures |
| `NomIMTUtils.h/.cpp` | `BuildIMTEntryModule`, which builds a new IMT entry's IR, and `CompileIMTEntryFunction`, which builds and JIT-compiles it synchronously |
| `NomIMTAsyncCompiler.h/.cpp` | Background compilation of IMT entries, the default mode |

The build picks up sources with `GLOB_RECURSE`, and `CMakeLists.txt` already lists this folder as an include directory. If you add a file, re-run `cmake` so the glob sees it.

## Runtime flow

When an interface call reaches a dynamic object whose IMT slot does not cover the call-tag, the call falls through to the record call-tag. JIT'd code in `NomRecordCallTag::createLLVMElement` (`IR/CallTags/NomRecordCallTag.cpp`) then calls `CPP_NOM_GetIMTTransition`, and afterwards serves the call itself through `GenerateFindDynamicDispatcherPair`. The triggering call always goes through dynamic dispatch, because the interface call-tag has already packed its arguments and IMT entries expect them unpacked. The bridge only prepares the IMT for later calls:

1. Installs any entries the background compiler has finished (`InstallFinishedIMTEntries`), so the slot it reads next is current.
2. Gets `imtIndex` from `callTag->GetMethod()->GetIMTIndex()` and reads the current entry from `imtArray[imtIndex]`.
3. Returns early if the record has no method for the call-tag (`LookupCallTagMethodAddr` is null, e.g. a field holding a lambda).
4. Gets the record's graph with `NomIMTGraph::GetIMTGraph(record)` and the node for the current entry with `GetIMTNode(imtIndex, imtEntry)`. The first lookup for a slot creates its root node.
5. Picks one of the cases listed under `CPP_NOM_GetIMTTransition` below: hit, known transition, pending, or miss.

All objects created at one construction site share one `NomRecord`, and so one vtable, one `imtArray` and one graph. Installing an entry therefore speeds up every object from that site at once. `experiments/monnom/imtshare` checks this.

### Arguments to the bridge

The JIT'd caller passes four `i64` values, and the C++ side receives them
as `void *`:

| Arg | Built in `NomRecordCallTag.cpp` as | Meaning |
|-----|-------------------------------------|---------|
| `callTagAddr` | invariant load from `((i64*)callTag)[-1]` | the `NomInterfaceCallTag *` object, which sits in the prefix data just before the function |
| `callTagFunAddr` | `ptrtoint callTag` | the call-tag's function address, which the new IMT entry compares the incoming call-tag against |
| `nomRecordAddr` | `RTVTable::GenerateReadNomObjPtr` | the `NomRecord *` that owns the graph |
| `imtArray` | `RTVTable::GenerateReadInterfaceMethodTable` | the record's IMT, an array of `IMTsize` entries |

If you change the signature, update the C function, the `FunctionType` in
`GetIMTTransition()`, and the call site, and keep all three in sync.

## Method reference

This section lists `CPP_NOM_GetIMTTransition` and the methods it calls, in call order.

### `CPP_NOM_GetIMTTransition` (`NomVMIMTInterface.cpp`)

```cpp
extern "C" void *CPP_NOM_GetIMTTransition(void *callTagAddr,
                                          void *callTagFunAddr,
                                          void *nomRecordAddr,
                                          void **imtArray);
```

This is the bridge that JIT'd code calls on the fallback path. After the steps in "Runtime flow", it handles one of four cases:

- **Hit:** the node already covers the call-tag (`CheckNodeValidity`). The slot is left as it is.
- **Known transition:** the node already has a transition for the call-tag (`CheckTransition`). It writes the child's entry into `imtArray[imtIndex]` and compiles nothing.
- **Pending:** an entry for this slot is still compiling in the background (`IsIMTEntryPending`). It returns `nullptr` and leaves the slot alone. A fallback after that compile finishes installs the entry, and a later fallback extends it with this call-tag if it still isn't covered.
- **Miss, async (default):** it hands the work to `CompileIMTEntryFunctionAsync` and returns `nullptr` right away.
- **Miss, sync:** it compiles with `CompileIMTEntryFunction`, creates the child with `CreateTransitionNode`, adds it to the graph with `GenerateTransitionRelation`, and writes the new entry into `imtArray[imtIndex]`.

The caller ignores the return value. LLVM declares the function through `GetIMTTransition(mod)`, with four `i64` parameters and a pointer return value.

### `NomIMTGraph::GetIMTGraph` (static)

```cpp
static NomIMTGraph *GetIMTGraph(NomRecord *record);
```

This returns the graph for `record`, creating one on first use. Graphs are kept in a function-static `IMTGraphs` map (`NomRecord * → NomIMTGraph *`) and live for the whole process. The constructor gives the graph one empty node list for each of the `IMTsize` slots. Since each construction site has its own `NomRecord`, keying by record gives one graph per construction site.

### `NomIMTGraph::GetIMTNode`

```cpp
NomIMTNode *GetIMTNode(int imtIndex, void *imtEntry);
```

This returns the node in slot `imtIndex` whose `imtEntry` equals the given entry.

- If the slot has no nodes yet, it creates a root node for `imtEntry` with no call-tags and no transitions. It then adds that node to the slot and returns it.
- If the slot has nodes but none matches, it throws `std::exception`. That can only happen if something other than this folder wrote the slot.

### `NomIMTNode::CheckNodeValidity`

```cpp
bool CheckNodeValidity(const NomInterfaceCallTag *callTag);
```

This returns `true` if `callTag` is in the node's `callTags` list, meaning this node's entry already handles that call-tag. The check is a linear search that compares pointers.

### `NomIMTNode::CheckTransition`

```cpp
void *CheckTransition(const NomInterfaceCallTag *callTag);
```

This looks up `callTag` in the node's `transitions` map. It returns the target node's `imtEntry` if there is an edge for that call-tag, and `nullptr` otherwise. The bridge calls it when `CheckNodeValidity` fails, so an entry is compiled only once per (node, call-tag) pair.

### `IMTUtils::BuildIMTEntryModule` (`NomIMTUtils.cpp`)

```cpp
std::unique_ptr<llvm::Module> BuildIMTEntryModule(
    NomRecord *nomRecord, int imtIndex, const NomInterfaceCallTag *callTag,
    void *callTagFunAddr, NomIMTNode *imtNode);
```

This builds a module holding one IMT entry function that covers `imtNode`'s call-tags plus `callTag`. The module and the function share the name `MONNOM_RT_RECORDIMT_<record>_<slot>_<n>`, where `n` is a global counter. Read it back with `mod->getModuleIdentifier()`. The function has the IMT function type (`GetIMTFunctionType()`) and the `NOMCC` calling convention.

The generated function works like this:

- **Branches:** one compare-and-branch for each call-tag, first the parent node's tags and then the new one. Each compares the incoming call-tag with that tag's function address.
- **On a match:** it calls the record method directly. The address comes from `LookupRecordMethodAddr`, which is cached. Before the call it packs primitive arguments, casts arguments to the method's parameter types, and converts the result back to the interface method's representation. A call-tag the record has no method for gets no branch.
- **No match:** it must-tail-calls the parent node's entry, which ends at the record's original entry and from there at the call-tag itself.

It verifies the function and throws if verification fails. It uses the shared `LLVMCONTEXT` and calls `NomJIT::lookup`, so it must run on the main thread.

### `IMTUtils::CompileIMTEntryFunction` (`NomIMTUtils.cpp`)

```cpp
void *CompileIMTEntryFunction(NomRecord *nomRecord, int imtIndex,
                              const NomInterfaceCallTag *callTag,
                              void *callTagFunAddr, NomIMTNode *imtNode);
```

This is the synchronous path. It calls `BuildIMTEntryModule`, adds the module to `NomJIT` (through the JIT's normal optimization layer), looks up the symbol, and returns the entry's native address. It throws if adding or looking up fails.

### `IMTUtils::CompileIMTEntryFunctionAsync` (`NomIMTAsyncCompiler.cpp`)

```cpp
void CompileIMTEntryFunctionAsync(NomRecord *nomRecord,
                                  NomIMTGraph *imtGraph, int imtIndex,
                                  const NomInterfaceCallTag *callTag,
                                  void *callTagFunAddr,
                                  NomIMTNode *imtNode, void **imtArray);
```

This is the asynchronous path. It calls `BuildIMTEntryModule` on the main thread, serializes the module to bitcode, queues the job, and marks the slot as pending. The worker thread then works on the job:

- It parses the bitcode into its own private `LLVMContext`.
- It optimizes the module with a trimmed copy of `NomJIT::optimizeModule`: the `PassManagerBuilder` passes at `NomOptLevel`, tail-call elimination, and `RTConfig_AdditionalOptPasses`, but none of the debug or profiling instrumentation.
- It generates an object with its own `TargetMachine`.

The main thread and the worker share no LLVM objects.

### `IMTUtils::InstallFinishedIMTEntries` (`NomIMTAsyncCompiler.cpp`)

The bridge calls this first thing. For each finished job it does the following:

1. Clears the slot's pending mark.
2. Adds the object to the JIT with `NomJIT::addObjectFile` and looks up the entry.
3. Creates the child with `CreateTransitionNode`, adds it with `GenerateTransitionRelation`, and writes the entry into `imtArray[imtIndex]`.

It throws if compiling, linking or the lookup failed.

### `IMTUtils::UseAsyncIMTCompile` and `ShutdownIMTCompiler`

`UseAsyncIMTCompile()` returns `false` in two cases, and the bridge then uses the sync path:

- The runtime was started with `--syncimtcompile`.
- Instrumentation is enabled that `optimizeModule` adds to modules in the shared context: verbose output, `-d`/`-d0`/`-d1`, `NomStatsLevel > 1`, or function timing level 3 (`-d2`).

`ShutdownIMTCompiler()` drops queued jobs, waits for the job being compiled, and joins the worker. `main` (`Runtime.cpp`) and `NOM_RT_Fail` (`RTOutput.cpp`) call it before exiting. Any new exit path that can run while the worker is alive must call it too. The compiler object is heap-allocated and never destroyed on purpose. Joining the worker from a static destructor can hang, because LLVM function-local statics that the worker first touched are destroyed before it.

### `NomIMTNode::CreateTransitionNode` (static)

```cpp
static NomIMTNode *CreateTransitionNode(NomIMTNode *parent,
                                        const NomInterfaceCallTag *callTag,
                                        void *callTagFunAddr,
                                        void *imtEntry);
```

This creates the node that a newly compiled entry represents. It copies the parent's `callTags`, appends `(callTag, callTagFunAddr)`, and returns a new node with `imtEntry` and the extended list. It does not link the parent to the child; `GenerateTransitionRelation` adds that edge.

### `NomIMTGraph::GenerateTransitionRelation`

```cpp
void GenerateTransitionRelation(int imtIndex, NomIMTNode *node);
```

This builds transition relations between `node` and the existing nodes in slot `imtIndex`. It checks every node already in the slot and compares the sizes of the two call-tag sets:

- **`node` has one fewer call-tag:** `node` can transition to the other node. It finds the call-tag that differs with `FindDiffCallTag(node, other)` and adds `node → other` under that tag with `AddTransitionNode`.
- **`node` has one more call-tag:** the other node can transition to `node`. It finds the differing tag with `FindDiffCallTag(other, node)` and adds `other → node`. The parent → child edge for a new node is created here.
- **Any other size difference:** no relation is added.

An edge is added only when the source node has no transition for that call-tag yet, so existing edges are never overwritten. Finally it appends `node` to the slot's node list, so `GetIMTNode` can find it by its entry later.

These extra edges let objects that reach the same call-tag set in different orders share a node. For example, if `{a}` → `{a, b}` already exists and `{b}` is created later, this adds `{b}` → `{a, b}` under `a`. A later `a` call on a `{b}` object then reuses the `{a, b}` entry instead of compiling a duplicate.

### `NomIMTGraph::FindDiffCallTag` (private static)

```cpp
static const NomInterfaceCallTag *FindDiffCallTag(const NomIMTNode *from,
                                                  const NomIMTNode *to);
```

This returns the call-tag that `from` would take to transition to `to`: the single tag that is in `to->callTags` but not in `from->callTags`. It returns `nullptr` when `to`'s set is not exactly `from`'s set plus one tag. That covers both a size difference other than one and sets of the right sizes that diverge (for example `{a}` and `{b, c}`). Call-tags are compared by pointer.

## Invariants

Keep these true when changing the code:

- **Threads.** JIT'd code, the bridge, the graphs and the nodes all run on the main thread only. The static maps are not locked. The worker touches nothing but its own job, its private `LLVMContext` and its `TargetMachine`. Never give it anything from `LLVMCONTEXT`, a graph or a node. It must not allocate from the Boehm GC heap either, because the GC does not know about it.
- **Slots.** Only this folder writes `imtArray` slots, and only on the main thread. Each slot has at most one async job at a time, and the bridge leaves a pending slot alone. So when a job is installed, the slot still holds the entry of the node the job extends.
- **Memory.** Nodes and graphs are allocated with plain `new` (not GC) and live for the whole process. Compiled entries embed raw node entry and method addresses.

## Testing

Build following the root CLAUDE.md, then run the IMT test case:

```bash
source setpath.sh   # from repo root
cd experiments/monnom/imtshare
nomc --project IMTShare --path .
nom IMTShare                    # async (default)
nom --syncimtcompile IMTShare   # sync
```

Both should print `680`, `440` and `10`. `String.Print` writes no newline, so the numbers may run into LLVM's "Global is external…" warnings. Don't filter the output with `grep -v` on those lines, or the numbers disappear with them.

In this test the program finishes before any background compile does, so in async mode every call takes the fallback path. That is expected. To see entries being installed, raise `count` in a scratch copy of `Main.mn` to around 200000, or run a longer benchmark such as `experiments/monnom/sort/.BM_IISSSCC` (`nom Sort`). Its timed section is about 2 ms async and about 30 ms with `--syncimtcompile`.

Check that you are running the binary you just built (`sourcecode/Runtime/nom`).
