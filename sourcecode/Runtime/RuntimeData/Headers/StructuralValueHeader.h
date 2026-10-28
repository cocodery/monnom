#pragma once
#include "ARTRep.h"
#include "DLLExport.h"
#include "NomBuilder.h"
#include "llvm/IR/Type.h"

namespace Nom {
namespace Runtime {
class NomRecord;
class NomInstruction;
class CompileEnv;
enum class StructuralValueHeaderFields : unsigned char {
  TypeArgs = 0,
  RefValueHeader = 1,
  RawInvokeSpace = 2,
  CastData = 3
};

struct StructuralCastListNode {
  void *Type;
  StructuralCastListNode *Next;
};
extern "C" DLLEXPORT void RT_NOM_AddStructuralCastType(void **castDataSlot,
                                                       void *castType);

class StructuralValueHeader
    : public ARTRep<StructuralValueHeader, StructuralValueHeaderFields> {
public:
  static llvm::StructType *GetLLVMType();
  // the first node of a tagged cast list, as POINTERTYPE*
  static llvm::Value *GenerateCastListNode(NomBuilder &builder,
                                           llvm::Value *castData);
  static llvm::Value *GenerateReadCastListType(NomBuilder &builder,
                                               llvm::Value *node);
  // the next node, or null
  static llvm::Value *GenerateReadCastListNext(NomBuilder &builder,
                                               llvm::Value *node);
  // adds a type to a value that has been cast before
  static void GenerateAddCastType(NomBuilder &builder, llvm::Value *sValue,
                                  llvm::Value *castType);
  static void
  GenerateInitializationCode(NomBuilder &builder, llvm::Value *refValue,
                             llvm::ArrayRef<llvm::Value *> typeArguments,
                             llvm::Constant *vTablePtr,
                             llvm::Constant *rawInvokePointer = nullptr);
  static llvm::Value *GenerateReadTypeArgsPtr(NomBuilder &builder,
                                              llvm::Value *refValue);
  static llvm::Value *GenerateReadCastData(NomBuilder &builder,
                                           llvm::Value *refValue);
  static llvm::Value *GenerateReadTypeArgument(NomBuilder &builder,
                                               llvm::Value *refValue,
                                               llvm::Value *index);
  static llvm::Value *GenerateWriteCastTypePointerCMPXCHG(NomBuilder &builder,
                                                          llvm::Value *refValue,
                                                          llvm::Value *newPtr,
                                                          llvm::Value *oldPtr);
  static void GenerateMonotonicStructuralCast(
      NomBuilder &builder, llvm::Function *fun, llvm::BasicBlock *successBlock,
      llvm::BasicBlock *failBlock, llvm::Value *value,
      NomClassTypeRef rightType, llvm::Value *outerStack);
  static void GenerateMonotonicStructuralCast(
      NomBuilder &builder, llvm::Function *fun, llvm::BasicBlock *successBlock,
      llvm::BasicBlock *failBlock, llvm::Value *value, llvm::Value *rightType,
      llvm::Value *rightIface, llvm::Value *rightTypeArgs,
      llvm::Value *outerStack);
};
} // namespace Runtime
} // namespace Nom