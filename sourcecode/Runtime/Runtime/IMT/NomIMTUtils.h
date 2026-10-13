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
// Builds the IR for an IMT entry covering imtNode's call-tags plus
// callTag. The module and its entry function share one name, read back
// with getModuleIdentifier(). Uses the shared LLVMCONTEXT, so it must
// run on the main thread.
std::unique_ptr<llvm::Module>
BuildIMTEntryModule(NomRecord *nomRecord, int imtIndex,
                    const NomInterfaceCallTag *callTag, void *callTagFunAddr,
                    NomIMTNode *imtNode);

void *CompileIMTEntryFunction(NomRecord *nomRecord, int imtIndex,
                              const NomInterfaceCallTag *callTag,
                              void *callTagFunAddr, NomIMTNode *imtNode);

void *LookupCallTagMethodAddr(const NomRecord *nomRecord,
                              const NomInterfaceCallTag *callTag);
} // namespace IMTUtils
} // namespace Runtime
} // namespace Nom