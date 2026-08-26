#pragma once
#include "LibraryVersion.h"
#include <list>
#include <string>

namespace Nom {
namespace Runtime {

class Library {
public:
  const std::string Name;
  const std::list<LibraryVersion> Versions;

  Library(Library &) = delete;
  Library(const Library &) = delete;
  Library(Library &&) = delete;

  Library(const std::string &name, const std::list<LibraryVersion> &&versions);

  ~Library();
};

} // namespace Runtime
} // namespace Nom
