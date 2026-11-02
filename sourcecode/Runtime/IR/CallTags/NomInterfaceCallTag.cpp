#include "NomInterfaceCallTag.h"
#include "CallingConvConf.h"
#include "CastStats.h"
#include "CompileHelpers.h"
#include "IMT.h"
#include "Metadata.h"
#include "NomClassType.h"
#include "NomInterface.h"
#include "NomLambdaCallTag.h"
#include "NomMethod.h"
#include "NomNameRepository.h"
#include "NomPartialApplication.h"
#include "NomRecord.h"
#include "NomRecordCallTag.h"
#include "RTCast.h"
#include "RTClassType.h"
#include "RTCompileConfig.h"
#include "RTConfig.h"
#include "RTInterface.h"
#include "RTOutput.h"
#include "RTVTable.h"
#include "RefValueHeader.h"
#include "StructuralValueHeader.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_os_ostream.h"
#include <cstdint>
#include <iostream>
#include <unordered_map>

using namespace std;
using namespace llvm;
namespace Nom {
namespace Runtime {
NomInterfaceCallTag::NomInterfaceCallTag(
    std::string &&key, const NomMethod *method, DICTKEYTYPE &&name,
    llvm::SmallVector<TypeReferenceType, 8> &&argTRTs)
    : method(method), name(name) {
  this->key = key;
  this->argTRTs = argTRTs;
}
NomInterfaceCallTag *NomInterfaceCallTag::GetCallTag(const NomMethod *method) {
  static unordered_map<string, NomInterfaceCallTag *> methodKeys;

  std::string key = *method->GetSymbolName();
  key.append("/");
  key.append(std::to_string(method->GetDirectTypeParametersCount()));
  key.append("/");
  TypeList args = method->GetArgumentTypes(nullptr);
  llvm::SmallVector<TypeReferenceType, 8> argTRTs;
  for (auto arg : args) {
    key.append(arg->GetSymbolRep());
    argTRTs.push_back(arg->GetTypeReferenceType());
  }
  key.append(method->GetReturnType(nullptr)->GetSymbolRep());

  auto foundElem = methodKeys.find(key);
  if (foundElem != methodKeys.end()) {
    return (foundElem->second);
  } else {
    NomInterfaceCallTag *nmk = new NomInterfaceCallTag(
        std::move(key), method,
        std::move(NomNameRepository::Instance().GetNameID(method->GetName())),
        std::move(argTRTs));
    methodKeys[nmk->key] = nmk;
    return nmk;
  }
}
llvm::Constant *NomInterfaceCallTag::createLLVMElement(
    llvm::Module &mod, llvm::GlobalValue::LinkageTypes linkage) const {
  Function *fun = Function::Create(GetIMTCastFunctionType(), linkage,
                                   "MONNOM_RT_ICT_" + key, mod);
  fun->setPrefixData(
      llvm::ConstantInt::get(INTTYPE, reinterpret_cast<int64_t>(this), false));
  fun->setCallingConv(NOMCC);
  BasicBlock *block = BasicBlock::Create(LLVMCONTEXT, "", fun);

  auto returnType = method->GetReturnType(nullptr);
  auto targcount = method->GetDirectTypeParametersCount();
  auto argcount = targcount + argTRTs.size();
  Value **argbuf = makealloca(Value *, 3 + RTConfig_NumberOfVarargsArguments);
  NomBuilder builder;
  builder->SetInsertPoint(block);

  //   builder->CreateCall(
  //       GetPrint(&mod),
  //       {ConstantInt::get(INTTYPE,
  //                         reinterpret_cast<uint64_t>(new std::string(
  //                             "Call interface call tag: " + key + "\n")),
  //                         false),
  //        llvm::ConstantInt::get(INTTYPE, 1, false)});

  argbuf++;

  auto fargs = fun->arg_begin();
  argbuf[0] = fargs;
  fargs++;           // ignore first argument, which is just this function
  argbuf[1] = fargs; // receiver; we know this is not a primitive value, as such
                     // a value would never call the method key
  fargs++;
  for (decltype(RTConfig_NumberOfVarargsArguments) i = 0;
       i < RTConfig_NumberOfVarargsArguments; i++) {
    argbuf[i + 2] = fargs;
    fargs++;
  }

  // the old version was **Value **varargs = argbuf + 2**
  // arbuf   [0]       [1]    [2] [3]
  //      call-tag  receiver arg1 arg2
  // param = receiver + type-param + param
  // each time the old version packs a unused argument and store it to varargs
  // this bug will trigger when call through an interface on record or lambda
  // and the interface method signature should type first argument as Integers
  // dyn or generic T are all REFTYPE
  Value **varargs = argbuf + 1;

  auto calledFunctionType = method->GetLLVMFunctionType();
  auto paramCount = calledFunctionType->getNumParams();

  for (decltype(paramCount) j = 0; j < paramCount; j++) {
    Value *curArg = nullptr;
    auto calledType = calledFunctionType->getParamType(j);
    if (calledType != REFTYPE && calledType != TYPETYPE) {
      if (j < RTConfig_NumberOfVarargsArguments ||
          RTConfig_NumberOfVarargsArguments + 1 == paramCount) {
        curArg = varargs[j];
      } else {
        curArg = MakeLoad(builder,
                          builder->CreateGEP(
                              builder->CreatePointerCast(
                                  varargs[RTConfig_NumberOfVarargsArguments],
                                  POINTERTYPE->getPointerTo()),
                              MakeInt32(j - RTConfig_NumberOfVarargsArguments)),
                          "varArg", AtomicOrdering::NotAtomic);
      }

      // curArg = EnsurePackedUnpacked(builder, curArg, REFTYPE);
      curArg = EnsurePackedUnpacked(builder, curArg, calledType);
      curArg = EnsurePackedUnpacked(builder, curArg, REFTYPE);
      curArg = EnsurePackedUnpacked(builder, curArg, POINTERTYPE);
      if (j < RTConfig_NumberOfVarargsArguments ||
          RTConfig_NumberOfVarargsArguments + 1 == paramCount) {
        varargs[j] = curArg;
      } else {
        MakeStore(builder, curArg,
                  builder->CreateGEP(
                      builder->CreatePointerCast(
                          varargs[RTConfig_NumberOfVarargsArguments],
                          POINTERTYPE->getPointerTo()),
                      MakeInt32(j - RTConfig_NumberOfVarargsArguments)),
                  AtomicOrdering::NotAtomic);
      }
    }
  }
  Value *actualResult = nullptr;
  if (method->GetName().empty() && NomLambdaOptimizationLevel > 0) {
    // builder->CreateCall(
    //     GetPrint(&mod),
    //     {ConstantInt::get(INTTYPE,
    //                       reinterpret_cast<uint64_t>(new std::string(
    //                           "Call Lambda Call-Tag in Interface Call \
    //                           Tag\n")),
    //                       false),
    //      llvm::ConstantInt::get(INTTYPE, 1, false)});
    argbuf[0] = builder->CreatePointerCast(
        NomLambdaCallTag::GetCallTag(targcount, argTRTs.size())
            ->GetLLVMElement(mod),
        POINTERTYPE);
    auto callResult = builder->CreateCall(
        GetIMTFunctionType(),
        builder->CreatePointerCast(fun->arg_begin(),
                                   GetIMTFunctionType()->getPointerTo()),
        ArrayRef<Value *>(argbuf, 2 + RTConfig_NumberOfVarargsArguments),
        method->GetQName());
    callResult->setCallingConv(NOMCC);
    actualResult = EnsurePackedUnpacked(builder, callResult, REFTYPE);
  } else {
    // builder->CreateCall(
    //     GetPrint(&mod),
    //     {ConstantInt::get(INTTYPE,
    //                       reinterpret_cast<uint64_t>(
    //                           new std::string("Call RCT in ICT\n")),
    //                       false),
    //      llvm::ConstantInt::get(INTTYPE, 1, false)});

    auto callTag = builder->CreatePointerCast(
        NomRecordCallTag::GetCallTag(this->method->GetName(), targcount,
                                     argTRTs.size())
            ->GetLLVMElement(mod),
        POINTERTYPE);

    // std::cout << NomRecordCallTag::GetCallTag(this->method->GetName(),
    //                                           targcount, argTRTs.size())
    //           << " | " << callTag << " " <<
    //           NomRecordCallTag::GetCallTag(this->method->GetName(),
    //           targcount,
    //                                  argTRTs.size())
    //         ->GetLLVMElement(mod) << std::endl;

    // argbuf[0] = callTag;
    // auto receiver = builder->CreatePointerCast(argbuf[1], REFTYPE);
    // auto vtable = RefValueHeader::GenerateReadVTablePointer(builder,
    // receiver); auto callee = RTVTable::GenerateReadInterfaceMethodTableEntry(
    //     builder, vtable, MakeInt32(name % IMTsize));

    auto callee = builder->CreatePointerCast(
        callTag, GetIMTCastFunctionType()->getPointerTo());

    auto callResult = builder->CreateCall(
        GetIMTFunctionType(), callee,
        ArrayRef<Value *>(argbuf, 2 + RTConfig_NumberOfVarargsArguments),
        method->GetQName());

    callResult->setCallingConv(NOMCC);
    actualResult = EnsurePackedUnpacked(builder, callResult, REFTYPE);
  }
  llvm::Value *retval = nullptr;
  if (ResultNeedsCast(method)) {
    // IMT entries call the same function, so the cast is compiled once
    auto castArgs = makealloca(Value *, 3 + RTConfig_NumberOfVarargsArguments);
    castArgs[0] = builder->CreatePointerCast(fun, POINTERTYPE);
    for (decltype(RTConfig_NumberOfVarargsArguments) i = 0;
         i <= RTConfig_NumberOfVarargsArguments; i++) {
      castArgs[i + 1] = varargs[i];
    }
    castArgs[2 + RTConfig_NumberOfVarargsArguments] = actualResult;
    auto castCall = builder->CreateCall(
        GetResultCastFunctionType(), GetResultCastFunction(mod, linkage),
        ArrayRef<Value *>(castArgs, 3 + RTConfig_NumberOfVarargsArguments));
    castCall->setCallingConv(NOMCC);
    actualResult = castCall;
  }
  retval = EnsurePackedUnpacked(
      builder,
      EnsurePackedUnpacked(builder, actualResult,
                           calledFunctionType->getReturnType()),
      POINTERTYPE);
  builder->CreateRet(retval);

  llvm::raw_os_ostream out(std::cout);

  if (verifyFunction(*fun, &out)) {
    out.flush();
    std::cout << "Could not verify method key method!";
    fun->print(out);
    out.flush();
    std::cout.flush();
    throw name;
  }
  return fun;
}
llvm::Constant *NomInterfaceCallTag::findLLVMElement(llvm::Module &mod) const {
  return mod.getFunction("MONNOM_RT_ICT_" + key);
}
bool NomInterfaceCallTag::ResultNeedsCast(const NomMethod *method) {
  bool simpleReturnType = false;
  auto actualReturnType = method->GetReturnType();
  if (actualReturnType->GetKind() == TypeKind::TKClass) {
    auto classReturnType = (NomClassTypeRef)actualReturnType;
    if (!classReturnType->Named->IsInterface() &&
        !classReturnType->ContainsVariables()) {
      simpleReturnType = true;
    }
  }
  if (actualReturnType->GetKind() == TypeKind::TKVariable) {
    simpleReturnType = true;
  }
  return (!RTConfig_OmitCallTagCasts) || simpleReturnType;
}
std::string NomInterfaceCallTag::GetResultCastFunctionName() const {
  return "MONNOM_RT_ICT_CAST_" + key;
}
llvm::FunctionType *NomInterfaceCallTag::GetResultCastFunctionType() {
  static FunctionType *ft = nullptr;
  if (ft == nullptr) {
    auto imtParams = GetIMTFunctionType()->params();
    SmallVector<Type *, 8> params(imtParams.begin(), imtParams.end());
    params.push_back(REFTYPE);
    ft = FunctionType::get(REFTYPE, params, false);
  }
  return ft;
}
llvm::Function *NomInterfaceCallTag::GetResultCastFunction(
    llvm::Module &mod, llvm::GlobalValue::LinkageTypes linkage) const {
  auto name = GetResultCastFunctionName();
  if (auto fun = mod.getFunction(name)) {
    return fun;
  }
  Function *fun =
      Function::Create(GetResultCastFunctionType(), linkage, name, mod);
  fun->setCallingConv(NOMCC);
  NomBuilder builder;
  builder->SetInsertPoint(BasicBlock::Create(LLVMCONTEXT, "", fun));
  auto fargs = fun->arg_begin();
  Value *callTagFun = fargs++;
  Value **varargs = makealloca(Value *, 1 + RTConfig_NumberOfVarargsArguments);
  for (decltype(RTConfig_NumberOfVarargsArguments) i = 0;
       i <= RTConfig_NumberOfVarargsArguments; i++) {
    varargs[i] = fargs++;
  }
  Value *result = fargs;
  builder->CreateRet(GenerateResultCast(builder, fun, callTagFun, varargs,
                                        result, method, argTRTs.size()));
  return fun;
}
llvm::Value *NomInterfaceCallTag::GenerateResultCast(
    NomBuilder &builder, llvm::Function *fun, llvm::Value *callTagFun,
    llvm::Value **varargs, llvm::Value *actualResult, const NomMethod *method,
    size_t argCount) {
  // a structural value remember the types it cast to in its header cast data
  auto structCastType =
      StructuralValueHeader::GenerateReadCastData(builder, varargs[0]);
  auto structCastTypeAsInt =
      builder->CreatePtrToInt(structCastType, numtype(intptr_t));
  auto structCastTypeTag =
      builder->CreateTrunc(structCastTypeAsInt, inttype(1));
  builder->CreateIntrinsic(Intrinsic::expect, {inttype(1)},
                           {structCastTypeTag, MakeUInt(1, 0)});
  BasicBlock *multiCastListBlock =
      BasicBlock::Create(LLVMCONTEXT, "multiCastList", fun);
  static const char *castfail_msg = "Cast failed!";
  BasicBlock *castFailBlock =
      RTOutput_Fail::GenerateFailOutputBlock(builder, castfail_msg);
  BasicBlock *singleCastBlock =
      BasicBlock::Create(LLVMCONTEXT, "singleCastCast", fun);
  BasicBlock *outBlock = BasicBlock::Create(LLVMCONTEXT, "out", fun);
  BasicBlock *curBlock = builder->GetInsertBlock();
  builder->SetInsertPoint(outBlock);
  auto outPHI = builder->CreatePHI(actualResult->getType(), 2);
  builder->SetInsertPoint(curBlock);

  builder->CreateCondBr(structCastTypeTag, multiCastListBlock, singleCastBlock,
                        GetLikelySecondBranchMetadata());
  {
    builder->SetInsertPoint(singleCastBlock);

    BasicBlock *bestMatchBlock =
        BasicBlock::Create(LLVMCONTEXT, "castTypePerfectMatch", fun);
    BasicBlock *mismatchBlock =
        BasicBlock::Create(LLVMCONTEXT, "castTypeMismatch", fun);

    auto iface =
        RTClassType::GenerateReadClassDescriptorLink(builder, structCastType);
    auto ifaceMatch = CreatePointerEq(
        builder, iface,
        method->GetContainer()->GetLLVMElement(*fun->getParent()));
    builder->CreateIntrinsic(Intrinsic::expect, {inttype(1)},
                             {ifaceMatch, MakeUInt(1, 1)});
    builder->CreateCondBr(ifaceMatch, bestMatchBlock, mismatchBlock,
                          GetLikelyFirstBranchMetadata());

    builder->SetInsertPoint(bestMatchBlock);
    {
      if (NomCastStats) {
        builder->CreateCall(
            GetIncPerfectCallTagTypeMatchesFunction(*fun->getParent()), {});
      }
      CastedValueCompileEnv cvce = CastedValueCompileEnv(
          method->GetDirectTypeParameters(),
          method->GetParent()->GetAllTypeParameters(), fun, 2, argCount,
          builder->CreatePointerCast(
              builder->CreateGEP(
                  builder->CreatePointerCast(
                      structCastType,
                      RTClassType::GetLLVMType()->getPointerTo()),
                  {MakeInt32(0), MakeInt32(RTClassTypeFields::TypeArgs)}),
              TYPETYPE->getPointerTo()));
      auto castResult = RTCast::GenerateCast(builder, &cvce, actualResult,
                                             method->GetReturnType());
      // builder->CreateIntrinsic(Intrinsic::expect, { inttype(1) }, {
      // castResult, MakeUInt(1,1) }); builder->CreateCondBr(castResult,
      // outBlock, castFailBlock, GetLikelyFirstBranchMetadata());
      outPHI->addIncoming(castResult, builder->GetInsertBlock());
      builder->CreateBr(outBlock);
    }

    {
      builder->SetInsertPoint(mismatchBlock);
      if (NomCastStats) {
        builder->CreateCall(
            GetIncCallTagTypeMismatchesFunction(*fun->getParent()), {});
      }
      auto typeArgRefStore =
          builder->CreateAlloca(TYPETYPE->getPointerTo(), MakeUInt(64, 1));
      builder->CreateIntrinsic(
          Intrinsic::lifetime_start, {POINTERTYPE},
          {MakeInt<int64_t>(GetNomJITDataLayout().getTypeAllocSize(
               TYPETYPE->getPointerTo())),
           builder->CreatePointerCast(typeArgRefStore, POINTERTYPE)});
      MakeInvariantStore(
          builder,
          builder->CreatePointerCast(
              builder->CreateGEP(
                  builder->CreatePointerCast(
                      structCastType,
                      RTClassType::GetLLVMType()->getPointerTo()),
                  {MakeInt32(0), MakeInt32(RTClassTypeFields::TypeArgs)}),
              TYPETYPE->getPointerTo()),
          typeArgRefStore, AtomicOrdering::NotAtomic);
      auto invariantID = builder->CreateIntrinsic(
          Intrinsic::invariant_start, {POINTERTYPE},
          {MakeInt<int64_t>(GetNomJITDataLayout().getTypeAllocSize(
               TYPETYPE->getPointerTo())),
           builder->CreatePointerCast(typeArgRefStore, POINTERTYPE)});
      Value **checkArgs =
          makealloca(Value *, 3 + RTConfig_NumberOfVarargsArguments);
      checkArgs[0] = builder->CreatePointerCast(callTagFun, POINTERTYPE);
      checkArgs[1] = typeArgRefStore;
      checkArgs[2] = actualResult;
      for (decltype(RTConfig_NumberOfVarargsArguments) i = 0;
           i < RTConfig_NumberOfVarargsArguments; i++) {
        checkArgs[i + 3] = varargs[i + 1];
      }
      // When an untyped value is used as an interface
      // its methods return dyn, but the interface promises a type
      // after call the call-tag in interface call-tag
      // do cast the return value to the declared type
      auto rvcf =
          RTInterface::GenerateReadReturnValueCheckFunction(builder, iface);
      builder
          ->CreateCall(GetCheckReturnValueFunctionType(), rvcf,
                       llvm::ArrayRef<Value *>(
                           checkArgs, 3 + RTConfig_NumberOfVarargsArguments))
          ->setCallingConv(NOMCC);

      builder->CreateIntrinsic(
          llvm::Intrinsic::invariant_end, {POINTERTYPE},
          {invariantID,
           MakeInt<int64_t>(GetNomJITDataLayout().getTypeAllocSize(
               TYPETYPE->getPointerTo())),
           builder->CreatePointerCast(typeArgRefStore, POINTERTYPE)});
      builder->CreateIntrinsic(
          llvm::Intrinsic::lifetime_end, {POINTERTYPE},
          {MakeInt<int64_t>(GetNomJITDataLayout().getTypeAllocSize(
               TYPETYPE->getPointerTo())),
           builder->CreatePointerCast(typeArgRefStore, POINTERTYPE)});
      outPHI->addIncoming(actualResult, builder->GetInsertBlock());
      builder->CreateBr(outBlock);
    }
  }

  {
    BasicBlock *loopHeadBlock =
        BasicBlock::Create(LLVMCONTEXT, "castList$Head", fun);
    BasicBlock *loopBodyBlock =
        BasicBlock::Create(LLVMCONTEXT, "castList$Body", fun);
    builder->SetInsertPoint(multiCastListBlock);
    auto typeArgRefStore =
        builder->CreateAlloca(TYPETYPE->getPointerTo(), MakeUInt(64, 1));
    auto firstNode =
        StructuralValueHeader::GenerateCastListNode(builder, structCastType);
    builder->CreateBr(loopHeadBlock);

    builder->SetInsertPoint(loopHeadBlock);
    auto node = builder->CreatePHI(firstNode->getType(), 2, "castListNode");
    node->addIncoming(firstNode, multiCastListBlock);
    builder->CreateCondBr(builder->CreateIsNull(node), outBlock, loopBodyBlock);
    outPHI->addIncoming(actualResult, loopHeadBlock);

    builder->SetInsertPoint(loopBodyBlock);
    auto castType =
        StructuralValueHeader::GenerateReadCastListType(builder, node);
    auto iface =
        RTClassType::GenerateReadClassDescriptorLink(builder, castType);
    MakeStore(builder,
              builder->CreatePointerCast(
                  builder->CreateGEP(
                      builder->CreatePointerCast(
                          castType, RTClassType::GetLLVMType()->getPointerTo()),
                      {MakeInt32(0), MakeInt32(RTClassTypeFields::TypeArgs)}),
                  TYPETYPE->getPointerTo()),
              typeArgRefStore, AtomicOrdering::NotAtomic);

    Value **checkArgs =
        makealloca(Value *, 3 + RTConfig_NumberOfVarargsArguments);
    checkArgs[0] = builder->CreatePointerCast(callTagFun, POINTERTYPE);
    checkArgs[1] = typeArgRefStore;
    checkArgs[2] = actualResult;
    for (decltype(RTConfig_NumberOfVarargsArguments) i = 0;
         i < RTConfig_NumberOfVarargsArguments; i++) {
      checkArgs[i + 3] = varargs[i + 1];
    }
    auto rvcf =
        RTInterface::GenerateReadReturnValueCheckFunction(builder, iface);
    builder
        ->CreateCall(GetCheckReturnValueFunctionType(), rvcf,
                     llvm::ArrayRef<Value *>(
                         checkArgs, 3 + RTConfig_NumberOfVarargsArguments))
        ->setCallingConv(NOMCC);
    node->addIncoming(
        StructuralValueHeader::GenerateReadCastListNext(builder, node),
        builder->GetInsertBlock());
    builder->CreateBr(loopHeadBlock);
  }

  builder->SetInsertPoint(outBlock);
  return outPHI;
}
} // namespace Runtime
} // namespace Nom