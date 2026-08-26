#pragma once
#include "NomClass.h"
#include "RTClass.h"

namespace Nom {
namespace Runtime {
class NomClassType;
class NomMathClass : public NomClassInternal {
private:
  NomMathClass();

public:
  static NomMathClass *GetInstance();
  virtual ~NomMathClass() override {}
};
} // namespace Runtime
} // namespace Nom