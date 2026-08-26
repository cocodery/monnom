#pragma once

#include "VersionNumber.h"
#include <string>

namespace Nom {
namespace Runtime {
class LibraryDependency {
public:
  std::string Name;
  VersionNumber RequiredVersion;
  LibraryDependency(const std::string &name,
                    const VersionNumber &requiredVersion);
  ~LibraryDependency();
};
} // namespace Runtime
} // namespace Nom
