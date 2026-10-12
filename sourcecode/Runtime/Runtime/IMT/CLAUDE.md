# Runtime/IMT — CLAUDE.md

This folder builds Interface Method Tables (IMTs) at runtime for dynamic
objects. The repo-root `.claude/CLAUDE.md` describes the concept (the
call-tag dispatch chain, monotonicity, IMT graphs). This file covers how
the code here works and what to watch for when changing it.

Everything here is under `Nom::Runtime`. \
The compile helper is in `Nom::Runtime::IMTUtils`.

## Files

| File | Contents |
|------|----------|
| `NomVMIMTInterface.h/.cpp` | `extern "C"` bridges called from JIT'd code, plus the `Get*` helpers that declare them in an LLVM module |
| `NomIMTransition.h/.cpp` | `NomIMTNode` and `NomIMTGraph`: the transition data structures |
| `NomIMTUtils.h/.cpp` | `IMTUtils::CompileIMTEntryFunction`, which JIT-compiles a new IMT entry |

The build picks up sources with `GLOB_RECURSE`, and `CMakeLists.txt` already lists this folder as an include directory. If you add a file, re-run `cmake` so the glob sees it.

## Runtime flow

JIT'd code in `NomRecordCallTag::createLLVMElement` (`IR/CallTags/NomRecordCallTag.cpp`) calls `CPP_NOM_GetIMTTransition`, which then does the following:

1. Gets `imtIndex` from `callTag->GetMethod()->GetIMTIndex()` and reads the current entry from `imtArray[imtIndex]`.
2. Gets the record's graph with `NomIMTGraph::GetIMTGraph(record)`. All graphs live in a function-static map keyed by `NomRecord *`.
3. Finds the node for that entry with `GetIMTNode(imtIndex, imtEntry)`. The first lookup for a slot creates the root node.
4. If the node already covers the call-tag (`CheckNodeValidity`), it returns the same entry.
5. Otherwise it compiles a new entry, creates a child node with `CreateTransitionNode`, which copies the parent's call-tag list, adds the new tag, and links parent → child, and then calls `GenerateTransitionRelation`, which links the child to the other nodes in the slot and adds it to the slot.
6. It returns the new entry pointer. Writing that pointer back into the object's IMT is the caller's job, and nothing does it yet.

### Arguments to the bridge

The JIT'd caller passes four `i64` values, and the C++ side receives them
as `void *`:

| Arg | Built in `NomRecordCallTag.cpp` as | Meaning |
|-----|-------------------------------------|---------|
| `callTagAddr` | invariant load from `((i64*)callTag)[-1]` | the `NomInterfaceCallTag *` object, which sits in the prefix data just before the function |
| `callTagFunAddr` | `ptrtoint callTag` | the call-tag's function address, meant to be the branch target in the new IMT entry |
| `nomRecordAddr` | `RTVTable::GenerateReadNomObjPtr` | the `NomRecord *` that owns the graph |
| `imtArray` | `RTVTable::GenerateReadInterfaceMethodTable` | the object's IMT, an array of `IMTsize` entries |

If you change the signature, update the C function, the `FunctionType` in
`GetIMTTransition()`, and the call site, and keep all three in sync.

## Method reference

This section lists `CPP_NOM_GetIMTTransition` and the methods it calls, in call order. The descriptions say what the code does now. Where that differs from what it is meant to do, see "Current state and known issues" below.

### `CPP_NOM_GetIMTTransition` (`NomVMIMTInterface.cpp`)

```cpp
extern "C" void *CPP_NOM_GetIMTTransition(void *callTagAddr,
                                          void *callTagFunAddr,
                                          void *nomRecordAddr,
                                          void **imtArray);
```

This is the bridge that JIT'd code calls when an interface call-tag reaches a dynamic object. It casts `callTagAddr` to `NomInterfaceCallTag *` and `nomRecordAddr` to `NomRecord *`. It then finds the slot index, reads the current entry `imtArray[imtIndex]`, and finds or creates that entry's node in the record's graph.

- **Hit:** if the node already covers the call-tag (`CheckNodeValidity`), it returns the current entry unchanged.
- **Known transition:** if the node already has a transition for the call-tag (`CheckTransition`), it returns the child node's entry and compiles nothing.
- **Miss:** it compiles a new entry, creates a child node for it, records the node in the graph, and returns the new entry.

It does not write the returned entry back into `imtArray`. LLVM declares it through `GetIMTTransition(mod)`, with four `i64` parameters and a pointer return value.

### `NomIMTGraph::GetIMTGraph` (static)

```cpp
static NomIMTGraph *GetIMTGraph(NomRecord *record);
```

This returns the graph for `record`, creating one on first use. Graphs are kept in a function-static `IMTGraphs` map (`NomRecord * → NomIMTGraph *`) and live for the whole process. The constructor gives the graph one empty node list for each of the `IMTsize` slots.

Graphs are currently keyed by record, so objects from different construction sites of the same record share one graph. The TODO in the code is to key graphs by construction site instead.

### `NomIMTGraph::GetIMTNode`

```cpp
NomIMTNode *GetIMTNode(int imtIndex, void *imtEntry);
```

This returns the node in slot `imtIndex` whose `imtEntry` equals the given entry.

- If the slot has no nodes yet, it creates a root node for `imtEntry` with no call-tags and no transitions. It then adds that node to the slot and returns it.
- If the slot has nodes but none matches, it throws `std::exception`.

### `NomIMTNode::CheckNodeValidity`

