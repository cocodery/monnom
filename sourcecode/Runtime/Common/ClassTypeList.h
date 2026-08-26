#pragma once

#include "NomType.h"
#include "llvm/ADT/TinyPtrVector.h"
#include <vector>

namespace Nom {
namespace Runtime {
class NomClassType;

class ClassTypeList {
private:
  std::vector<NomClassTypeRef> types;

public:
  std::vector<NomClassTypeRef> *operator->() { return &types; }
  const std::vector<NomClassTypeRef> *operator->() const { return &types; }

  std::vector<NomClassTypeRef> &operator*() { return types; }

  ClassTypeList() {}
  ClassTypeList(int defaultFields) {}

  ~ClassTypeList() {}
};
} // namespace Runtime
} // namespace Nom