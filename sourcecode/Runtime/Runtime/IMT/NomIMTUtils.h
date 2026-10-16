#pragma once

#include "NomIMTransition.h"
#include "NomInterfaceCallTag.h"
#include "NomRecord.h"
#include <memory>

namespace llvm {
class Module;
} // namespace llvm

namespace Nom {
namespace Runtime {
namespace IMTUtils {
bool CheckCallTagMethodExist(const NomRecord *nomRecord,
                             const NomInterfaceCallTag *callTag);
// Builds the IR for an IMT entry covering the parent node's call-tags plus
// callTag; calls it doesn't cover go on to the call-tag itself. The module
// and its entry function share one name, read back with
// getModuleIdentifier(). Uses the
// shared LLVMCONTEXT and the runtime's type data, so the caller must hold
// NomJIT::getContextLock() if the background compiler may run. Every method
// address it needs must already be cached (see CheckCallTagMethodExist), since
// it must not call into the JIT while holding that lock.
std::unique_ptr<llvm::Module>
BuildIMTEntryModule(NomRecord *nomRecord, int imtIndex,
                    const NomInterfaceCallTag *callTag, void *callTagFunAddr,
                    const IMTCallTagSet &parentCallTags);

void *CompileIMTEntryFunction(NomRecord *nomRecord, int imtIndex,
                              const NomInterfaceCallTag *callTag,
                              void *callTagFunAddr, NomIMTNode *imtNode);
} // namespace IMTUtils
} // namespace Runtime
} // namespace Nom