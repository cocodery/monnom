#pragma once
#include <string>
#include <vector>

namespace Nom {
namespace Runtime {
void RegisterGlobalForAddressLookup(std::string name);
std::vector<std::string> &GetGlobalsForAddressLookup();
} // namespace Runtime
} // namespace Nom