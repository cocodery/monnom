#pragma once

#include "FileSysDefs.h"
#include <string>

namespace Nom {
namespace Runtime {

class Path {
  friend class Directory;
  friend class File;

private:
  const FSNamespace::path path;

public:
  Path(const std::string &path);

  ~Path();
};
} // namespace Runtime
} // namespace Nom
