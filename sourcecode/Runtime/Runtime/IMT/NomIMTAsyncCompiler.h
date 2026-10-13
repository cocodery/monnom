#pragma once

#include "NomIMTransition.h"
#include "NomInterfaceCallTag.h"
#include "NomRecord.h"

namespace Nom {
namespace Runtime {
namespace IMTUtils {
// Whether new IMT entries are compiled on the background thread. The
// JIT's debug and profiling instrumentation edits modules in the shared
// LLVMCONTEXT, so enabling any of it forces the synchronous path.
bool UseAsyncIMTCompile();

// Builds the IR for a new IMT entry and queues its optimization and code
// generation on the background thread, then returns at once. A later
// InstallFinishedIMTEntries call installs the entry into
// imtArray[imtIndex]. Main thread only.
void CompileIMTEntryFunctionAsync(NomRecord *nomRecord, NomIMTGraph *imtGraph,
                                  int imtIndex,
                                  const NomInterfaceCallTag *callTag,
                                  void *callTagFunAddr, NomIMTNode *imtNode,
                                  void **imtArray);

// Links every entry the background thread has finished into the JIT,
// adds its node to the IMT graph and installs it into its IMT slot.
// Main thread only.
void InstallFinishedIMTEntries();

// Stops the background compiler, if it was started: drops queued jobs
// and waits for the one in flight. Call it before the process exits.
void ShutdownIMTCompiler();

// Whether an entry for this slot is still being compiled.
bool IsIMTEntryPending(const NomIMTGraph *imtGraph, int imtIndex);
} // namespace IMTUtils
} // namespace Runtime
} // namespace Nom
