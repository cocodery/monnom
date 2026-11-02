#pragma once
#include "AvailableExternally.h"
#include "NomBuilder.h"
#include "NomType.h"
#include "RTDictionary.h"
#include "llvm/ADT/SmallVector.h"
namespace Nom {
namespace Runtime {
class NomMethod;
class NomInterfaceCallTag : public CallTagExternally {
private:
  std::string key;
  const NomMethod *method;
  DICTKEYTYPE name;
  llvm::SmallVector<TypeReferenceType, 8> argTRTs;
  NomInterfaceCallTag(std::string &&key, const NomMethod *method,
                      DICTKEYTYPE &&name,
                      llvm::SmallVector<TypeReferenceType, 8> &&argTRTs);

public:
  NomInterfaceCallTag(NomInterfaceCallTag &other) = delete;
  NomInterfaceCallTag(NomInterfaceCallTag &&other) = delete;
  static NomInterfaceCallTag *GetCallTag(const NomMethod *method);
  // Inherited via AvailableExternally
  virtual llvm::Constant *
  createLLVMElement(llvm::Module &mod,
                    llvm::GlobalValue::LinkageTypes linkage) const override;
  virtual llvm::Constant *findLLVMElement(llvm::Module &mod) const override;
  const std::string &GetKey() const { return key; }
  const NomMethod *GetMethod() const { return method; }
  static bool ResultNeedsCast(const NomMethod *method);
  static llvm::Value *
  GenerateResultCast(NomBuilder &builder, llvm::Function *fun,
                     llvm::Value *callTagFun, llvm::Value **varargs,
                     llvm::Value *actualResult, const NomMethod *method,
                     size_t argCount);
};
} // namespace Runtime
} // namespace Nom