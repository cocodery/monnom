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
#include "NomVMInterface.h"
#include "ObjectHeader.h"
#include "RTCast.h"
#include "RTCompileConfig.h"
#include "TypeOperations.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Support/raw_ostream.h"
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

static NomRecordMethod *FindRecordMethod(const NomRecord *nomRecord,
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

static void *LookupRecordMethodAddr(const NomRecordMethod *meth) {
  static std::unordered_map<const NomRecordMethod *, void *> addrMap;

  auto it = addrMap.find(meth);
  if (it != addrMap.end()) {
    return it->second;
  }

  auto &jit = NomJIT::Instance();
  auto sym = jit.lookup(*meth->GetSymbolName());
  if (!sym) {
    logAllUnhandledErrors(sym.takeError(), errs(),
                          "Could not find record method " +
                              *meth->GetSymbolName() + ": ");
    throw new std::exception();
  }
  auto addr = reinterpret_cast<void *>(sym->getAddress());
  addrMap[meth] = addr;
  return addr;
}

// Reads the IMT entry argument at position `pos` (0 is the receiver). The
// first RTConfig_NumberOfVarargsArguments positions are passed in
// registers; if there are more, the last register points to an array
// holding the rest.
static Value *ReadIMTArgument(NomBuilder &builder, Value **varargs,
                              unsigned int pos, unsigned int paramCount) {
  const unsigned int regCount = RTConfig_NumberOfVarargsArguments;
  if (pos < regCount || regCount + 1 == paramCount) {
    return varargs[pos];
  }
  return MakeInvariantLoad(
      builder, builder->CreateGEP(varargs[regCount], MakeInt32(pos - regCount)),
      "varArg", AtomicOrdering::NotAtomic);
}

// Emits one compare-and-branch for `callTag`: if the incoming call-tag is
// `callTagFunAddr`, call the record method implementing it and return.
// Leaves the builder in the fall-through block. Does nothing if the record
// has no matching method.
static void GenerateCallTagBranch(NomBuilder &builder, NomRecord *nomRecord,
                                  Function *fun, Value *argCallTag,
                                  Value **varargs,
                                  const NomInterfaceCallTag *callTag,
                                  void *callTagFunAddr) {
  NomRecordMethod *meth = FindRecordMethod(nomRecord, callTag);
  if (meth == nullptr) {
    return;
  }

  BasicBlock *callBlock = BasicBlock::Create(LLVMCONTEXT, meth->GetName(), fun);
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

  auto ifaceFunctionType = callTag->GetMethod()->GetLLVMFunctionType();
  auto implFunctionType = meth->GetLLVMFunctionType();
  auto paramCount = implFunctionType->getNumParams();
  auto targCount = meth->GetDirectTypeParametersCount();
  auto methodPtr = builder->CreateIntToPtr(
      ConstantInt::get(numtype(intptr_t), reinterpret_cast<intptr_t>(
                                              LookupRecordMethodAddr(meth))),
      implFunctionType->getPointerTo());

  NomSubstitutionContextMemberContext nscmc(meth);
  CastedValueCompileEnv cvce = CastedValueCompileEnv(
      meth->GetDirectTypeParameters(), nomRecord->GetAllTypeParameters(), fun,
      2, paramCount,
      ObjectHeader::GeneratePointerToTypeArguments(builder, varargs[0]));

  auto argsarr = makealloca(Value *, paramCount);
  for (decltype(paramCount) j = 0; j < paramCount; j++) {
    Value *curArg = ReadIMTArgument(builder, varargs, j, paramCount);
    // Interface call sites pass primitive arguments unpacked; pack them
    // as NomInterfaceCallTag would before the record method sees them.
    if (j < ifaceFunctionType->getNumParams()) {
      auto ifaceType = ifaceFunctionType->getParamType(j);
      if (ifaceType != REFTYPE && ifaceType != TYPETYPE) {
        curArg = EnsurePackedUnpacked(builder, curArg, ifaceType);
        curArg = EnsurePackedUnpacked(builder, curArg, REFTYPE);
      }
    }
    if (j >= targCount) {
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
  // Return in the interface method's representation, as
  // NomInterfaceCallTag does.
  auto refResult = EnsurePackedUnpacked(builder, callResult, REFTYPE);
  auto ifaceResult = EnsurePackedUnpacked(builder, refResult,
                                          ifaceFunctionType->getReturnType());
  builder->CreateRet(EnsurePackedUnpacked(builder, ifaceResult, POINTERTYPE));

  builder->SetInsertPoint(nextBlock);
}

std::unique_ptr<Module> BuildIMTEntryModule(NomRecord *nomRecord, int imtIndex,
                                            const NomInterfaceCallTag *callTag,
                                            void *callTagFunAddr,
                                            NomIMTNode *imtNode) {
  static int transitionCount = 0;

  std::string name = "MONNOM_RT_RECORDIMT_" + *nomRecord->GetSymbolName() +
                     "_" + std::to_string(imtIndex) + "_" +
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

  // Call-tags the parent node already handles
  for (auto &[parentTag, parentTagFunAddr] : imtNode->GetCallTags()) {
    GenerateCallTagBranch(builder, nomRecord, fun, argCallTag, varargs,
                          parentTag, parentTagFunAddr);
  }
  // The call-tag being added
  GenerateCallTagBranch(builder, nomRecord, fun, argCallTag, varargs, callTag,
                        callTagFunAddr);

  // No match: defer to the parent node's entry. It covers everything this
  // entry doesn't, down to the record's original IMT entry, which
  // tail-calls the call-tag itself.
  auto parentEntry = builder->CreateIntToPtr(
      ConstantInt::get(numtype(intptr_t),
                       reinterpret_cast<intptr_t>(imtNode->GetIMTEntry())),
      GetIMTFunctionType()->getPointerTo());
  auto parentCall = builder->CreateCall(
      GetIMTFunctionType(), parentEntry,
      ArrayRef<Value *>(argarr, 2 + RTConfig_NumberOfVarargsArguments));
  parentCall->setCallingConv(NOMCC);
  if (NOMCC == CallingConv::Fast) {
    parentCall->setTailCallKind(CallInst::TailCallKind::TCK_MustTail);
  }
  builder->CreateRet(parentCall);

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

void *CompileIMTEntryFunction(NomRecord *nomRecord, int imtIndex,
                              const NomInterfaceCallTag *callTag,
                              void *callTagFunAddr, NomIMTNode *imtNode) {
  auto mod = BuildIMTEntryModule(nomRecord, imtIndex, callTag, callTagFunAddr,
                                 imtNode);
  std::string name = mod->getModuleIdentifier();

  auto &jit = NomJIT::Instance();
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

void *LookupCallTagMethodAddr(const NomRecord *nomRecord,
                              const NomInterfaceCallTag *callTag) {
  auto *meth = FindRecordMethod(nomRecord, callTag);
  if (meth == nullptr) {
    return nullptr;
  }
  return LookupRecordMethodAddr(meth);
}
} // namespace IMTUtils
} // namespace Runtime
} // namespace Nom
