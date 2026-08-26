#include "RTDynamicType.h"
#include "CompileHelpers.h"
#include "NomDynamicType.h"
#include "RTTypeHead.h"

using namespace llvm;
using namespace std;
namespace Nom {
namespace Runtime {
llvm::StructType *RTDynamicType::GetLLVMType() {
  static llvm::StructType *stype = llvm::StructType::create(
      LLVMCONTEXT, {RTTypeHead::GetLLVMType()}, "NOM_RT_DynamicType");
  return stype;
}

llvm::Constant *RTDynamicType::CreateConstant(llvm::Constant *castFun) {
  return ConstantStruct::get(
      GetLLVMType(),
      RTTypeHead::GetConstant(TypeKind::TKDynamic,
                              MakeInt(NomDynamicType::Instance().GetHashCode()),
                              &NomDynamicType::Instance(), castFun));
}
} // namespace Runtime
} // namespace Nom