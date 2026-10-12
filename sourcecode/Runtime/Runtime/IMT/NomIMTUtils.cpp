#include "NomIMTUtils.h"
#include "CallingConvConf.h"
#include "CompileEnv.h"
#include "CompileHelpers.h"
#include "IMT.h"
#include "Metadata.h"
#include "NomBuilder.h"
#include "NomJIT.h"
#include "NomMethod.h"
#include "NomRecordMethod.h"
#include "NomSubstitutionContext.h"
#include "ObjectHeader.h"
#include "RTCast.h"
#include "RTCompileConfig.h"
#include "TypeOperations.h"
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
#include <memory>
#include <string>
#include <unordered_map>

namespace Nom {
namespace Runtime {
namespace IMTUtils {

static NomRecordMethod *FindRecordMethod(
    const NomRecord *nomRecord,
    const NomInterfaceCallTag *callTag) {
  auto method = callTag->GetMethod();
  auto methodName = method->GetName();
  auto targCount = method->GetDirectTypeParametersCount();
  auto argCount = method->GetArgumentCount();
  for (auto *meth : nomRecord->Methods) {
    if (meth->GetName() == methodName &&
        meth->GetDirectTypeParametersCount() == targCount &&
        meth->GetArgumentCount() == argCount) {
      return meth;
    }
  }
  return nullptr;
}

void *CompileIMTEntryFunction(NomRecord *nomRecord, int imtIndex,
                              const NomInterfaceCallTag *callTag,
                              void *callTagFunAddr, NomIMTNode *imtNode) {
  static int transitionCount = 0;

  std::string name = "MONNOM_RT_RECORDIMT_" + *nomRecord->GetSymbolName() +
                     "_" + std::to_string(imtIndex) + "_" +
                     std::to_string(transitionCount++);

  auto &jit = NomJIT::Instance();
  auto mod = std::make_unique<llvm::Module>(name, LLVMCONTEXT);
  mod->setDataLayout(jit.getDataLayout());

  llvm::Function *fun = Function::Create(
      GetIMTFunctionType(), llvm::Function::ExternalLinkage, name, mod.get());
  fun->setCallingConv(NOMCC);

  BasicBlock *startBlock = BasicBlock::Create(LLVMCONTEXT, "", fun);
  NomBuilder builder;
  builder->SetInsertPoint(startBlock);

  auto argiter = fun->arg_begin();
  auto argarr = makealloca(Value *, 2 + RTConfig_NumberOfVarargsArguments);
  argarr[0] = argiter;

  auto argCallTag = argiter;
  argiter++;
  auto varargs = makealloca(Value *, RTConfig_NumberOfVarargsArguments + 1);
  for (decltype(RTConfig_NumberOfVarargsArguments) i = 0;
       i <= RTConfig_NumberOfVarargsArguments; i++) {
    varargs[i] = argiter;
    argarr[i + 1] = argiter;
    argiter++;
  }

  auto lhs = builder->CreatePtrToInt(argCallTag, numtype(intptr_t));

  // Generate compare-and-branch for a single call-tag entry
  auto generateBranch = [&](const NomInterfaceCallTag *ict,
                            void *ctFunAddr) {
    auto *meth = FindRecordMethod(nomRecord, ict);
    if (!meth)
      return;

    BasicBlock *callBlock =
        BasicBlock::Create(LLVMCONTEXT, meth->GetName(), fun);
    BasicBlock *nextBlock = BasicBlock::Create(LLVMCONTEXT, "next", fun);

    auto rhs = ConstantInt::get(
        numtype(intptr_t), reinterpret_cast<intptr_t>(ctFunAddr));
    auto match = builder->CreateICmpEQ(lhs, rhs, "callTagMatch");
    builder->CreateIntrinsic(Intrinsic::expect, {inttype(1)},
                             {match, MakeUInt(1, 1)});
    builder->CreateCondBr(match, callBlock, nextBlock,
                          GetLikelyFirstBranchMetadata());

    builder->SetInsertPoint(callBlock);

    auto methodAddr = LookupCallTagMethodAddr(nomRecord, ict);
    auto implFunctionType = meth->GetLLVMFunctionType();
    auto paramCount = implFunctionType->getNumParams();
    auto methodPtr = builder->CreateIntToPtr(
        ConstantInt::get(numtype(intptr_t),
                         reinterpret_cast<intptr_t>(methodAddr)),
        implFunctionType->getPointerTo());

    auto argsarr = makealloca(Value *, paramCount);

    NomSubstitutionContextMemberContext nscmc(meth);
    CastedValueCompileEnv cvce = CastedValueCompileEnv(
        meth->GetDirectTypeParameters(),
        nomRecord->GetAllTypeParameters(), fun, 2, paramCount,
        ObjectHeader::GeneratePointerToTypeArguments(builder, varargs[0]));

    for (decltype(paramCount) j = 0; j < paramCount; j++) {
      Value *curArg = nullptr;
      if (j < RTConfig_NumberOfVarargsArguments ||
          RTConfig_NumberOfVarargsArguments + 1 == paramCount) {
        curArg = varargs[j];
      } else {
        curArg = MakeInvariantLoad(
            builder,
            builder->CreateGEP(
                varargs[RTConfig_NumberOfVarargsArguments],
                MakeInt32(j - RTConfig_NumberOfVarargsArguments)),
            "varArg", AtomicOrdering::NotAtomic);
      }
      auto expectedType = implFunctionType->getParamType(j);
      if (j >= meth->GetDirectTypeParametersCount()) {
        curArg = EnsurePackedUnpacked(builder, curArg, REFTYPE);
        if (j > meth->GetDirectTypeParametersCount()) {
          curArg = RTCast::GenerateCast(
              builder, &cvce, curArg,
              meth->GetArgumentTypes(
                  &nscmc)[j -
                          (meth->GetDirectTypeParametersCount() + 1)]);
        }
      }
      curArg = EnsurePackedUnpacked(builder, curArg, expectedType);
      argsarr[j] = curArg;
    }

    auto callResult = builder->CreateCall(
        implFunctionType, methodPtr,
        ArrayRef<Value *>(argsarr, paramCount), meth->GetQName());
    callResult->setCallingConv(NOMCC);
    auto actualResult = EnsurePackedUnpacked(
        builder, EnsurePackedUnpacked(builder, callResult, REFTYPE),
        POINTERTYPE);
    builder->CreateRet(actualResult);

    builder->SetInsertPoint(nextBlock);
  };

  // Branches for existing call-tags in the node
  for (auto &[tag, addr] : imtNode->GetCallTags()) {
    generateBranch(tag, addr);
  }
  // Branch for the new call-tag
  generateBranch(callTag, callTagFunAddr);

  // Fallback: tail-call through argCallTag
  auto callTagFun = builder->CreatePointerCast(
      argCallTag, GetIMTCastFunctionType()->getPointerTo());
  auto tagCall = builder->CreateCall(
      GetIMTCastFunctionType(), callTagFun,
      ArrayRef<Value *>(argarr, 2 + RTConfig_NumberOfVarargsArguments));
  tagCall->setCallingConv(NOMCC);
  tagCall->setTailCallKind(CallInst::TailCallKind::TCK_MustTail);
  builder->CreateRet(tagCall);

  if (jit.addModule(std::move(mod))) {
    throw new std::exception();
  }
  auto sym = jit.lookup(name);
  if (!sym) {
    throw sym.takeError();
  }
  return reinterpret_cast<void *>(sym->getAddress());
}

void *LookupCallTagMethodAddr(const NomRecord *nomRecord,
                              const NomInterfaceCallTag *callTag) {
  static std::unordered_map<const NomRecordMethod *, void *> addrMap;

  auto *meth = FindRecordMethod(nomRecord, callTag);
  if (!meth)
    return nullptr;

  auto it = addrMap.find(meth);
  if (it != addrMap.end()) {
    return it->second;
  }

  auto &jit = NomJIT::Instance();
  auto sym = jit.lookup(*meth->GetSymbolName());
  if (!sym) {
    throw sym.takeError();
  }
  auto addr = reinterpret_cast<void *>(sym->getAddress());
  addrMap[meth] = addr;
  return addr;
}
} // namespace IMTUtils
} // namespace Runtime
} // namespace Nom
