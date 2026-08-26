#pragma once
#include "NomClass.h"
#include "RTClass.h"

namespace Nom {
namespace Runtime {
class NomClassType;
class NomObjectClass : public NomClassInternal {
private:
  NomObjectClass();

public:
  static NomObjectClass *GetInstance();
  virtual ~NomObjectClass() override {}
};

} // namespace Runtime
} // namespace Nom