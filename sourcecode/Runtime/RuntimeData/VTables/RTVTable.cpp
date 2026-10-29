#include "RTVTable.h"
#include "CallingConvConf.h"
#include "CompileHelpers.h"
#include "Context.h"
#include "Defs.h"
#include "IMT.h"
#include "LambdaHeader.h"
#include "Metadata.h"
#include "NomNameRepository.h"
#include "ObjectHeader.h"
#include "RTConfig.h"
#include "RTConfig_LambdaOpt.h"
#include "RTDescriptor.h"
#include "RTInstanceType.h"
#include "RTInterface.h"
#include "RTInterfaceTableEntry.h"
#include "RTOutput.h"
#include "RecordHeader.h"
#include "RefValueHeader.h"
#include <llvm/Support/AtomicOrdering.h>

using namespace llvm;
using namespace std;
namespace Nom {
namespace Runtime {
llvm::StructType *RTVTable::GetLLVMType() {
  static StructType *rtitt =
      StructType::create(LLVMCONTEXT, "RT_NOM_InterfaceVTable");
  static bool once = true;
  if (once) {
    once = false;
    rtitt->setBody(arrtype(POINTERTYPE, 0), // Class method table (grows upward)
                   numtype(RTDescriptorKind), // Kind
                   numtype(int32_t),          // Flags
                   arrtype(GetIMTFunctionType()->getPointerTo(),
                           IMTsize), // Interface method table
                   arrtype(GetDynamicDispatchListEntryType()->getPointerTo(),
                           IMTsize), // Dynamic dispatcher retrieval
                   GetFieldReadFunctionType()->getPointerTo(),  // field lookup
                   GetFieldWriteFunctionType()->getPointerTo(), // field store,
                   INTTYPE);
  }
  return rtitt;
}
llvm::Constant *RTVTable::CreateConstant(RTDescriptorKind kind,
                                         llvm::Constant *flags,
                                         llvm::Constant *interfaceMethodTable,
                                         llvm::Constant *dynamicDispatcherTable,
                                         llvm::Constant *fieldLookupFunction,
                                         llvm::Constant *fieldStoreFunction,
                                         llvm::Constant *nomObjPtr) {
  return ConstantStruct::get(
      GetLLVMType(), ConstantArray::get(arrtype(POINTERTYPE, 0), {}),
      MakeInt<RTDescriptorKind>(kind),
      ConstantExpr::getTruncOrBitCast(flags, numtype(int32_t)),
      interfaceMethodTable, dynamicDispatcherTable, fieldLookupFunction,
      fieldStoreFunction,
      (nomObjPtr != nullptr) ? nomObjPtr : llvm::ConstantInt::get(INTTYPE, 0));
}
llvm::Value *RTVTable::GenerateReadKind(NomBuilder &builder,
                                        llvm::Value *vtablePtr) {
  return MakeInvariantLoad(
      builder,
      builder->CreatePointerCast(vtablePtr, GetLLVMType()->getPointerTo()),
      MakeInt32(RTVTableFields::Kind), "VTableKind", AtomicOrdering::NotAtomic);
}
llvm::Value *
RTVTable::GenerateReadInterfaceMethodTable(NomBuilder &builder,
                                           llvm::Value *vtablePtr) {
  return builder->CreateGEP(
      builder->CreatePointerCast(vtablePtr, GetLLVMType()->getPointerTo()),
      {MakeInt32(0), MakeInt32(RTVTableFields::InterfaceMethodTable)});
}
void RTVTable::GenerateWriteInterfaceMethodTable(NomBuilder &builder,
                                                 llvm::Value *vtablePtr,
                                                 llvm::Value *imt) {
  MakeInvariantStore(
      builder,
      builder->CreatePointerCast(
          imt, arrtype(GetIMTFunctionType()->getPointerTo(), IMTsize)),
      builder->CreateGEP(
          builder->CreatePointerCast(vtablePtr, GetLLVMType()->getPointerTo()),
          {MakeInt32(0), MakeInt32(RTVTableFields::InterfaceMethodTable)}),
      AtomicOrdering::NotAtomic);
}
llvm::Value *RTVTable::GenerateReadInterfaceMethodTableEntry(
    NomBuilder &builder, llvm::Value *vtablePtr, llvm::Constant *index) {
  auto entryPtr = builder->CreateGEP(
      builder->CreatePointerCast(vtablePtr, GetLLVMType()->getPointerTo()),
      {MakeInt32(0), MakeInt32(RTVTableFields::InterfaceMethodTable), index});
  // IMT transitions rewrite slots at run time. An invariant load would let
  // LLVM hoist the slot out of loops or merge it with an earlier read, so
  // a loop that started before its entry was installed would keep calling
  // the old entry, falling back on every call.
  if (RTConfig_EnableTransition) {
    return builder->CreateLoad(entryPtr, "IMTEntry");
  }
  return MakeInvariantLoad(builder, entryPtr, "IMTEntry",
                           AtomicOrdering::NotAtomic);
}
llvm::Value *RTVTable::GenerateReadMethodTableEntry(NomBuilder &builder,
                                                    llvm::Value *vtablePtr,
                                                    llvm::Value *offset) {
  return MakeInvariantLoad(
      builder,
      builder->CreateGEP(
          builder->CreatePointerCast(vtablePtr, GetLLVMType()->getPointerTo()),
          {MakeInt32(0), MakeInt32(RTVTableFields::MethodTable),
           builder->CreateSub(MakeInt32(-1), offset)}),
      "MethodPointer", AtomicOrdering::NotAtomic);
}
llvm::Value *RTVTable::GenerateReadReadFieldFunction(NomBuilder &builder,
                                                     llvm::Value *vtablePtr) {
  return MakeInvariantLoad(
      builder,
      builder->CreatePointerCast(vtablePtr, GetLLVMType()->getPointerTo()),
      MakeInt32(RTVTableFields::ReadField), "readFieldFun",
      AtomicOrdering::NotAtomic);
}
llvm::Value *RTVTable::GenerateReadWriteFieldFunction(NomBuilder &builder,
                                                      llvm::Value *vtablePtr) {
  return MakeInvariantLoad(
      builder,
      builder->CreatePointerCast(vtablePtr, GetLLVMType()->getPointerTo()),
      MakeInt32(RTVTableFields::WriteField), "writeFieldFun",
      AtomicOrdering::NotAtomic);
}
llvm::Value *RTVTable::GenerateReadNomObjPtr(NomBuilder &builder,
                                             llvm::Value *vtablePtr) {
  return MakeInvariantLoad(
      builder,
      builder->CreatePointerCast(vtablePtr, GetLLVMType()->getPointerTo()),
      MakeInt32(RTVTableFields::NomObjPtr), "", AtomicOrdering::NotAtomic);
}
llvm::Value *RTVTable::GenerateFindDynamicDispatcherPair(
    NomBuilder &builder, Value *refValue, Value *vtablePtr, size_t name,
    bool freezeInvokedField) {
  return GenerateFindDynamicDispatcherPair(
      builder, refValue, vtablePtr, MakeUInt(64, name),
      MakeUInt(32, name % IMTsize), freezeInvokedField);
}
llvm::Value *RTVTable::GenerateHasRawInvoke(NomBuilder &builder,
                                            Value *vtablePtr) {
  if (RTConfig_UseLambdaOffset) {
    return builder->CreateTrunc(
        builder->CreateLShr(
            builder->CreatePtrToInt(vtablePtr, numtype(intptr_t)),
            MakeInt<intptr_t>(3)),
        inttype(1));
  } else {
    auto flags = MakeInvariantLoad(
        builder,
        builder->CreatePointerCast(vtablePtr, GetLLVMType()->getPointerTo()),
        MakeInt32(RTVTableFields::Flags), "Flags", AtomicOrdering::NotAtomic);
    return builder->CreateICmpEQ(
        builder->CreateAnd(flags, MakeIntLike(flags, 1)),
        MakeIntLike(flags, 1));
  }
}
llvm::Value *RTVTable::GenerateIsNominalValue(NomBuilder &builder,
                                              Value *vtablePtr) {
  return builder->CreateTrunc(
      builder->CreateLShr(builder->CreatePtrToInt(vtablePtr, numtype(intptr_t)),
                          MakeInt<intptr_t>(4)),
      inttype(1));
}
llvm::Value *RTVTable::GenerateFindDynamicDispatcherPair(
    NomBuilder &builder, Value *refValue, Value *vtablePtr, Value *name,
    Value *tableIndex, bool freezeInvokedField) {
  auto listPtr = MakeInvariantLoad(
      builder,
      builder->CreatePointerCast(vtablePtr, GetLLVMType()->getPointerTo()),
      {MakeInt32(RTVTableFields::DynamicDispatcherTable), tableIndex}, "DDTE",
      AtomicOrdering::NotAtomic);

  BasicBlock *origBlock = builder->GetInsertBlock();
  auto fun = origBlock->getParent();

  BasicBlock *loopHeadBlock =
      BasicBlock::Create(LLVMCONTEXT, "DDLookupLoop$Head", fun);
  BasicBlock *loopBodyBlock =
      BasicBlock::Create(LLVMCONTEXT, "DDLookupLoop$Body", fun);
  BasicBlock *loopMatchBlock =
      BasicBlock::Create(LLVMCONTEXT, "DDLookupLoop$Match", fun);
  BasicBlock *loopMatchFieldBlock =
      BasicBlock::Create(LLVMCONTEXT, "DDLookupLoop$Match$Field", fun);
  BasicBlock *loopMatchMethodBlock =
      BasicBlock::Create(LLVMCONTEXT, "DDLookupLoop$Match$Method", fun);
  //   BasicBlock *loopDictionaryBlock =
  //       RTOutput_Fail::GenerateFailUnimplementedBlock(builder);
  BasicBlock *loopDictionaryBlock =
      BasicBlock::Create(LLVMCONTEXT, "DDLookupLoop$IsRecord", fun);
  BasicBlock *loopRecordDictionaryBlock =
      BasicBlock::Create(LLVMCONTEXT, "DDLookupLoop$Dictionary", fun);
  BasicBlock *errorBlock =
      RTOutput_Fail::GenerateFailOutputBlock(builder, "No such method!");
  BasicBlock *checkLambdaExistsBlock =
      BasicBlock::Create(LLVMCONTEXT, "checkLambdaExists", fun);
  BasicBlock *outBlock = BasicBlock::Create(LLVMCONTEXT, "DDLookup$out", fun);

  builder->SetInsertPoint(outBlock);
  auto outPHI = builder->CreatePHI(GetDynamicDispatcherLookupResultType(), 3);

  builder->SetInsertPoint(origBlock);

  builder->CreateBr(loopHeadBlock);

  builder->SetInsertPoint(loopHeadBlock);
  auto indexPHI = builder->CreatePHI(inttype(32), 2);
  indexPHI->addIncoming(MakeInt32(0), origBlock);
  auto currentEntry = builder->CreateGEP(listPtr, indexPHI, "currentEntry");
  auto currentKey = MakeInvariantLoad(
      builder, currentEntry, MakeInt32(DynamicDispatchListEntryFields::Key),
      "DDTEKey", AtomicOrdering::NotAtomic);
  auto currentKeyIsNotNull = builder->CreateICmpNE(
      currentKey, MakeIntLike(currentKey, 0), "keyIsNotNull");
  builder->CreateIntrinsic(Intrinsic::expect, {inttype(1)},
                           {currentKeyIsNotNull, MakeUInt(1, 1)});
  builder->CreateCondBr(currentKeyIsNotNull, loopBodyBlock, loopDictionaryBlock,
                        GetLikelyFirstBranchMetadata());

  builder->SetInsertPoint(loopBodyBlock);
  auto isMatch = builder->CreateICmpEQ(
      currentKey, builder->CreateZExtOrTrunc(name, currentKey->getType()),
      "keyMatch");
  auto nextIndex = builder->CreateAdd(indexPHI, MakeIntLike(indexPHI, 1));
  indexPHI->addIncoming(nextIndex, builder->GetInsertBlock());
  builder->CreateIntrinsic(Intrinsic::expect, {inttype(1)},
                           {isMatch, MakeUInt(1, 1)});
  builder->CreateCondBr(isMatch, loopMatchBlock, loopHeadBlock,
                        GetLikelyFirstBranchMetadata());

  builder->SetInsertPoint(loopMatchBlock);
  auto flag = MakeInvariantLoad(
      builder, currentEntry, MakeInt32(DynamicDispatchListEntryFields::Flags),
      "DDTEFlags", AtomicOrdering::NotAtomic);
  auto isMethod = builder->CreateICmpEQ(flag, MakeIntLike(flag, 0), "isMethod");
  builder->CreateIntrinsic(Intrinsic::expect, {inttype(1)},
                           {isMethod, MakeUInt(1, 1)});
  builder->CreateCondBr(isMethod, loopMatchMethodBlock, loopMatchFieldBlock,
                        GetLikelyFirstBranchMetadata());

  builder->SetInsertPoint(loopMatchMethodBlock);
  {
    auto dispatcher =
        MakeInvariantLoad(builder, currentEntry,
                          MakeInt32(DynamicDispatchListEntryFields::Dispatcher),
                          "dispatcher", AtomicOrdering::Unordered);
    auto pairfirst = builder->CreateInsertValue(
        UndefValue::get(GetDynamicDispatcherLookupResultType()), dispatcher,
        {0});
    auto retval = builder->CreateInsertValue(
        pairfirst, EnsurePackedUnpacked(builder, refValue, POINTERTYPE), {1});
    outPHI->addIncoming(retval, builder->GetInsertBlock());
    builder->CreateBr(outBlock);
  }

  builder->SetInsertPoint(checkLambdaExistsBlock);
  BasicBlock *loadLambdaBlock =
      BasicBlock::Create(LLVMCONTEXT, "loadLambda", fun);
  static const char *noLambdaMsg = "Tried to load invokable value from field, "
                                   "but value has no lambda method!";
  BasicBlock *noLambdaBlock =
      RTOutput_Fail::GenerateFailOutputBlock(builder, noLambdaMsg);
  auto fieldValuePHI = builder->CreatePHI(REFTYPE, 3, "fieldValue");
  BasicBlock *isRefValueBlock =
      BasicBlock::Create(LLVMCONTEXT, "fieldIsRefValue", fun);
  auto fieldValueAsInt = builder->CreatePtrToInt(fieldValuePHI, INTTYPE);
  auto isRefValue = builder->CreateAnd(
      builder->CreateICmpEQ(
          builder->CreateAnd(fieldValueAsInt, MakeIntLike(fieldValueAsInt, 3)),
          MakeIntLike(fieldValueAsInt, 0)),
      builder->CreateICmpNE(fieldValueAsInt, MakeIntLike(fieldValueAsInt, 0)));
  builder->CreateIntrinsic(Intrinsic::expect, {inttype(1)},
                           {isRefValue, MakeUInt(1, 1)});
  builder->CreateCondBr(isRefValue, isRefValueBlock, noLambdaBlock,
                        GetLikelyFirstBranchMetadata());

  builder->SetInsertPoint(isRefValueBlock);
  auto hasLambda = GenerateHasRawInvoke(
      builder,
      RefValueHeader::GenerateReadVTablePointer(builder, fieldValuePHI));
  builder->CreateIntrinsic(Intrinsic::expect, {inttype(1)},
                           {hasLambda, MakeUInt(1, 1)});
  builder->CreateCondBr(hasLambda, loadLambdaBlock, noLambdaBlock,
                        GetLikelyFirstBranchMetadata());

  builder->SetInsertPoint(loadLambdaBlock);
  if (NomLambdaOptimizationLevel > 0) {
    auto lambda = RefValueHeader::GenerateReadRawInvoke(builder, fieldValuePHI);
    auto pairfirst = builder->CreateInsertValue(
        UndefValue::get(GetDynamicDispatcherLookupResultType()),
        builder->CreatePointerCast(lambda,
                                   GetIMTFunctionType()->getPointerTo()),
        {0});
    auto retval = builder->CreateInsertValue(
        pairfirst, EnsurePackedUnpacked(builder, fieldValuePHI, POINTERTYPE),
        {1});
    outPHI->addIncoming(retval, builder->GetInsertBlock());
    builder->CreateBr(outBlock);
  } else {
    auto findFun = GetFindInvokeDispatcherFunction(*fun->getParent());
    auto retval = builder->CreateCall(findFun, {fieldValuePHI});
    retval->setCallingConv(findFun->getCallingConv());
    outPHI->addIncoming(retval, builder->GetInsertBlock());
    builder->CreateBr(outBlock);
  }

  BasicBlock *loopClassDictionaryBlock =
      BasicBlock::Create(LLVMCONTEXT, "DDLookupLoop$ClassDictionary", fun);
  builder->SetInsertPoint(loopDictionaryBlock);
  {
    BasicBlock *notRecordBlock =
        BasicBlock::Create(LLVMCONTEXT, "DDLookupLoop$IsClass", fun);
    auto kind = GenerateReadKind(builder, vtablePtr);
    auto isRecord = builder->CreateICmpEQ(
        kind, MakeIntLike(kind, (uint64_t)RTDescriptorKind::Record));
    builder->CreateIntrinsic(Intrinsic::expect, {inttype(1)},
                             {isRecord, MakeUInt(1, 1)});
    builder->CreateCondBr(isRecord, loopRecordDictionaryBlock,
                          notRecordBlock);

    builder->SetInsertPoint(notRecordBlock);
    auto isClass = builder->CreateICmpEQ(
        kind, MakeIntLike(kind, (uint64_t)RTDescriptorKind::Class));
    builder->CreateCondBr(isClass, loopClassDictionaryBlock, errorBlock);

    builder->SetInsertPoint(loopRecordDictionaryBlock);
    auto dfield = builder->CreatePointerCast(
        RecordHeader::GenerateReadDictField(builder, refValue, name), REFTYPE);
    if (freezeInvokedField) { // freeze dict field invoke through interface
      RecordHeader::GenerateFreezeDictField(builder, refValue, name);
    }
    fieldValuePHI->addIncoming(dfield, builder->GetInsertBlock());
    builder->CreateBr(checkLambdaExistsBlock);
  }

  builder->SetInsertPoint(loopClassDictionaryBlock);
  {
    // the class's field lookup reads its instance dictionary for names
    // that are not declared fields; classes without one yield null. No
    // freezing here: record call tags never dispatch on class objects
    auto fieldLookupFun =
        GenerateReadReadFieldFunction(builder, vtablePtr);
    auto cfieldCall = builder->CreateCall(
        GetFieldReadFunctionType(), fieldLookupFun,
        {builder->CreateZExtOrTrunc(name, numtype(size_t)),
         builder->CreatePointerCast(
             refValue, GetFieldReadFunctionType()->getParamType(1))},
        "classDictField");
    cfieldCall->setCallingConv(NOMCC);
    auto cfield = builder->CreatePointerCast(cfieldCall, REFTYPE);
    auto isNull = builder->CreateIsNull(cfield, "classDictFieldIsNull");
    BasicBlock *classDictFoundBlock = BasicBlock::Create(
        LLVMCONTEXT, "DDLookupLoop$ClassDictionary$Found", fun);
    builder->CreateCondBr(isNull, errorBlock, classDictFoundBlock,
                          GetLikelySecondBranchMetadata());
    builder->SetInsertPoint(classDictFoundBlock);
    fieldValuePHI->addIncoming(cfield, builder->GetInsertBlock());
    builder->CreateBr(checkLambdaExistsBlock);
  }

  builder->SetInsertPoint(loopMatchFieldBlock);
  {
    BasicBlock *classFieldBlock =
        BasicBlock::Create(LLVMCONTEXT, "classFieldRead", fun);
    BasicBlock *recordFieldBlock =
        BasicBlock::Create(LLVMCONTEXT, "recordFieldRead", fun);
    auto fieldIndex = builder->CreatePtrToInt(
        MakeInvariantLoad(builder, currentEntry,
                          MakeInt32(DynamicDispatchListEntryFields::Dispatcher),
                          "fieldNumber", AtomicOrdering::Unordered),
        numtype(intptr_t));
    auto isNominalValue = GenerateIsNominalValue(builder, vtablePtr);
    builder->CreateIntrinsic(Intrinsic::expect, {inttype(1)},
                             {isNominalValue, MakeUInt(1, 1)});
    builder->CreateCondBr(isNominalValue, classFieldBlock, recordFieldBlock,
                          GetLikelyFirstBranchMetadata());

    builder->SetInsertPoint(classFieldBlock);
    {
      // BasicBlock* loadClassFieldBlock = BasicBlock::Create(LLVMCONTEXT,
      // "classFieldRead$load", fun);
      auto actualIndex = builder->CreateTrunc(
          builder->CreateLShr(fieldIndex, MakeIntLike(fieldIndex, 32)),
          inttype(32));
      // auto indexFlags = builder->CreateTrunc(fieldIndex, inttype(2));

      // auto noSpecialFlags = builder->CreateICmpEQ(indexFlags,
      // MakeIntLike(indexFlags, 0)); builder->CreateCondBr(noSpecialFlags,
      // loadClassFieldBlock, noLambdaBlock);

      // builder->SetInsertPoint(loadClassFieldBlock);
      auto fieldValue = ObjectHeader::ReadField(
          builder, refValue, actualIndex,
          false); // hasRawInvoke is false because this table is already filled
                  // with appropriately changed indices

      BasicBlock *refValueBlock = nullptr, *intBlock = nullptr,
                 *floatBlock = nullptr;
      RefValueHeader::GenerateRefOrPrimitiveValueSwitch(
          builder, fieldValue, &refValueBlock, &intBlock, &floatBlock, false,
          nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

      if (refValueBlock != nullptr) {
        builder->SetInsertPoint(refValueBlock);
        fieldValuePHI->addIncoming(fieldValue, builder->GetInsertBlock());
        builder->CreateBr(checkLambdaExistsBlock);
      }
      if (intBlock != nullptr) {
        builder->SetInsertPoint(intBlock);
        builder->CreateBr(noLambdaBlock);
      }
      if (floatBlock != nullptr) {
        builder->SetInsertPoint(floatBlock);
        builder->CreateBr(noLambdaBlock);
      }
    }

    builder->SetInsertPoint(recordFieldBlock);
    {
      auto actualIndex = builder->CreateTrunc(
          builder->CreateLShr(fieldIndex, MakeIntLike(fieldIndex, 32)),
          inttype(32));
      auto fieldsOffset = builder->CreateTrunc(fieldIndex, inttype(32));
      auto fieldValue = RecordHeader::GenerateReadField(
          builder, refValue,
          builder->CreateAdd(
              actualIndex, fieldsOffset)); // hasRawInvoke is false because this
                                           // table is already filled with
                                           // appropriately changed indices
      // the field value is re-read on every call and writes are cast to the
      // field's type so an overwritten field may be invoked
      // once invoked through an interface the field is frozen
      if (freezeInvokedField) { // freeze record field invoke through interface
        RecordHeader::GenerateWriteWrittenTag(builder, refValue, actualIndex);
      }
      fieldValuePHI->addIncoming(fieldValue, builder->GetInsertBlock());
      builder->CreateBr(checkLambdaExistsBlock);
    }
  }

  builder->SetInsertPoint(outBlock);
  return outPHI;
}

llvm::Function *RTVTable::GetFindInvokeDispatcherFunction(llvm::Module &mod) {
  static const char *name = "MONNOM_RT_FIND_INVOKE_DISPATCHER";
  Function *fun = mod.getFunction(name);
  if (fun != nullptr) {
    return fun;
  }
  fun =
      Function::Create(FunctionType::get(GetDynamicDispatcherLookupResultType(),
                                         {REFTYPE}, false),
                       GlobalValue::LinkageTypes::InternalLinkage, name, mod);
  fun->setCallingConv(NOMCC);
  NomBuilder builder;
  BasicBlock *startBlock = BasicBlock::Create(LLVMCONTEXT, "", fun);
  builder->SetInsertPoint(startBlock);
  Value *refValue = fun->arg_begin();
  // a value with a field named "" invoked through this field would call this
  // function again at runtime, not while generating it
  auto pair = GenerateFindDynamicDispatcherPair(
      builder, refValue,
      RefValueHeader::GenerateReadVTablePointer(builder, refValue),
      NomNameRepository::Instance().GetNameID(""));
  builder->CreateRet(pair);
  return fun;
}
} // namespace Runtime
} // namespace Nom