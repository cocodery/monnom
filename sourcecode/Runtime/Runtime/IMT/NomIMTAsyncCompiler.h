#pragma once

#include "NomIMTransition.h"
#include "NomInterfaceCallTag.h"
#include "NomRecord.h"

namespace Nom {
namespace Runtime {
namespace IMTUtils {
// Whether new IMT entries are compiled on the background thread
// JIT's debug profiling instrumentation edits modules in the shared LLVMCONTEXT
// so enabling any of it forces the synchronous path.
bool UseAsyncIMTCompile();

// Queues a new IMT entry on the background thread
// A later InstallFinishedIMTEntries call installs the entry into imtArray
void CompileIMTEntryFunctionAsync(NomRecord *nomRecord, NomIMTGraph *imtGraph,
                                  int imtIndex, void *imtEntry,
                                  const NomInterfaceCallTag *callTag,
                                  void *callTagFunAddr, NomIMTNode *imtNode,
                                  void **imtArray);

// Links every entry the background thread has finished into the JIT
// adds its node to the IMT graph and installs it into its IMT slot
void InstallFinishedIMTEntries();

// Stops the background compiler, if it was started
void ShutdownIMTCompiler();

// Whether an entry for this slot is still being compiled.
bool IsIMTEntryPending(const NomIMTGraph *imtGraph, int imtIndex);
} // namespace IMTUtils
} // namespace Runtime
} // namespace Nom
