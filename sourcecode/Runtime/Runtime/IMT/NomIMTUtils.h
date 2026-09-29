#pragma once

#include "NomRecord.h"

namespace Nom {
namespace Runtime {
namespace IMTUntils {
void *CompileIMTEntryFunction(NomRecord *nomRecord, int imtIndex);
} // namespace IMTUntils
} // namespace Runtime
} // namespace Nom