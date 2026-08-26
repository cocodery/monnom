#pragma once
#include "ARTRep.h"
#include "Context.h"
#include "NomBuilder.h"
#include "RTDescriptor.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"

namespace Nom {
namespace Runtime {
enum class RTPartialAppFields : unsigned char {
  VTable = 0,
  NumEntries = 1,
  Entries = 2
};
enum class RTPartialAppEntryFields : unsigned char { Key = 0, Dispatcher = 1 };
class RTPartialApp : public ARTRep<RTPartialApp, RTPartialAppFields> {
public:
  static llvm::StructType *GetLLVMType();
  static llvm::Constant *CreateConstant(
      llvm::ArrayRef<std::pair<std::pair<uint32_t, uint32_t>, llvm::Constant *>>
          entries);
  static llvm::Value *GenerateFindDispatcher(NomBuilder &builder,
                                             llvm::Value *partialAppDesc,
                                             uint32_t typeargcount,
                                             uint32_t argcount);
};
} // namespace Runtime
} // namespace Nom