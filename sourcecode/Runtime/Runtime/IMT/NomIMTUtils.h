#pragma once

#include "NomIMTransition.h"
#include "NomInterfaceCallTag.h"
#include "NomRecord.h"

namespace Nom {
namespace Runtime {
namespace IMTUtils {
void *CompileIMTEntryFunction(NomRecord *nomRecord, int imtIndex,
                              void *callTagFunAddr, NomIMTNode *imtNode);
} // namespace IMTUtils
} // namespace Runtime
} // namespace Nom