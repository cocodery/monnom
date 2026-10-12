#pragma once

#include "NomIMTransition.h"
#include "NomInterfaceCallTag.h"
#include "NomRecord.h"

namespace Nom {
namespace Runtime {
namespace IMTUtils {
void *CompileIMTEntryFunction(NomRecord *nomRecord, int imtIndex,
                              const NomInterfaceCallTag *callTag,
                              void *callTagFunAddr, NomIMTNode *imtNode);

void *LookupCallTagMethodAddr(const NomRecord *nomRecord,
                              const NomInterfaceCallTag *callTag);
} // namespace IMTUtils
} // namespace Runtime
} // namespace Nom