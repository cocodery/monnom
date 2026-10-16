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
// Whether the record has a method for callTag's interface method
bool CheckCallTagMethodExist(const NomRecord *nomRecord,
                             const NomInterfaceCallTag *callTag);
// Caches the JIT address of the record's method for callTag
// The method must exist (CheckCallTagMethodExist).
void CacheRecordMethodAddr(const NomRecord *nomRecord,
                           const NomInterfaceCallTag *callTag);
// Builds the IR for an IMT entry covering the parent CallTags plus callTag
// address it needs must already be cached (see CacheRecordMethodAddr)
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