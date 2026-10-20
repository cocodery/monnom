# MonNom Runtime — CLAUDE.md

## Project Overview

MonNom is a gradually-typed language with **monotonic** semantics. Dynamic objects
may be used where interface types are expected. As the program executes, type
information for dynamic objects becomes increasingly precise.

The runtime is an LLVM 12–based JIT compiler (C++17, Clang 12). \
The primarysource tree lives under `sourcecode/Runtime/`.

## Build

```bash
source setpath.sh # configure environment first and run only once
cd sourcecode/Runtime/build
cmake .. -DCMAKE_C_COMPILER=clang-12 -DCMAKE_CXX_COMPILER=clang++-12
make -j$(nproc)
```

Dependencies: LLVM 12, Intel VTune, Intel TBB, Boehm GC (`libgc-dev`),
pugixml (`libpugixml-dev`).

Build types: `-DCMAKE_BUILD_TYPE=Release|Debug`,
`-DNOM_BUILD_TYPE=Static|Shared` (default Static).

## Code Style

Follow `sourcecode/Runtime/.clang-foramt` (LLVM-derived, 75-column limit,
2-space indent, `Attach` braces, pointer right-aligned). Format with:

```bash
clang-format -style=file -i <file>
```

## Repository Layout (sourcecode/Runtime/)

```
Runtime.cpp              — entry point
Common/                  — shared utilities
FileSystem/              — file I/O helpers
Instructions/            — bytecode instruction implementations
IR/                      — intermediate representation
  CallTags/              — call-tag classes (Interface, Record, Lambda)
  Callables/             — method / callable abstractions
  Data/                  — data-level IR nodes
  Types/                 — type representations
Library/                 — standard library bindings
Parse/                   — bytecode parser
Runtime/                 — core runtime subsystems
  Config/                — runtime configuration flags (RTConfig)
  IMT/                   — Interface Method Table graph & transitions
  Memory/                — Boehm GC allocators
  Statistics/            — cast / timing statistics
  Typing/                — subtyping, casts, disjointness checks
RuntimeData/             — runtime data structures (VTables, headers, types)
```

## Key Concepts

### Call-Tag Dispatch Chain

Each interface method carries a **call-tag** — a signature-keyed descriptor
used for dynamic dispatch.

For **typed objects** the call-tag is known statically and installed in the
object's Interface Method Table (IMT) at class-load time.

For **dynamic objects** (no interface imposed at declaration) the IMT starts
empty. When an interface method is invoked on a dynamic object the dispatch
chain is:

1. Look up the IMT slot for the interface method.
2. The slot is empty → call the **Interface Call-Tag**
   (`NomInterfaceCallTag`).
3. The interface call-tag delegates to the **Record Call-Tag**
   (`NomRecordCallTag`).
4. The record call-tag performs dynamic dispatch
   (`GenerateFindDynamicDispatcherPair`) to locate the concrete method entry.
5. The method executes.

### IMT Graph & Transitions (current optimization work)

Because of monotonicity, once an interface is imposed on an object, the callee
method is valid for that interface signature forever. The runtime exploits this:

- **IMT Graph** (`NomIMTGraph`): one graph per construction-site record
  (`NomRecord`). All objects created from the same site share the graph.
- **IMT Node** (`NomIMTNode`): represents an IMT slot state. Holds an
  `imtEntry` (JIT-compiled function pointer), the set of call-tags it already
  handles, and a `TransitionChain` mapping a call-tag to a successor node.
- **Transition**: when a new call-tag arrives at a node that doesn't cover it,
  the runtime:
  1. JIT-compiles a new IMT entry function via `IMTUtils::CompileIMTEntryFunction`.
  2. Creates a new `NomIMTNode` with the extended call-tag set.
  3. Links the old node to the new node in the transition chain.
  4. Updates the object's IMT pointer so future calls hit the new entry
     directly.

This makes repeated interface calls on the same (or same-site) dynamic objects
progressively cheaper — effectively building an IMT at runtime.

### Call-Tag Hierarchy

| Class                   | Role                                        |
|-------------------------|---------------------------------------------|
| `NomInterfaceCallTag`   | Keyed by method signature; entry point for interface dispatch on dynamic objects |
| `NomRecordCallTag`      | Delegates to dynamic dispatch; invoked by the interface call-tag |
| `NomLambdaCallTag`      | Handles lambda / closure calls              |

All call-tags inherit from `CallTagExternally` (→ `AvailableExternally`), which
manages their LLVM global materialization.

### Key Runtime Entry Points

- `CPP_NOM_GetIMTTransition` (`NomVMIMTInterface.cpp`) — C-callable bridge
  invoked from JIT'd code when an IMT slot needs a transition.
- `CPP_NOM_ReadFunCallTag` / `CPP_NOM_WriteFunCallTag` — read/write the
  function-to-call-tag association map.

## Active Development Focus

The current work centers on `Runtime/IMT/` and `IR/CallTags/`:

- Building out `NomIMTGraph::GenerateTransitionRelation` to maintain transition
  chains between IMT nodes.
- JIT-compiling new IMT entry functions that branch on call-tags
  (`CompileIMTEntryFunction`).
- Ensuring the object's IMT array is updated in-place after a transition so
  subsequent calls skip the fallback path.

## Namespaces

All runtime code lives under `Nom::Runtime`. IMT utilities are in
`Nom::Runtime::IMTUtils`.

## Conventions

- LLVM IR generation uses `NomBuilder` (wrapper around `llvm::IRBuilder`).
- Calling convention: `NOMCC` (typically `llvm::CallingConv::Fast`).
- Function naming: `MONNOM_RT_ICT_<key>` (interface call-tag),
  `MONNOM_RT_RCT_<name>/<typeargs>/<args>` (record call-tag),
  `MONNOM_RT_RECORDIMT_<record>_<slot>_<count>` (IMT entry).
- Memory allocation: Boehm GC (`NomAlloc.h`, `BoehmAllocator.h`).
- `makealloca(Type, n)` allocates a stack array of `n` elements.
- `MakeInt32`, `MakeUInt`, `MakeInt<T>` — convenience LLVM constant builders.
- `EnsurePackedUnpacked` — bitcast between MonNom value representations.
- `MakeInvariantLoad` / `MakeInvariantStore` — loads/stores with invariance
  metadata.

## Git Workflow

- Main branch: `perf`
- Development branch: `dev`
- PRs target `perf`.