```cpp
bool CheckNodeValidity(const NomInterfaceCallTag *callTag);
```

This returns `true` if `callTag` is in the node's `callTags` list, meaning this node's entry already handles that call-tag. The check is a linear search that compares pointers.

### `NomIMTNode::CheckTransition`

```cpp
void *CheckTransition(const NomInterfaceCallTag *callTag);
```

This looks up `callTag` in the node's `transitions` map. If an earlier miss on this node already created a child for that call-tag, it returns the child's `imtEntry`. Otherwise it returns `nullptr`. The bridge calls it when `CheckNodeValidity` fails, so that a transition is compiled only once per (node, call-tag) pair.

### `IMTUtils::CompileIMTEntryFunction` (`NomIMTUtils.cpp`)

```cpp
void *CompileIMTEntryFunction(NomRecord *nomRecord, int imtIndex,
                              void *callTagFunAddr, NomIMTNode *imtNode);
```

This JIT-compiles a new IMT entry function for slot `imtIndex` and returns its native address. The function is named `MONNOM_RT_RECORDIMT_<record>_<slot>_<n>`, where `n` is a global counter. It is meant to branch on the call-tag argument: to `callTagFunAddr` for the new tag, and to the functions already recorded in `imtNode->callTags` for the others. The function has the IMT function type (`GetIMTFunctionType()`) and the `NOMCC` calling convention.

The current version emits only the argument setup and then adds the module to `NomJIT`. It throws if adding the module or looking up the symbol fails.

### `NomIMTNode::CreateTransitionNode` (static)

```cpp
static NomIMTNode *CreateTransitionNode(NomIMTNode *parent,
                                        const NomInterfaceCallTag *callTag,
                                        void *callTagFunAddr,
                                        void *imtEntry);
```

This creates the child node that the newly compiled entry represents. It copies the parent's `callTags`, appends `(callTag, callTagFunAddr)`, and creates a node with `imtEntry` and the extended list. It then calls `parent->AddTransitionNode(callTag, child)`, which records `transitions[callTag] = child` and so links parent to child under that call-tag. It returns the child.

### `NomIMTGraph::GenerateTransitionRelation`

```cpp
void GenerateTransitionRelation(int imtIndex, NomIMTNode *node);
```

This builds transition relations between `node` and the existing nodes in slot `imtIndex`. It checks every node already in the slot and compares the sizes of the two call-tag sets:

- **`node` has one fewer call-tag:** `node` can transition to the other node. It finds the call-tag that differs with `FindDiffCallTag(node, other)` and adds `node → other` under that tag with `AddTransitionNode`.
- **`node` has one more call-tag:** the other node can transition to `node`. It finds the differing tag with `FindDiffCallTag(other, node)` and adds `other → node`.
- **Any other size difference:** no relation is added.

Finally it appends `node` to the slot's node list, so `GetIMTNode` can find it by its entry later.

An edge is added only when the source node has no transition for that call-tag yet. Existing edges, such as the parent → child edge from `CreateTransitionNode`, are never overwritten.

These extra edges let objects that reach the same call-tag set in different orders share a node. For example, if `{a}` → `{a, b}` already exists and `{b}` is created later, this adds `{b}` → `{a, b}` under `a`. A later `a` call on a `{b}` object then reuses the `{a, b}` entry instead of compiling a duplicate.

### `NomIMTGraph::FindDiffCallTag` (private static)

```cpp
static const NomInterfaceCallTag *FindDiffCallTag(const NomIMTNode *from,
                                                  const NomIMTNode *to);
```

This returns the call-tag that `from` would take to transition to `to`: the single tag that is in `to->callTags` but not in `from->callTags`. It returns `nullptr` when `to`'s set is not exactly `from`'s set plus one tag. That covers both a size difference other than one and sets of the right sizes that diverge (for example `{a}` and `{b, c}`). Call-tags are compared by pointer.

## Current state and known issues (as of 2026-10-12)

The code is a work in progress. Check these points before assuming a
code path works:

- **Not connected yet.** The `CreateCall(GetIMTTransition(&mod), ...)` in `NomRecordCallTag.cpp` is commented out (an uncommitted change on `dev`), so this code doesn't run right now. Dispatch still goes through `GenerateFindDynamicDispatcherPair`.
- **`CompileIMTEntryFunction` emits an incomplete function.** It sets up arguments, but the entry block has no terminator and nothing branches on `callTagFunAddr`. `imtNode`, `argCallTag` and `varargs` are unused. Adding that module to the JIT will fail verification. Its generated name also contains a stray space (`"MONNOM_RT_RECORDIMT_ "`), so it doesn't follow the root naming convention.
- **`GetIMTNode` copies the node list.** The loop uses `auto nodes = nodesPerEntry[imtIndex];`, which copies the list on every lookup. The result is correct but wasteful; `auto &nodes` would avoid the copy.
- **Objects and errors.** Nodes and graphs are allocated with plain `new` (not GC) and are never freed. The static maps aren't thread-safe. Errors are raised inconsistently, with both `throw std::exception()` and `throw new std::exception()`.

## Testing

Build following the root CLAUDE.md, then run a benchmark that uses interface calls on dynamic objects, for example:

```bash
source setpath.sh   # from repo root
cd experiments/monnom/sort/.BM_IISSSCC && nom Sort
```

Check that you are running the binary you just built (`sourcecode/Runtime/nom`). As long as the bridge call is commented out, this run doesn't exercise any code in this folder.
