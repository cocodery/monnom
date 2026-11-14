#include "NomIMTUtils.h"
#include "CallingConvConf.h"
#include "CompileEnv.h"
#include "CompileHelpers.h"
#include "IMT.h"
#include "InstanceDictionary.h"
#include "Metadata.h"
#include "NomBuilder.h"
#include "NomField.h"
#include "NomIMTResultCast.h"
#include "NomJIT.h"
#include "NomLambdaCallTag.h"
#include "NomMethod.h"
#include "NomNameRepository.h"
#include "NomRecordMethod.h"
#include "NomVMInterface.h"
#include "ObjectHeader.h"
#include "RTCast.h"
#include "RTCompileConfig.h"
#include "RTOutput.h"
#include "RTVTable.h"
#include "RecordHeader.h"
#include "RefValueHeader.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
#include <memory>
#include <string>
#include <unordered_map>

using namespace llvm;
namespace Nom {
namespace Runtime {
namespace IMTUtils {

// IMT entry functions and their modules share this name prefix
const char IMTEntryPrefix[] = "MONNOM_RT_RECORDIMT_";

using DenseMapPair = std::pair<const NomRecord *, const NomInterfaceCallTag *>;
static DenseMap<DenseMapPair, CallTagTarget> &CallTagTargetCache() {
  static DenseMap<DenseMapPair, CallTagTarget> cache;
  return cache;
}

const CallTagTarget &GetCallTagTarget(const NomRecord *nomRecord,
                                      const NomInterfaceCallTag *callTag) {
  auto &cache = CallTagTargetCache();
  auto iter = cache.find({nomRecord, callTag});
  if (iter == cache.end()) {
    std::cerr << "No IMT target recorded for call-tag " << callTag->GetKey()
              << std::endl;
    throw new std::exception();
  }
  return iter->second;
}

static CallTagTarget MakeCallTagTarget(const NomRecord *nomRecord,
                                       const NomInterfaceCallTag *callTag,
                                       uint64_t kind, uint64_t addr) {
  switch (static_cast<DispatchTargetKind>(kind)) {
  case DispatchTargetKind::Method: {
    auto prefix = reinterpret_cast<const RecordDDTEPrefix *>(addr) - 1;
    auto meth = const_cast<NomRecordMethod *>(prefix->method);
    // assert(meth->Container == nomRecord);
    auto ifaceMethod = callTag->GetMethod();
    if (meth->GetDirectTypeParametersCount() !=
            ifaceMethod->GetDirectTypeParametersCount() ||
        meth->GetArgumentCount() != ifaceMethod->GetArgumentCount()) {
      std::cerr << "Called record method " << meth->GetName()
                << " with invalid number of arguments" << std::endl;
      throw new std::exception();
    }
    return {DispatchTargetKind::Method, meth, nullptr, prefix->addr};
  }
  case DispatchTargetKind::Field: {
    auto methodName = callTag->GetMethod()->GetName();
    for (auto *field : nomRecord->Fields) {
      if (field->GetName()->ToStdString() == methodName) {
        return {DispatchTargetKind::Field, nullptr, field, nullptr};
      }
    }
    std::cerr << "dispatch found no declared field of this name" << std::endl;
    throw new std::exception();
  }
  case DispatchTargetKind::DictField:
    return {DispatchTargetKind::DictField, nullptr, nullptr, nullptr};
  }
  std::cerr << "Invalid IMT target kind " << kind << " for call-tag "
            << callTag->GetKey() << std::endl;
  throw new std::exception();
}

void SetCallTagTarget(const NomRecord *nomRecord,
                      const NomInterfaceCallTag *callTag, uint64_t kind,
                      uint64_t addr) {
  auto &cache = CallTagTargetCache();
  if (cache.count({nomRecord, callTag}) != 0) {
    return; // already set
  }
  cache.try_emplace({nomRecord, callTag},
                    MakeCallTagTarget(nomRecord, callTag, kind, addr));
}

// Reads the IMT entry argument at position `pos` (0 is the receiver)
// The first RTConfig_NumberOfVarargsArguments positions are passed in registers
// if there are more, the last register points to an array holding the rest
static Value *ReadIMTArgument(NomBuilder &builder, Value **varargs,
                              unsigned int pos, unsigned int paramCount) {
  const unsigned int regCount = RTConfig_NumberOfVarargsArguments;
  if (pos < regCount || regCount + 1 == paramCount) {
    return varargs[pos];
  }
  return MakeLoad(
      builder,
      builder->CreateGEP(builder->CreatePointerCast(
                             varargs[regCount], POINTERTYPE->getPointerTo()),
                         MakeInt32(pos - regCount)),
      "varArg", AtomicOrdering::NotAtomic);
}
static Value *GenerateMethodCall(NomBuilder &builder, Function *fun,
                                 NomRecord *nomRecord, Value **varargs,
                                 const NomInterfaceCallTag *callTag,
                                 const CallTagTarget &target) {
  auto meth = target.method;
  // same as how record IMT entry build
  auto implFunctionType = meth->GetLLVMFunctionType();
  auto paramCount = implFunctionType->getNumParams();
  auto argsarr = makealloca(Value *, paramCount);

  auto methodPtr = builder->CreateIntToPtr(
      ConstantInt::get(numtype(intptr_t),
                       reinterpret_cast<intptr_t>(target.methodAddr)),
      implFunctionType->getPointerTo());
  // although the method is still called under the record signature discipline
  // the argument types need to read from it
  auto ifaceFunctionType = callTag->GetMethod()->GetLLVMFunctionType();
  auto targCount = meth->GetDirectTypeParametersCount();

  NomSubstitutionContextMemberContext nscmc(meth);
  CastedValueCompileEnv cvce = CastedValueCompileEnv(
      meth->GetDirectTypeParameters(), nomRecord->GetAllTypeParameters(), fun,
      2, meth->GetArgumentCount(),
      ObjectHeader::GeneratePointerToTypeArguments(builder, varargs[0]));

  for (decltype(paramCount) j = 0; j < paramCount; j++) {
    Value *curArg = ReadIMTArgument(builder, varargs, j, paramCount);
    // Interface call sites pass primitive arguments unpacked
    // pack them as NomInterfaceCallTag would before the record method sees them
    if (j < ifaceFunctionType->getNumParams()) {
      auto ifaceType = ifaceFunctionType->getParamType(j);
      if (ifaceType != REFTYPE && ifaceType != TYPETYPE) {
        curArg = EnsurePackedUnpacked(builder, curArg, ifaceType);
        curArg = EnsurePackedUnpacked(builder, curArg, REFTYPE);
      }
    }
    // [0] receiver, [1..targCount] the method's type arguments, then values
    if (j == 0 || j > targCount) {
      curArg = EnsurePackedUnpacked(builder, curArg, REFTYPE);
      if (j > targCount) {
        curArg = RTCast::GenerateCast(
            builder, &cvce, curArg,
            meth->GetArgumentTypes(&nscmc)[j - (targCount + 1)]);
      }
    }
    argsarr[j] = EnsurePackedUnpacked(builder, curArg,
                                      implFunctionType->getParamType(j));
  }

  auto callResult = builder->CreateCall(implFunctionType, methodPtr,
                                        ArrayRef<Value *>(argsarr, paramCount),
                                        meth->GetQName());
  callResult->setCallingConv(NOMCC);
  return EnsurePackedUnpacked(builder, callResult, REFTYPE);
}

static Value *GenerateLambdaFieldCall(NomBuilder &builder, Function *fun,
                                      NomRecord *nomRecord, Value **varargs,
                                      const NomInterfaceCallTag *callTag,
                                      const CallTagTarget &target) {
  auto method = callTag->GetMethod();
  auto receiver = builder->CreatePointerCast(varargs[0], REFTYPE);

  // unreachable: an object only reaches this branch after the record
  // call-tag's dispatch found a lambda in the field and froze it. Its
  // dispatch would abort with the same message
  BasicBlock *fallbackBlock = RTOutput_Fail::GenerateFailOutputBlock(
      builder, "Tried to load invokable value from field, "
               "but value has no lambda method!");
  BasicBlock *checkBlock = BasicBlock::Create(LLVMCONTEXT, "checkLambda", fun);
  BasicBlock *isRefValueBlock =
      BasicBlock::Create(LLVMCONTEXT, "lambdaIsRefValue", fun);
  BasicBlock *callBlock = BasicBlock::Create(LLVMCONTEXT, "callLambda", fun);

  Value *fieldValue = nullptr;
  if (target.kind == DispatchTargetKind::Field) {
    auto index = target.field->Index;
    fieldValue = RecordHeader::GenerateReadField(builder, receiver, index,
                                                 nomRecord->Fields.size());
    builder->CreateBr(checkBlock);
  } else {
    BasicBlock *lookupBlock =
        BasicBlock::Create(LLVMCONTEXT, "dictLookup", fun);
    auto name = MakeUInt(
        64, NomNameRepository::Instance().GetNameID(method->GetName()));
    // reading the dictionary pointer does not create a missing dictionary
    auto dict = RecordHeader::GenerateReadStructDictionary(builder, receiver);
    builder->CreateCondBr(builder->CreateIsNull(dict), fallbackBlock,
                          lookupBlock, GetLikelySecondBranchMetadata());

    builder->SetInsertPoint(lookupBlock);
    fieldValue = InstanceDictionary::GenerateLookup(builder, dict, name);
    builder->CreateCondBr(builder->CreateIsNull(fieldValue), fallbackBlock,
                          checkBlock, GetLikelySecondBranchMetadata());
  }

  builder->SetInsertPoint(checkBlock);
  auto fieldValueAsInt = builder->CreatePtrToInt(fieldValue, INTTYPE);
  auto isRefValue = builder->CreateAnd(
      builder->CreateICmpEQ(
          builder->CreateAnd(fieldValueAsInt, MakeIntLike(fieldValueAsInt, 3)),
          MakeIntLike(fieldValueAsInt, 0)),
      builder->CreateICmpNE(fieldValueAsInt, MakeIntLike(fieldValueAsInt, 0)));
  builder->CreateCondBr(isRefValue, isRefValueBlock, fallbackBlock,
                        GetLikelyFirstBranchMetadata());

  builder->SetInsertPoint(isRefValueBlock);
  auto hasLambda = RTVTable::GenerateHasRawInvoke(
      builder, RefValueHeader::GenerateReadVTablePointer(builder, fieldValue));
  builder->CreateCondBr(hasLambda, callBlock, fallbackBlock,
                        GetLikelyFirstBranchMetadata());

  builder->SetInsertPoint(callBlock);
  auto lambdaArgs = makealloca(Value *, 2 + RTConfig_NumberOfVarargsArguments);
  for (decltype(RTConfig_NumberOfVarargsArguments) i = 0;
       i <= RTConfig_NumberOfVarargsArguments; i++) {
    lambdaArgs[i + 1] = varargs[i];
  }
  {
    auto calledType = method->GetLLVMFunctionType();
    auto paramCount = calledType->getNumParams();
    Value **packed = lambdaArgs + 1;
    for (decltype(paramCount) j = 0; j < paramCount; j++) {
      auto paramType = calledType->getParamType(j);
      if (paramType == REFTYPE || paramType == TYPETYPE) {
        continue;
      }
      bool inRegister = j < RTConfig_NumberOfVarargsArguments ||
                        RTConfig_NumberOfVarargsArguments + 1 == paramCount;
      Value *slot = nullptr;
      Value *curArg = nullptr;
      if (inRegister) {
        curArg = packed[j];
      } else {
        slot = builder->CreateGEP(
            builder->CreatePointerCast(
                packed[RTConfig_NumberOfVarargsArguments],
                POINTERTYPE->getPointerTo()),
            MakeInt32(j - RTConfig_NumberOfVarargsArguments));
        curArg = MakeLoad(builder, slot, "varArg", AtomicOrdering::NotAtomic);
      }
      curArg = EnsurePackedUnpacked(builder, curArg, paramType);
      curArg = EnsurePackedUnpacked(builder, curArg, REFTYPE);
      curArg = EnsurePackedUnpacked(builder, curArg, POINTERTYPE);
      if (inRegister) {
        packed[j] = curArg;
      } else {
        MakeStore(builder, curArg, slot, AtomicOrdering::NotAtomic);
      }
    }
  }

  Value *callee = nullptr;
  if (NomLambdaOptimizationLevel > 0) {
    callee = RefValueHeader::GenerateReadRawInvoke(builder, fieldValue);
    lambdaArgs[1] = builder->CreatePointerCast(fieldValue, POINTERTYPE);
  } else {
    auto findFun = RTVTable::GetFindInvokeDispatcherFunction(*fun->getParent());
    auto dpair = builder->CreateCall(findFun, {fieldValue});
    dpair->setCallingConv(findFun->getCallingConv());
    callee = builder->CreateExtractValue(dpair, {0});
    lambdaArgs[1] = builder->CreatePointerCast(
        builder->CreateExtractValue(dpair, {1}), POINTERTYPE);
  }
  lambdaArgs[0] =
      NomLambdaCallTag::GetCallTag(method->GetDirectTypeParametersCount(),
                                   method->GetArgumentCount())
          ->GetLLVMElement(*fun->getParent());
  auto callResult = builder->CreateCall(
      GetIMTFunctionType(),
      builder->CreatePointerCast(callee, GetIMTFunctionType()->getPointerTo()),
      ArrayRef<Value *>(lambdaArgs, 2 + RTConfig_NumberOfVarargsArguments),
      "lambda_" + method->GetName());
  callResult->setCallingConv(NOMCC);
  return EnsurePackedUnpacked(builder, callResult, REFTYPE);
}

static void
GenerateCallTagBranch(NomBuilder &builder, Function *fun, NomRecord *nomRecord,
                      Value *argCallTag, Value **varargs,
                      const NomInterfaceCallTag *callTag, void *callTagFunAddr,
                      const IMTResultCast::ReceiverTypeArgs &recvTypeArgs) {
  auto target = GetCallTagTarget(nomRecord, callTag);

  BasicBlock *callBlock =
      BasicBlock::Create(LLVMCONTEXT, callTag->GetMethod()->GetName(), fun);
  BasicBlock *nextBlock = BasicBlock::Create(LLVMCONTEXT, "next", fun);

  auto lhs = builder->CreatePtrToInt(argCallTag, numtype(intptr_t));
  auto rhs = ConstantInt::get(numtype(intptr_t),
                              reinterpret_cast<intptr_t>(callTagFunAddr));
  auto callTagMatch = builder->CreateICmpEQ(lhs, rhs, "callTagMatch");
  builder->CreateIntrinsic(Intrinsic::expect, {inttype(1)},
                           {callTagMatch, MakeUInt(1, 1)});
  builder->CreateCondBr(callTagMatch, callBlock, nextBlock,
                        GetLikelyFirstBranchMetadata());
  builder->SetInsertPoint(callBlock);

  Value *actualResult = nullptr;
  if (target.kind == DispatchTargetKind::Method) {
    actualResult =
        GenerateMethodCall(builder, fun, nomRecord, varargs, callTag, target);
  } else {
    actualResult = GenerateLambdaFieldCall(builder, fun, nomRecord, varargs,
                                           callTag, target);
  }

  // Return in the interface method's representation as NomInterfaceCallTag does
  auto ifaceFunctionType = callTag->GetMethod()->GetLLVMFunctionType();
  actualResult = IMTResultCast::GenerateBranchResultCast(
      builder, fun, target, callTag, argCallTag, varargs, recvTypeArgs,
      actualResult);
  actualResult = EnsurePackedUnpacked(builder, actualResult,
                                      ifaceFunctionType->getReturnType());
  builder->CreateRet(EnsurePackedUnpacked(builder, actualResult, POINTERTYPE));

  builder->SetInsertPoint(nextBlock);
}

std::unique_ptr<Module>
BuildIMTEntryModule(NomRecord *nomRecord, int imtIndex,
                    const NomInterfaceCallTag *callTag, void *callTagFunAddr,
                    const IMTCallTagSet &parentCallTags,
                    const IMTResultCast::ReceiverTypeArgs &recvTypeArgs) {
  static std::atomic<int> transitionCount{0};

  std::string name = IMTEntryPrefix + *nomRecord->GetSymbolName() + "_" +
                     std::to_string(imtIndex) + "_" +
                     std::to_string(transitionCount++);

  auto &jit = NomJIT::Instance();
  auto mod = std::make_unique<Module>(name, LLVMCONTEXT);
  mod->setDataLayout(jit.getDataLayout());

  Function *fun = Function::Create(GetIMTFunctionType(),
                                   Function::ExternalLinkage, name, mod.get());
  fun->setCallingConv(NOMCC);

  BasicBlock *startBlock = BasicBlock::Create(LLVMCONTEXT, "", fun);
  NomBuilder builder;
  builder->SetInsertPoint(startBlock);

  // builder->CreateCall(
  //     GetPrint(mod.get()),
  //     {ConstantInt::get(
  //          INTTYPE,
  //          reinterpret_cast<uint64_t>(new std::string(
  //              "Enter IMT slot " + std::to_string(imtIndex) + " transition "
  //              + std::to_string(transitionCount) + "\n")),
  //          false),
  //      llvm::ConstantInt::get(INTTYPE, 1, false)});

  auto argiter = fun->arg_begin();
  auto argarr = makealloca(Value *, 2 + RTConfig_NumberOfVarargsArguments);
  argarr[0] = argiter;

  Value *argCallTag = argiter;
  argiter++;
  auto varargs = makealloca(Value *, RTConfig_NumberOfVarargsArguments + 1);
  for (decltype(RTConfig_NumberOfVarargsArguments) i = 0;
       i <= RTConfig_NumberOfVarargsArguments; i++) {
    varargs[i] = argiter;
    argarr[i + 1] = argiter;
    argiter++;
  }

  // The call-tag being added
  GenerateCallTagBranch(builder, fun, nomRecord, argCallTag, varargs, callTag,
                        callTagFunAddr, recvTypeArgs);
  // Call-tags the parent node already handles
  for (auto &[parentTag, parentTagFunAddr] : parentCallTags) {
    GenerateCallTagBranch(builder, fun, nomRecord, argCallTag, varargs,
                          parentTag, parentTagFunAddr, recvTypeArgs);
  }

  // No call-tag matched call the call-tag itself
  auto tagCall = builder->CreateCall(
      GetIMTFunctionType(),
      builder->CreatePointerCast(argCallTag,
                                 GetIMTFunctionType()->getPointerTo()),
      ArrayRef<Value *>(argarr, 2 + RTConfig_NumberOfVarargsArguments));
  tagCall->setCallingConv(NOMCC);
  tagCall->setTailCallKind(CallInst::TailCallKind::TCK_MustTail);
  builder->CreateRet(tagCall);

  raw_os_ostream out(std::cout);
  if (verifyFunction(*fun, &out)) {
    out.flush();
    std::cout << "Could not verify IMT entry function " << name;
    fun->print(out);
    out.flush();
    std::cout.flush();
    throw new std::exception();
  }
  return mod;
}

bool IsIMTEntryModule(const Module &mod) {
  return mod.getModuleIdentifier().rfind(IMTEntryPrefix, 0) == 0;
}

void OptimizeIMTEntryModule(Module &mod) {
  legacy::PassManager mpm;
  mpm.add(createEliminateAvailableExternallyPass());
  mpm.add(createGlobalDCEPass());
  mpm.run(mod);

  legacy::FunctionPassManager fpm(&mod);
  fpm.add(createEarlyCSEPass());
  fpm.add(createInstructionCombiningPass());
  fpm.add(createJumpThreadingPass());
  fpm.add(createTailCallEliminationPass());
  fpm.doInitialization();
  for (auto &fun : mod) {
    fpm.run(fun);
  }
  fpm.doFinalization();
}

void *
CompileIMTEntryFunction(NomRecord *nomRecord, int imtIndex,
                        const NomInterfaceCallTag *callTag,
                        void *callTagFunAddr, NomIMTNode *imtNode,
                        const IMTResultCast::ReceiverTypeArgs &recvTypeArgs) {
  auto &jit = NomJIT::Instance();
  std::unique_ptr<Module> mod;
  {
    auto lock = jit.getContextLock();
    mod = BuildIMTEntryModule(nomRecord, imtIndex, callTag, callTagFunAddr,
                              imtNode->GetCallTags(), recvTypeArgs);
  }
  std::string name = mod->getModuleIdentifier();

  if (auto err = jit.addModule(std::move(mod))) {
    logAllUnhandledErrors(std::move(err), errs(),
                          "Could not add IMT entry " + name + ": ");
    throw new std::exception();
  }
  auto sym = jit.lookup(name);
  if (!sym) {
    logAllUnhandledErrors(sym.takeError(), errs(),
                          "Could not find IMT entry " + name + ": ");
    throw new std::exception();
  }
  return reinterpret_cast<void *>(sym->getAddress());
}
} // namespace IMTUtils
} // namespace Runtime
} // namespace Nom
