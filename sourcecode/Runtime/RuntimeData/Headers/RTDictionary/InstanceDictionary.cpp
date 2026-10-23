#include "InstanceDictionary.h"
#include "CompileHelpers.h"
#include "RTDictionary.h"

using namespace llvm;
using namespace std;
namespace Nom {
namespace Runtime {
llvm::Value *InstanceDictionary::GenerateGetOrCreate(NomBuilder &builder,
                                                     llvm::Value *dictAddr) {
  auto dict = MakeLoad(builder, dictAddr);
  auto isNull = builder->CreateIsNull(dict, "dictIsNull");
  builder->CreateIntrinsic(Intrinsic::expect, {inttype(1)},
                           {isNull, MakeUInt(1, 0)});
  BasicBlock *createDictBlock =
      BasicBlock::Create(builder->getContext(), "createDictionary",
                         builder->GetInsertBlock()->getParent());
  BasicBlock *concurrentBlock =
      BasicBlock::Create(builder->getContext(), "concurrentlyCreatedDictionary",
                         builder->GetInsertBlock()->getParent());
  BasicBlock *continueBlock =
      BasicBlock::Create(builder->getContext(), "hasDictionary",
                         builder->GetInsertBlock()->getParent());
  BasicBlock *origin = builder->GetInsertBlock();
  builder->CreateCondBr(isNull, createDictBlock, continueBlock);

  builder->SetInsertPoint(createDictBlock);
  auto dictCreate = RTDictionaryCreate::Instance().GetLLVMElement(
      *builder->GetInsertBlock()->getModule());
  auto newDict = builder->CreateCall(dictCreate);
  newDict->setCallingConv(CallingConv::C);
  auto cmpx = builder->CreateAtomicCmpXchg(
      dictAddr, ConstantPointerNull::get(POINTERTYPE), newDict,
      llvm::AtomicOrdering::Monotonic, llvm::AtomicOrdering::Monotonic);
  auto success = builder->CreateExtractValue(cmpx, {1}, "cmpxSuccess");
  builder->CreateIntrinsic(Intrinsic::expect, {inttype(1)},
                           {success, MakeUInt(1, 1)});
  builder->CreateCondBr(success, continueBlock, concurrentBlock);

  builder->SetInsertPoint(concurrentBlock);
  auto oldDict = builder->CreateExtractValue(cmpx, {0}, "oldDictionary");
  builder->CreateBr(continueBlock);

  builder->SetInsertPoint(continueBlock);
  auto merge = builder->CreatePHI(POINTERTYPE, 3, "dict");
  merge->addIncoming(dict, origin);
  merge->addIncoming(newDict, createDictBlock);
  merge->addIncoming(oldDict, concurrentBlock);
  return merge;
}

llvm::Value *InstanceDictionary::GenerateLookup(NomBuilder &builder,
                                                llvm::Value *dict,
                                                llvm::Value *fieldName) {
  auto getFun = RTDictionaryLookup::Instance().GetLLVMElement(
      *builder->GetInsertBlock()->getModule());
  auto call = builder->CreateCall(getFun, {dict, fieldName});
  call->setCallingConv(CallingConv::C);
  return builder->CreatePointerCast(call, REFTYPE);
}

llvm::Value *InstanceDictionary::GenerateSet(NomBuilder &builder,
                                             llvm::Value *dict,
                                             llvm::Value *fieldName,
                                             llvm::Value *value) {
  auto setFun = RTDictionarySet::Instance().GetLLVMElement(
      *builder->GetInsertBlock()->getModule());
  auto call = builder->CreateCall(
      setFun,
      {dict, fieldName, builder->CreatePointerCast(value, POINTERTYPE)});
  call->setCallingConv(CallingConv::C);
  return call;
}

llvm::Value *InstanceDictionary::GenerateFreeze(NomBuilder &builder,
                                                llvm::Value *dict,
                                                llvm::Value *fieldName) {
  auto freezeFun = RTDictionaryFreeze::Instance().GetLLVMElement(
      *builder->GetInsertBlock()->getModule());
  auto call = builder->CreateCall(freezeFun, {dict, fieldName});
  call->setCallingConv(CallingConv::C);
  return call;
}

llvm::Value *InstanceDictionary::GenerateIsFrozen(NomBuilder &builder,
                                                  llvm::Value *dict,
                                                  llvm::Value *fieldName) {
  auto isFrozenFun = RTDictionaryIsFrozen::Instance().GetLLVMElement(
      *builder->GetInsertBlock()->getModule());
  auto call = builder->CreateCall(isFrozenFun, {dict, fieldName});
  call->setCallingConv(CallingConv::C);
  return builder->CreateICmpNE(call, MakeIntLike(call, 0));
}
} // namespace Runtime
} // namespace Nom
