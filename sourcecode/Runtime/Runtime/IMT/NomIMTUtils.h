#pragma once

#include "NomInterfaceCallTag.h"
#include "NomRecord.h"

namespace Nom {
namespace Runtime {
namespace IMTUntils {
void *CompileIMTEntryFunction(NomRecord *nomRecord,
                              NomInterfaceCallTag *callTag, int imtIndex);
} // namespace IMTUntils
} // namespace Runtime
} // namespace Nom