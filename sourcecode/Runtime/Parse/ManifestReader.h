#pragma once
#include "CharStream.h"
#include "Manifest.h"
#include "pugixml/pugixml.hpp"
#include <iostream>

namespace Nom {
namespace Runtime {

class ManifestReader {
private:
  ManifestReader();

public:
  static ManifestReader &Instance() {
    static ManifestReader instance;
    return instance;
  }
  ~ManifestReader();

  Manifest *ReadManifest(pugi::xml_document &doc);
  Manifest *ReadManifest(std::string &path);
  Manifest *ReadManifest(void *buffer, size_t bufsize);
};

} // namespace Runtime
} // namespace Nom