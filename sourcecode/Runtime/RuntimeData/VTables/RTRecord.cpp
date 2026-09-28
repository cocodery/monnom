#include "RTRecord.h"
#include "CompileHelpers.h"
#include "Context.h"
#include "Defs.h"
#include "NomInterface.h"
#include "NomPartialApplication.h"
#include "NomRecord.h"
#include "RTSignature.h"
#include "RTVTable.h"
#include <cstdint>
#include <llvm/IR/Constants.h>

using namespace llvm;
using namespace std;
namespace Nom {
namespace Runtime {
llvm::StructType *RTRecord::GetLLVMType() {
  static auto rtst = StructType::create(LLVMCONTEXT, "RT_NOM_StructDescriptor");
  static bool once = true;
  if (once) {
    once = false;
    rtst->setBody({
        RTVTable::GetLLVMType(), // common VTable parts
        numtype(size_t),         // field count
        numtype(size_t)          // typearg count
    });
  }
  return rtst;
}

llvm::Constant *
RTRecord::CreateConstant(const NomRecord *record, llvm::Function *fieldRead,
                         llvm::Function *fieldWrite,
                         llvm::Constant *interfaceMethodTable,
                         llvm::Constant *dynamicDispatcherTable) {
  return ConstantStruct::get(
      GetLLVMType(),
      RTVTable::CreateConstant(
          RTDescriptorKind::Record,
          MakeInt32(record->GetHasRawInvoke() ? 1 : 0), interfaceMethodTable,
          dynamicDispatcherTable, fieldRead, fieldWrite,
          llvm::ConstantInt::get(INTTYPE, reinterpret_cast<uint64_t>(record))),
      MakeInt<size_t>(record->Fields.size()),
      MakeInt<size_t>(record->GetTypeParametersCount()));
}

llvm::Value *RTRecord::GenerateReadFieldCount(NomBuilder &builder,
                                              llvm::Value *descriptor) {
  return MakeInvariantLoad(builder, descriptor,
                           RTRecord::GetLLVMType()->getPointerTo(),
                           MakeInt32(RTStructFields::FieldCount), "fieldCount",
                           AtomicOrdering::NotAtomic);
}
llvm::Value *RTRecord::GenerateReadTypeArgCount(NomBuilder &builder,
                                                llvm::Value *descriptor) {
  return MakeInvariantLoad(builder, descriptor,
                           RTRecord::GetLLVMType()->getPointerTo(),
                           MakeInt32(RTStructFields::TypeArgCount),
                           "typeArgCount", AtomicOrdering::NotAtomic);
}
} // namespace Runtime
} // namespace Nom