#pragma once

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"

namespace Nom {
namespace Runtime {
class RTIMTNode {
public:
  static llvm::StructType *GetLLVMType();
  static llvm::Constant *CreateConstant();
};
} // namespace Runtime
} // namespace Nom