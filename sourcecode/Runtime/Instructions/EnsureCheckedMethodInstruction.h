#pragma once
#include "CompileEnv.h"
#include "Defs.h"
#include "NomBuilder.h"
#include "NomInstruction.h"

namespace Nom {
namespace Runtime {
class EnsureCheckedMethodInstruction : public NomInstruction {
public:
  const ConstantID MethodName;
  const RegIndex Receiver;
  EnsureCheckedMethodInstruction(ConstantID methodNameID, RegIndex receiver);
  virtual ~EnsureCheckedMethodInstruction();

  // Inherited via NomInstruction
  virtual void Compile(NomBuilder &builder, CompileEnv *env,
                       int lineno) override;
  virtual void Print(bool resolve = false) override;
  virtual void
  FillConstantDependencies(NOM_CONSTANT_DEPENCENCY_CONTAINER &result) override;
};
} // namespace Runtime
} // namespace Nom