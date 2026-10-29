#pragma once
#include "NomBuilder.h"
#include "NomValue.h"
#include "RTDescriptor.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Value.h"

namespace Nom {
namespace Runtime {
enum class RTVTableFields : unsigned char {
  MethodTable = 0,
  Kind = 1,
  Flags = 2,
  InterfaceMethodTable = 3,
  DynamicDispatcherTable = 4,
  ReadField = 5,
  WriteField = 6,
  NomObjPtr = 7
};
class RTVTable {
public:
  static llvm::StructType *GetLLVMType();

  static llvm::Constant *CreateConstant(RTDescriptorKind kind,
                                        llvm::Constant *flags,
                                        llvm::Constant *interfaceMethodTable,
                                        llvm::Constant *dynamicDispatcherTable,
                                        llvm::Constant *fieldLookupFunction,
                                        llvm::Constant *fieldStoreFunction,
                                        llvm::Constant *nomObjPtr = nullptr);

  static llvm::Value *GenerateReadKind(NomBuilder &builder,
                                       llvm::Value *vtablePtr);
  static llvm::Value *GenerateReadInterfaceMethodTable(NomBuilder &builder,
                                                       llvm::Value *vtablePtr);
  static void GenerateWriteInterfaceMethodTable(NomBuilder &builder,
                                                llvm::Value *vtablePtr,
                                                llvm::Value *imt);
  static llvm::Value *GenerateReadInterfaceMethodTableEntry(
      NomBuilder &builder, llvm::Value *vtablePtr, llvm::Constant *index);
  static llvm::Value *GenerateReadMethodTableEntry(NomBuilder &builder,
                                                   llvm::Value *vtablePtr,
                                                   llvm::Value *offset);
  static llvm::Value *GenerateReadReadFieldFunction(NomBuilder &builder,
                                                    llvm::Value *vtablePtr);
  static llvm::Value *GenerateReadWriteFieldFunction(NomBuilder &builder,
                                                     llvm::Value *vtablePtr);
  static llvm::Value *GenerateReadNomObjPtr(NomBuilder &builder,
                                            llvm::Value *vtablePtr);

  static llvm::Value *GenerateHasRawInvoke(NomBuilder &builder,
                                           llvm::Value *vtablePtr);
  static llvm::Value *GenerateIsNominalValue(NomBuilder &builder,
                                             llvm::Value *vtablePtr);

  // the call goes through an interface
  // a record field found as the callee gets frozen and not be written
  static llvm::Value *
  GenerateFindDynamicDispatcherPair(NomBuilder &builder, llvm::Value *refValue,
                                    llvm::Value *vtablePtr, size_t name,
                                    bool freezeInvokedField = false);
  static llvm::Value *
  GenerateFindDynamicDispatcherPair(NomBuilder &builder, llvm::Value *refValue,
                                    llvm::Value *vtablePtr, llvm::Value *name,
                                    llvm::Value *tableIndex,
                                    bool freezeInvokedField = false);
  // without lambda optimization (--nolambdaopt) there is no raw invoke
  // pointer: an invokable value (lambda, record or object with a `this`
  // method) is invoked through its dynamic dispatcher for the empty name.
  // Takes a reference value, returns its dispatcher pair.
  static llvm::Function *GetFindInvokeDispatcherFunction(llvm::Module &mod);
};
} // namespace Runtime
} // namespace Nom