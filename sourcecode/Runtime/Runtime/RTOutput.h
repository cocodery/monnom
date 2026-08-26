#pragma once
#include "AvailableExternally.h"
#include "NomBuilder.h"
#include "NomVMInterface.h"
#include "Singleton.h"

namespace Nom {
namespace Runtime {
class RTOutput_Fail {
private:
  RTOutput_Fail() {}

public:
  ~RTOutput_Fail() {}
  static llvm::FunctionType *GetFunctionType();
  static llvm::Function *GetLLVMElement(llvm::Module &mod);

  static llvm::BasicBlock *GenerateFailUnimplementedBlock(NomBuilder &builder);

  static llvm::BasicBlock *GenerateFailOutputBlock(NomBuilder &builder,
                                                   const char *errorMessage);
  static void MakeBlockFailOutputBlock(NomBuilder &builder,
                                       const char *errorMessage,
                                       llvm::BasicBlock *block);
};

class RTOutput_Name {
private:
  RTOutput_Name() {}

public:
  ~RTOutput_Name() {}
  static llvm::FunctionType *GetFunctionType();
  static llvm::Function *GetLLVMElement(llvm::Module &mod);
};
} // namespace Runtime
} // namespace Nom

extern "C" DLLEXPORT void *NOM_RT_Fail(const char *errstr);