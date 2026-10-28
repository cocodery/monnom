#include "StructuralValueHeader.h"
#include "CastStats.h"
#include "CompileEnv.h"
#include "CompileHelpers.h"
#include "Metadata.h"
#include "NomAlloc.h"
#include "NomClassType.h"
#include "NomTypeRegistry.h"
#include "RTCast.h"
#include "RTCompileConfig.h"
#include "RTInterface.h"
#include "RTLambda.h"
#include "RTOutput.h"
#include "RTRecord.h"
#include "RTSignature.h"
#include "RTSubtyping.h"
#include "RTVTable.h"
#include "RefValueHeader.h"

using namespace llvm;
using namespace std;
namespace Nom {
namespace Runtime {
static const char *unimplementedMsg = "UNIMPLEMENTED!";
static const char *notAFunctionalInterfaceMsg =
    "Trying to cast lambda to non-functional interface!";

extern "C" DLLEXPORT void RT_NOM_AddStructuralCastType(void **castDataSlot,
                                                       void *castType) {
  // current cast data
  intptr_t old = __atomic_load_n((intptr_t *)castDataSlot, __ATOMIC_ACQUIRE);
  while (true) {
    intptr_t replacement;
    if (old == 0) { // never cast yet
      replacement = (intptr_t)castType;
    } else {
      auto node =
          (StructuralCastListNode *)gcalloc(sizeof(StructuralCastListNode));
      node->Type = castType;
      if (old & 1) { // already a list
        node->Next = (StructuralCastListNode *)(old & ~(intptr_t)1);
      } else { // single cast
        auto first =
            (StructuralCastListNode *)gcalloc(sizeof(StructuralCastListNode));
        first->Type = (void *)old;
        first->Next = nullptr;
        node->Next = first;
      }
      replacement = (intptr_t)node | 1;
    }
    // If another thread changed the slot in the meantime
    // the CAS writes the current value into old and the loop rebuilds from that
    if (__atomic_compare_exchange_n((intptr_t *)castDataSlot, &old, replacement,
                                    false, __ATOMIC_ACQ_REL,
                                    __ATOMIC_ACQUIRE)) {
      return;
    }
  }
}

static llvm::Function *GetAddStructuralCastTypeFunction(llvm::Module &mod) {
  llvm::Function *fun = mod.getFunction("RT_NOM_AddStructuralCastType");
  if (fun == nullptr) {
    fun = Function::Create(
        FunctionType::get(Type::getVoidTy(LLVMCONTEXT),
                          {POINTERTYPE->getPointerTo(), POINTERTYPE}, false),
        GlobalValue::LinkageTypes::ExternalLinkage,
        "RT_NOM_AddStructuralCastType", mod);
  }
  return fun;
}

static llvm::Value *GenerateCastDataSlot(NomBuilder &builder,
                                         llvm::Value *sValue) {
  return builder->CreatePointerCast(
      builder->CreateGEP(
          builder->CreatePointerCast(
              sValue, StructuralValueHeader::GetLLVMType()->getPointerTo()),
          {MakeInt32(0), MakeInt32(StructuralValueHeaderFields::CastData)}),
      POINTERTYPE->getPointerTo());
}

llvm::Value *
StructuralValueHeader::GenerateCastListNode(NomBuilder &builder,
                                            llvm::Value *castData) {
  return builder->CreateIntToPtr(
      builder->CreateAnd(builder->CreatePtrToInt(castData, numtype(intptr_t)),
                         MakeInt<intptr_t>(~(intptr_t)1)),
      POINTERTYPE->getPointerTo());
}

llvm::Value *
StructuralValueHeader::GenerateReadCastListType(NomBuilder &builder,
                                                llvm::Value *node) {
  return MakeLoad(builder, builder->CreateGEP(node, MakeInt32(0)),
                  "castListType", AtomicOrdering::NotAtomic);
}

llvm::Value *
StructuralValueHeader::GenerateReadCastListNext(NomBuilder &builder,
                                                llvm::Value *node) {
  return builder->CreatePointerCast(
      MakeLoad(builder, builder->CreateGEP(node, MakeInt32(1)), "castListNext",
               AtomicOrdering::NotAtomic),
      POINTERTYPE->getPointerTo());
}

void StructuralValueHeader::GenerateAddCastType(NomBuilder &builder,
                                                llvm::Value *sValue,
                                                llvm::Value *castType) {
  auto fun = GetAddStructuralCastTypeFunction(
      *builder->GetInsertBlock()->getParent()->getParent());
  builder->CreateCall(fun, {GenerateCastDataSlot(builder, sValue),
                            builder->CreatePointerCast(castType, POINTERTYPE)});
}
llvm::StructType *StructuralValueHeader::GetLLVMType() {
  static llvm::StructType *shst =
      StructType::create(LLVMCONTEXT, "RT_NOM_StructHeader");
  static bool once = true;
  if (once) {
    once = false;
    shst->setBody(arrtype(TYPETYPE, 0),          // Closure type args
                  RefValueHeader::GetLLVMType(), // vtable
                  (NomLambdaOptimizationLevel > 0
                       ? static_cast<llvm::Type *>(POINTERTYPE)
                       : arrtype(POINTERTYPE,
                                 0)), // potential space for raw invoke pointer
                  POINTERTYPE         // cast data
    );
  }
  return shst;
}

void StructuralValueHeader::GenerateInitializationCode(
    NomBuilder &builder, llvm::Value *refValue,
    llvm::ArrayRef<llvm::Value *> typeArguments, llvm::Constant *vTablePtr,
    llvm::Constant *rawInvokePointer) {
  auto castedDescriptor =
      builder->CreatePointerCast(refValue, GetLLVMType()->getPointerTo());
  int targIndex = -1;
  for (auto &targ : typeArguments) {
    auto targAddress = builder->CreateGEP(
        castedDescriptor,
        {MakeInt32(0), MakeInt32(StructuralValueHeaderFields::TypeArgs),
         MakeInt32(targIndex)});
    MakeInvariantStore(builder, targ, targAddress);
    targIndex--;
  }
  RefValueHeader::GenerateInitializerCode(
      builder,
      builder->CreateGEP(
          castedDescriptor,
          {MakeInt32(0),
           MakeInt32(StructuralValueHeaderFields::RefValueHeader)}),
      vTablePtr, rawInvokePointer);
}

llvm::Value *
StructuralValueHeader::GenerateReadTypeArgsPtr(NomBuilder &builder,
                                               llvm::Value *sValue) {
  return builder->CreateGEP(
      builder->CreatePointerCast(sValue, GetLLVMType()->getPointerTo()),
      {MakeInt32(0), MakeInt32(StructuralValueHeaderFields::TypeArgs)});
}

llvm::Value *StructuralValueHeader::GenerateReadCastData(NomBuilder &builder,
                                                         llvm::Value *sValue) {
  auto loadInst = MakeLoad(builder, sValue, GetLLVMType()->getPointerTo(),
                           MakeInt32(StructuralValueHeaderFields::CastData),
                           "castData", AtomicOrdering::Acquire);
  return loadInst;
}

llvm::Value *StructuralValueHeader::GenerateReadTypeArgument(
    NomBuilder &builder, llvm::Value *sValue, llvm::Value *index) {
  auto targAddress = builder->CreateGEP(
      builder->CreatePointerCast(sValue, GetLLVMType()->getPointerTo()),
      {MakeInt32(0), MakeInt32(StructuralValueHeaderFields::TypeArgs),
       builder->CreateSub(MakeInt32(-1),
                          builder->CreateZExtOrTrunc(index, inttype(32)))});
  auto loadInst = MakeInvariantLoad(builder, targAddress, "typeArgument");
  return loadInst;
}

llvm::Value *StructuralValueHeader::GenerateWriteCastTypePointerCMPXCHG(
    NomBuilder &builder, llvm::Value *thisObj, llvm::Value *newPtr,
    llvm::Value *oldPtr) {
  auto argPtr = builder->CreateGEP(
      builder->CreatePointerCast(thisObj, GetLLVMType()->getPointerTo()),
      {MakeInt32(0), MakeInt32(StructuralValueHeaderFields::CastData)});
  return builder->CreateAtomicCmpXchg(argPtr, oldPtr, newPtr,
                                      AtomicOrdering::AcquireRelease,
                                      AtomicOrdering::Acquire);
}

void StructuralValueHeader::GenerateMonotonicStructuralCast(
    NomBuilder &builder, llvm::Function *fun, llvm::BasicBlock *successBlock,
    llvm::BasicBlock *failBlock, llvm::Value *value, NomClassTypeRef rightType,
    llvm::Value *outerStack) {
  const unsigned int extractIndex[2] = {0, 1};
  auto rightNamed = rightType->Named;
  if (!rightNamed->IsInterface()) {
    builder->CreateBr(failBlock);
    return;
  }

  auto typeUniqueFun =
      TypeRegistry::Instance().GetLLVMElement(*fun->getParent());
  BasicBlock *startBlock =
      BasicBlock::Create(LLVMCONTEXT, "monoCastStart", fun);
  BasicBlock *cmpxLoopBlock =
      BasicBlock::Create(LLVMCONTEXT, "monoCastCMPXLoop$Head", fun);
  BasicBlock *firstCastBlock =
      BasicBlock::Create(LLVMCONTEXT, "firstCast", fun);
  BasicBlock *notFirstCastBlock =
      BasicBlock::Create(LLVMCONTEXT, "notFirstCast", fun);
  BasicBlock *unimplementedBlock =
      RTOutput_Fail::GenerateFailOutputBlock(builder, unimplementedMsg);
  builder->CreateBr(startBlock);

  builder->SetInsertPoint(startBlock);
  auto castData = StructuralValueHeader::GenerateReadCastData(builder, value);
  if (!rightType->ContainsVariables()) {
    auto immediateMatch = CreatePointerEq(
        builder, castData, rightType->GetLLVMElement(*fun->getParent()));
    builder->CreateCondBr(immediateMatch, successBlock, cmpxLoopBlock);
  } else {
    builder->CreateBr(cmpxLoopBlock);
  }

  builder->SetInsertPoint(cmpxLoopBlock);
  auto currentCastData =
      builder->CreatePHI(castData->getType(), 2, "castDataPHI");
  currentCastData->addIncoming(castData, startBlock);
  auto hasBeenCast = builder->CreateIsNotNull(currentCastData);
  builder->CreateCondBr(hasBeenCast, notFirstCastBlock, firstCastBlock);

  {
    builder->SetInsertPoint(firstCastBlock);
    Value *vTableVar = nullptr;
    BasicBlock *lambdaBlock = nullptr, *recordBlock = nullptr;

    RefValueHeader::GenerateStructuralValueKindSwitch(
        builder, value, &vTableVar, &lambdaBlock, &recordBlock,
        &unimplementedBlock);

    BasicBlock *doWriteCastTypeBlock =
        BasicBlock::Create(LLVMCONTEXT, "writeSingleCastType", fun);
    BasicBlock *potentiallyCheckSignatureBlock = doWriteCastTypeBlock;
    if (rightNamed->HasLambdaMethod()) {
      if (RTConfig_CheckLambdaSignaturesAtCast) {
        potentiallyCheckSignatureBlock =
            BasicBlock::Create(LLVMCONTEXT, "checkLambdaSignature", fun);
        builder->SetInsertPoint(potentiallyCheckSignatureBlock);
        // TODO: implement if actually needed
      }
    }

    if (rightNamed->IsFunctional() || rightNamed->HasNoMethods()) {
      builder->SetInsertPoint(lambdaBlock);
      builder->CreateBr(potentiallyCheckSignatureBlock);
    } else {
      RTOutput_Fail::MakeBlockFailOutputBlock(
          builder, notAFunctionalInterfaceMsg, lambdaBlock);
    }

    {
      builder->SetInsertPoint(recordBlock);
      if (rightNamed->HasLambdaMethod() &&
          !RTConfig_CheckLambdaSignaturesAtCast) {
        BasicBlock *lambdaExistsBlock =
            BasicBlock::Create(LLVMCONTEXT, "lambdaDoesExist", fun);
        auto hasLambda = RTVTable::GenerateHasRawInvoke(builder, vTableVar);
        builder->CreateIntrinsic(Intrinsic::expect, {inttype(1)},
                                 {hasLambda, MakeUInt(1, 1)});
        builder->CreateCondBr(hasLambda, lambdaExistsBlock, failBlock,
                              GetLikelyFirstBranchMetadata());
        builder->SetInsertPoint(lambdaExistsBlock);
      }
      builder->CreateBr(potentiallyCheckSignatureBlock);
    }

    {
      BasicBlock *cmpxchgFailBlock =
          BasicBlock::Create(LLVMCONTEXT, "cmpxchgfail", fun);
      builder->SetInsertPoint(doWriteCastTypeBlock);
      if (NomCastStats) {
        builder->CreateCall(GetIncImpositionsFunction(*fun->getParent()), {});
      }
      Value *castWriteType = nullptr;
      if (rightType->ContainsVariables()) {
        if (outerStack == ConstantPointerNull::get(
                              RTSubstStack::GetLLVMType()->getPointerTo())) {
          throw new std::exception();
        }
        auto uniquedType = builder->CreateCall(
            typeUniqueFun,
            {rightType->GetLLVMElement(*fun->getParent()), outerStack});
        uniquedType->setCallingConv(typeUniqueFun->getCallingConv());
        castWriteType = uniquedType;
      } else {
        castWriteType = rightType->GetLLVMElement(*fun->getParent());
      }
      // a value that has been cast before (to an unrelated type, see below)
      // gets the type added to its cast data
      BasicBlock *writeFirstCastTypeBlock =
          BasicBlock::Create(LLVMCONTEXT, "writeFirstCastType", fun);
      BasicBlock *addCastTypeBlock =
          BasicBlock::Create(LLVMCONTEXT, "addCastType", fun);
      builder->CreateCondBr(builder->CreateIsNull(currentCastData),
                            writeFirstCastTypeBlock, addCastTypeBlock,
                            GetLikelyFirstBranchMetadata());
      builder->SetInsertPoint(addCastTypeBlock);
      StructuralValueHeader::GenerateAddCastType(builder, value, castWriteType);
      builder->CreateBr(successBlock);

      builder->SetInsertPoint(writeFirstCastTypeBlock);
      auto cmpxresult =
          StructuralValueHeader::GenerateWriteCastTypePointerCMPXCHG(
              builder, value,
              builder->CreatePointerCast(castWriteType, POINTERTYPE),
              currentCastData);
      auto updateSuccess = builder->CreateExtractValue(
          cmpxresult, ArrayRef<unsigned int>(extractIndex + 1, 1));
      builder->CreateCondBr(updateSuccess, successBlock, cmpxchgFailBlock,
                            GetLikelyFirstBranchMetadata());

      builder->SetInsertPoint(cmpxchgFailBlock);
      auto lambdaCastTypeOnFirstFail = builder->CreateExtractValue(
          cmpxresult, ArrayRef<unsigned int>(extractIndex, 1));
      currentCastData->addIncoming(lambdaCastTypeOnFirstFail,
                                   builder->GetInsertBlock());
      builder->CreateBr(cmpxLoopBlock);
    }
  }

  {
    BasicBlock *singleCastBlock =
        BasicBlock::Create(LLVMCONTEXT, "checkSinglyCastSubtyping", fun);
    BasicBlock *castListBlock =
        BasicBlock::Create(LLVMCONTEXT, "checkCastListSubtyping", fun);
    // none of the cast types so far is a subtype of the right type: the value
    // is checked as for a first cast, then the type is added
    BasicBlock *unrelatedCastBlock =
        BasicBlock::Create(LLVMCONTEXT, "unrelatedCast", fun);

    builder->SetInsertPoint(notFirstCastBlock);
    Value *castTag = builder->CreateTrunc(
        builder->CreatePtrToInt(currentCastData, numtype(intptr_t)),
        inttype(1));
    builder->CreateCondBr(castTag, castListBlock, singleCastBlock,
                          GetLikelySecondBranchMetadata());

    builder->SetInsertPoint(singleCastBlock);
    RTSubtyping::CreateInlineSubtypingCheck(
        builder, builder->CreatePointerCast(currentCastData, TYPETYPE), nullptr,
        rightType, outerStack, successBlock, nullptr, unrelatedCastBlock);

    {
      BasicBlock *loopHeadBlock =
          BasicBlock::Create(LLVMCONTEXT, "castList$Head", fun);
      BasicBlock *loopBodyBlock =
          BasicBlock::Create(LLVMCONTEXT, "castList$Body", fun);
      BasicBlock *loopNextBlock =
          BasicBlock::Create(LLVMCONTEXT, "castList$Next", fun);
      builder->SetInsertPoint(castListBlock);
      auto firstNode =
          StructuralValueHeader::GenerateCastListNode(builder, currentCastData);
      builder->CreateBr(loopHeadBlock);

      builder->SetInsertPoint(loopHeadBlock);
      auto node = builder->CreatePHI(firstNode->getType(), 2, "castListNode");
      node->addIncoming(firstNode, castListBlock);
      builder->CreateCondBr(builder->CreateIsNull(node), unrelatedCastBlock,
                            loopBodyBlock);

      builder->SetInsertPoint(loopBodyBlock);
      RTSubtyping::CreateInlineSubtypingCheck(
          builder,
          builder->CreatePointerCast(
              StructuralValueHeader::GenerateReadCastListType(builder, node),
              TYPETYPE),
          nullptr, rightType, outerStack, successBlock, nullptr, loopNextBlock);

      builder->SetInsertPoint(loopNextBlock);
      node->addIncoming(
          StructuralValueHeader::GenerateReadCastListNext(builder, node),
          loopNextBlock);
      builder->CreateBr(loopHeadBlock);
    }

    builder->SetInsertPoint(unrelatedCastBlock);
    builder->CreateBr(firstCastBlock);
  }
}

void StructuralValueHeader::GenerateMonotonicStructuralCast(
    NomBuilder &builder, llvm::Function *fun, llvm::BasicBlock *successBlock,
    llvm::BasicBlock *failBlock, llvm::Value *value, llvm::Value *rightType,
    llvm::Value *rightIface, llvm::Value *rightTypeArgs,
    llvm::Value *outerStack) {
  unsigned int extractIndex[2];
  extractIndex[0] = 0;
  extractIndex[1] = 1;

  auto typeUniqueFun =
      TypeRegistry::Instance().GetLLVMElement(*fun->getParent());
  auto signatureSubtyping =
      RTSignature::Instance().GetLLVMElement(*fun->getParent());
  auto subtypingFun = RTSubtyping::Instance().GetLLVMElement(*fun->getParent());

  BasicBlock *cmpxchgFailBlock =
      BasicBlock::Create(LLVMCONTEXT, "cmpxchgfail", fun);
  BasicBlock *notFunctionalInterface = RTOutput_Fail::GenerateFailOutputBlock(
      builder, notAFunctionalInterfaceMsg);
  BasicBlock *unimplementedBlock =
      RTOutput_Fail::GenerateFailOutputBlock(builder, unimplementedMsg);

  BasicBlock *startBlock =
      BasicBlock::Create(LLVMCONTEXT, "monoCastStart", fun);
  builder->CreateBr(startBlock);

  builder->SetInsertPoint(startBlock);
  auto currentCastData =
      StructuralValueHeader::GenerateReadCastData(builder, value);
  auto hasBeenCast = builder->CreateIsNotNull(currentCastData);
  Value *lambdaCastTypeOnFirstFail = nullptr;
  PHINode *writeCastData = nullptr;

  BasicBlock *firstCastBlock =
      BasicBlock::Create(LLVMCONTEXT, "LambdaCast$firstCast", fun);
  BasicBlock *multiCastBlock =
      BasicBlock::Create(LLVMCONTEXT, "LambdaCast$multiCast", fun);

  BasicBlock *incomingBlock = builder->GetInsertBlock();
  builder->CreateCondBr(hasBeenCast, multiCastBlock, firstCastBlock);
  {

    BasicBlock *tryWriteTypeCastBlock =
        BasicBlock::Create(LLVMCONTEXT, "trycmpxchg", fun);
    BasicBlock *checkSignatureRequiredBlock =
        BasicBlock::Create(LLVMCONTEXT, "checkSignatureRequired", fun);
    BasicBlock *checkHasLambdaBlock =
        BasicBlock::Create(LLVMCONTEXT, "checkSignatureExists", fun);
    BasicBlock *checkSignatureBlock =
        RTConfig_CheckLambdaSignaturesAtCast
            ? BasicBlock::Create(LLVMCONTEXT, "checkSignature", fun)
            : tryWriteTypeCastBlock;

    builder->SetInsertPoint(firstCastBlock);
    // the cast data seen when the checks below start: null for a first cast,
    // or the cast data of a value cast before to unrelated types only
    writeCastData = builder->CreatePHI(currentCastData->getType(), 2,
                                       "castDataBeforeWrite");
    writeCastData->addIncoming(currentCastData, incomingBlock);

    BasicBlock *lambdaBlock = nullptr, *recordBlock = nullptr,
               *partialAppBlock = nullptr;
    Value *vTableVar = nullptr;

    auto interfaceSig = RTInterface::GenerateReadSignature(builder, rightIface);

    RefValueHeader::GenerateStructuralValueKindSwitch(
        builder, value, &vTableVar, &lambdaBlock, &recordBlock,
        &unimplementedBlock);

    if (lambdaBlock != nullptr) {
      builder->SetInsertPoint(lambdaBlock);
      auto interfaceFlag = RTInterface::GenerateReadFlags(builder, rightIface);
      auto flagTarget =
          MakeIntLike(interfaceFlag, (long)(RTInterfaceFlags::IsFunctional |
                                            RTInterfaceFlags::IsInterface));
      builder->CreateCondBr(
          builder->CreateICmpEQ(builder->CreateAnd(interfaceFlag, flagTarget),
                                flagTarget),
          checkSignatureBlock, notFunctionalInterface,
          GetLikelyFirstBranchMetadata());
    }

    if (recordBlock != nullptr) {
      builder->SetInsertPoint(recordBlock);
      builder->CreateBr(checkSignatureRequiredBlock);
    }

    builder->SetInsertPoint(checkSignatureRequiredBlock);
    builder->CreateCondBr(builder->CreateIsNull(interfaceSig),
                          tryWriteTypeCastBlock, checkHasLambdaBlock);

    builder->SetInsertPoint(checkHasLambdaBlock);
    auto hasLambda = RTVTable::GenerateHasRawInvoke(builder, vTableVar);
    builder->CreateIntrinsic(Intrinsic::expect, {inttype(1)},
                             {hasLambda, MakeUInt(1, 1)});
    builder->CreateCondBr(hasLambda, checkSignatureBlock, failBlock,
                          GetLikelyFirstBranchMetadata());

    if (RTConfig_CheckLambdaSignaturesAtCast) {
      // TODO: implement if needed
      // BasicBlock* pessimisticSignatureMatchBlock =
      // BasicBlock::Create(LLVMCONTEXT, "pessimisticSignatureMatch", fun);
      // BasicBlock* optimisticSignatureMatchBlock =
      // BasicBlock::Create(LLVMCONTEXT, "optimisticSignatureMatch", fun);
      // builder->SetInsertPoint(checkSignatureBlock);

      // auto sigSubst =
      // builder->CreateAlloca(RTSubtyping::TypeArgumentListStackType());
      // MakeStore(builder, outerStack, builder->CreateGEP(sigSubst, {
      // MakeInt32(0), MakeInt32(TypeArgumentListStackFields::Next) }));
      // MakeStore(builder, rightTypeArgs, builder->CreateGEP(sigSubst, {
      // MakeInt32(0), MakeInt32(TypeArgumentListStackFields::Types) }));

      // auto signatureMatch = builder->CreateCall(signatureSubtyping, {
      // structuralValueSig,
      // ConstantPointerNull::get(RTSubtyping::TypeArgumentListStackType()->getPointerTo()),
      // interfaceSig, sigSubst }, "signatureMatch");
      // signatureMatch->setCallingConv(signatureSubtyping->getCallingConv());
      // auto signatureMatchSwitch = builder->CreateSwitch(signatureMatch,
      // failBlock, 2); signatureMatchSwitch->addCase(MakeInt(2, (uint64_t)3),
      // pessimisticSignatureMatchBlock);
      // signatureMatchSwitch->addCase(MakeInt(2, (uint64_t)1),
      // optimisticSignatureMatchBlock);
    }

    builder->SetInsertPoint(tryWriteTypeCastBlock);
    auto uniquedType =
        builder->CreateCall(typeUniqueFun, {rightType, outerStack});
    uniquedType->setCallingConv(typeUniqueFun->getCallingConv());
    if (NomCastStats) {
      builder->CreateCall(GetIncImpositionsFunction(*fun->getParent()), {});
    }
    BasicBlock *writeFirstCastTypeBlock =
        BasicBlock::Create(LLVMCONTEXT, "writeFirstCastType", fun);
    BasicBlock *addCastTypeBlock =
        BasicBlock::Create(LLVMCONTEXT, "addCastType", fun);
    builder->CreateCondBr(builder->CreateIsNull(writeCastData),
                          writeFirstCastTypeBlock, addCastTypeBlock,
                          GetLikelyFirstBranchMetadata());
    builder->SetInsertPoint(addCastTypeBlock);
    StructuralValueHeader::GenerateAddCastType(builder, value, uniquedType);
    builder->CreateBr(successBlock);

    builder->SetInsertPoint(writeFirstCastTypeBlock);
    auto cmpxresult =
        StructuralValueHeader::GenerateWriteCastTypePointerCMPXCHG(
            builder, value,
            builder->CreatePointerCast(uniquedType, POINTERTYPE),
            writeCastData);
    auto updateSuccess = builder->CreateExtractValue(
        cmpxresult, ArrayRef<unsigned int>(extractIndex + 1, 1));
    builder->CreateIntrinsic(Intrinsic::expect, {inttype(1)},
                             {updateSuccess, MakeUInt(1, 1)});
    builder->CreateCondBr(updateSuccess, successBlock, cmpxchgFailBlock,
                          GetLikelyFirstBranchMetadata());

    builder->SetInsertPoint(cmpxchgFailBlock);
    lambdaCastTypeOnFirstFail = builder->CreateExtractValue(
        cmpxresult, ArrayRef<unsigned int>(extractIndex, 1));
    builder->CreateBr(multiCastBlock);
  }

  {
    builder->SetInsertPoint(multiCastBlock);
    auto lambdaCastTypePHI = builder->CreatePHI(currentCastData->getType(), 2);
    lambdaCastTypePHI->addIncoming(currentCastData, incomingBlock);
    lambdaCastTypePHI->addIncoming(lambdaCastTypeOnFirstFail, cmpxchgFailBlock);

    BasicBlock *singleCastEntryBlock =
        BasicBlock::Create(LLVMCONTEXT, "singleCastEntry", fun);
    BasicBlock *multiCastListBlock =
        BasicBlock::Create(LLVMCONTEXT, "multiCastList", fun);

    auto lambdaCastTag = builder->CreateTrunc(
        builder->CreatePtrToInt(lambdaCastTypePHI, numtype(intptr_t)),
        inttype(1));
    builder->CreateIntrinsic(Intrinsic::expect, {inttype(1)},
                             {lambdaCastTag, MakeUInt(1, 0)});
    builder->CreateCondBr(lambdaCastTag, multiCastListBlock,
                          singleCastEntryBlock,
                          GetLikelySecondBranchMetadata());

    // none of the cast types so far is a subtype of the right type: the value
    // is checked as for a first cast, then the type is added
    BasicBlock *unrelatedCastBlock =
        BasicBlock::Create(LLVMCONTEXT, "unrelatedCast", fun);
    auto generateSubtypingCheck = [&](Value *leftType,
                                      BasicBlock *notSubtypeBlock) {
      auto subtypingCall = builder->CreateCall(
          subtypingFun,
          {builder->CreatePointerCast(leftType, TYPETYPE), rightType,
           ConstantPointerNull::get(
               RTSubtyping::TypeArgumentListStackType()->getPointerTo()),
           outerStack});
      subtypingCall->setCallingConv(subtypingFun->getCallingConv());
      auto subtypingSuccess =
          builder->CreateICmpEQ(subtypingCall, MakeIntLike(subtypingCall, 3));
      builder->CreateIntrinsic(Intrinsic::expect, {inttype(1)},
                               {subtypingSuccess, MakeUInt(1, 1)});
      builder->CreateCondBr(subtypingSuccess, successBlock, notSubtypeBlock,
                            GetLikelyFirstBranchMetadata());
    };

    builder->SetInsertPoint(singleCastEntryBlock);
    auto cleanLeftType = builder->CreateIntToPtr(
        builder->CreateAnd(
            builder->CreatePtrToInt(lambdaCastTypePHI, numtype(intptr_t)),
            ConstantExpr::getXor(
                llvm::ConstantInt::getAllOnesValue(numtype(intptr_t)),
                MakeInt<intptr_t>(7))),
        TYPETYPE);
    generateSubtypingCheck(cleanLeftType, unrelatedCastBlock);

    {
      BasicBlock *loopHeadBlock =
          BasicBlock::Create(LLVMCONTEXT, "castList$Head", fun);
      BasicBlock *loopBodyBlock =
          BasicBlock::Create(LLVMCONTEXT, "castList$Body", fun);
      BasicBlock *loopNextBlock =
          BasicBlock::Create(LLVMCONTEXT, "castList$Next", fun);
      builder->SetInsertPoint(multiCastListBlock);
      auto firstNode = StructuralValueHeader::GenerateCastListNode(
          builder, lambdaCastTypePHI);
      builder->CreateBr(loopHeadBlock);

      builder->SetInsertPoint(loopHeadBlock);
      auto node = builder->CreatePHI(firstNode->getType(), 2, "castListNode");
      node->addIncoming(firstNode, multiCastListBlock);
      builder->CreateCondBr(builder->CreateIsNull(node), unrelatedCastBlock,
                            loopBodyBlock);

      builder->SetInsertPoint(loopBodyBlock);
      generateSubtypingCheck(
          StructuralValueHeader::GenerateReadCastListType(builder, node),
          loopNextBlock);

      builder->SetInsertPoint(loopNextBlock);
      node->addIncoming(
          StructuralValueHeader::GenerateReadCastListNext(builder, node),
          loopNextBlock);
      builder->CreateBr(loopHeadBlock);
    }

    builder->SetInsertPoint(unrelatedCastBlock);
    writeCastData->addIncoming(lambdaCastTypePHI, unrelatedCastBlock);
    builder->CreateBr(firstCastBlock);
  }
}
} // namespace Runtime
} // namespace Nom