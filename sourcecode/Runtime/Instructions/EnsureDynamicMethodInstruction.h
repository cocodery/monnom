#pragma once
#include "CompileEnv.h"
#include "Defs.h"
#include "NomBuilder.h"
#include "NomInstruction.h"

namespace Nom {
namespace Runtime {
class EnsureDynamicMethodInstruction : public NomInstruction {
public:
  const ConstantID MethodName;
  const RegIndex Receiver;
  EnsureDynamicMethodInstruction(ConstantID methodNameID, RegIndex receiver);
  virtual ~EnsureDynamicMethodInstruction();

  static llvm::Value *GenerateGetBestInvokeDispatcherDyn(NomBuilder &builder,
                                                         NomValue receiver);

  // Inherited via NomInstruction
  virtual void Compile(NomBuilder &builder, CompileEnv *env,
                       int lineno) override;
  virtual void Print(bool resolve = false) override;
  virtual void
  FillConstantDependencies(NOM_CONSTANT_DEPENCENCY_CONTAINER &result) override;
};
} // namespace Runtime
} // namespace Nom