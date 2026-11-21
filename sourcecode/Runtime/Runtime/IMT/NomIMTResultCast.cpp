#include "NomIMTResultCast.h"
#include "CallingConvConf.h"
#include "CastStats.h"
#include "CompileEnv.h"
#include "CompileHelpers.h"
#include "Metadata.h"
#include "NomBuilder.h"
#include "NomIMTUtils.h"
#include "NomInterface.h"
#include "NomJIT.h"
#include "NomMethod.h"
#include "NomRecordMethod.h"
#include "NomSubstitutionContext.h"
#include "RTCast.h"
#include "RTClassType.h"
#include "RTCompileConfig.h"
#include "RTInterface.h"
#include "RTTypeHead.h"
#include "StructuralValueHeader.h"
#include "TypeOperations.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <memory>
#include <string>
#include <unordered_map>

using namespace llvm;
namespace Nom {
namespace Runtime {
namespace IMTResultCast {
using IMTUtils::CallTagTarget;
using IMTUtils::GetCallTagTarget;
using IMTUtils::IMTEntryPrefix;

namespace {
struct CastDataLayout {
  size_t castData;  // in the structural value header
  size_t classLink; // in a class type, from its head
  size_t kind;      // in a type head
  size_t nomType;   // in a type head
  size_t irPtr;     // in an RTInterface
};
} // namespace

static const CastDataLayout &GetCastDataLayout() {
  static const CastDataLayout layout = [] {
    auto &dl = GetNomJITDataLayout();
    auto svh = dl.getStructLayout(StructuralValueHeader::GetLLVMType());
    auto ct = dl.getStructLayout(RTClassType::GetLLVMType());
    auto head = dl.getStructLayout(RTTypeHead::GetLLVMType());
    auto iface = dl.getStructLayout(RTInterface::GetLLVMType());
    return CastDataLayout{
        svh->getElementOffset(
            static_cast<unsigned>(StructuralValueHeaderFields::CastData)),
        ct->getElementOffset(static_cast<unsigned>(RTClassTypeFields::Class)) -
            ct->getElementOffset(
                static_cast<unsigned>(RTClassTypeFields::Head)),
        head->getElementOffset(static_cast<unsigned>(RTTypeHeadFields::Kind)),
        head->getElementOffset(
            static_cast<unsigned>(RTTypeHeadFields::NomType)),
        iface->getElementOffset(
            static_cast<unsigned>(RTInterfaceFields::IRPtr))};
  }();
  return layout;
}

ReceiverTypeArgs ReadReceiverTypeArgs(void *receiver) {
  ReceiverTypeArgs ret;
  auto &layout = GetCastDataLayout();
  auto castData = *reinterpret_cast<char **>(
      reinterpret_cast<char *>(receiver) + layout.castData);
  if (castData == nullptr || (reinterpret_cast<uintptr_t>(castData) & 1) ||
      static_cast<TypeKind>(*(castData + layout.kind)) != TypeKind::TKClass) {
    return ret;
  }
  auto rtIface = *reinterpret_cast<char **>(castData + layout.classLink);
  auto iface = *reinterpret_cast<const NomInterface **>(rtIface + layout.irPtr);
  if (iface == nullptr) {
    return ret;
  }
  auto count = iface->GetTypeParametersCount();
  if (count == 0) {
    return ret;
  }
  auto args = reinterpret_cast<char **>(castData);
  for (size_t k = 0; k < count; k++) {
    auto argHead = args[-static_cast<ptrdiff_t>(k + 1)];
    auto nomType =
        *reinterpret_cast<const NomType **>(argHead + layout.nomType);
    if (nomType == nullptr || nomType->ContainsVariables()) {
      return ReceiverTypeArgs();
    }
    ret.nomArgs.push_back(nomType);
    ret.rtArgs.push_back(argHead);
  }
  ret.iface = iface;
  ret.rtIface = rtIface;
  return ret;
}

static NomTypeRef ClosedResultCastType(const CallTagTarget &target,
                                       const NomInterfaceCallTag *callTag) {
  auto ifaceMethod = callTag->GetMethod();
  auto returnType = ifaceMethod->GetReturnType();
  if (returnType->ContainsVariables() || !ifaceMethod->ResultNeedsCast()) {
    return nullptr;
  }
  if (target.kind == DispatchTargetKind::Method) {
    NomSubstitutionContextMemberContext nscmc(target.method);
    if (target.method->GetReturnType(&nscmc)->IsSubtype(returnType)) {
      return nullptr;
    }
  }
  return returnType;
}

static FunctionType *GetResultCastFunctionType() {
  static auto ft = FunctionType::get(REFTYPE, {REFTYPE}, false);
  return ft;
}

static void *LookupRetValCastforT(NomTypeRef type) {
  static std::unordered_map<std::string, void *> casts;
  auto [iter, inserted] = casts.try_emplace(type->GetSymbolRep(), nullptr);
  if (!inserted) {
    return iter->second;
  }
  auto &jit = NomJIT::Instance();
  // the IMT entry prefix makes NomJIT optimize it like an entry
  std::string name =
      IMTEntryPrefix + std::string("CAST_") + std::to_string(casts.size());
  std::unique_ptr<Module> mod;
  {
    // held until the module is handed to the JIT
    auto lock = jit.getContextLock();
    mod = std::make_unique<Module>(name, LLVMCONTEXT);
    mod->setDataLayout(jit.getDataLayout());
    Function *fun =
        Function::Create(GetResultCastFunctionType(), Function::ExternalLinkage,
                         name, mod.get());
    fun->setCallingConv(NOMCC);
    NomBuilder builder;
    builder->SetInsertPoint(BasicBlock::Create(LLVMCONTEXT, "", fun));
    CastedValueCompileEnv env(
        {}, {}, fun, 1, 0, ConstantPointerNull::get(TYPETYPE->getPointerTo()));
    builder->CreateRet(EnsurePackedUnpacked(
        builder, RTCast::GenerateCast(builder, &env, fun->arg_begin(), type),
        REFTYPE));
    if (auto err = jit.addModule(std::move(mod))) {
      logAllUnhandledErrors(std::move(err), errs(),
                            "Could not add result cast " + name + ": ");
      throw new std::exception();
    }
  }
  auto sym = jit.lookup(name);
  if (!sym) {
    logAllUnhandledErrors(sym.takeError(), errs(),
                          "Could not find result cast " + name + ": ");
    throw new std::exception();
  }
  iter->second = reinterpret_cast<void *>(sym->getAddress());
  return iter->second;
}

// The interface call-tag own result cast, for return types with type variables
// Its module is in the JIT: the bridge only runs after a call
// went through the call-tag, which calls this function
static void *LookupCallTagRetValCast(const NomInterfaceCallTag *callTag) {
  static std::unordered_map<const NomInterfaceCallTag *, void *> casts;
  auto [iter, inserted] = casts.try_emplace(callTag, nullptr);
  if (!inserted) {
    return iter->second;
  }
  auto name = callTag->GetResultCastFunctionName();
  auto sym = NomJIT::Instance().lookup(name);
  if (!sym) {
    logAllUnhandledErrors(sym.takeError(), errs(),
                          "Could not find result cast " + name + ": ");
    throw new std::exception();
  }
  iter->second = reinterpret_cast<void *>(sym->getAddress());
  return iter->second;
}

static bool NeedsCallTagResultCast(const NomInterfaceCallTag *callTag) {
  auto ifaceMethod = callTag->GetMethod();
  return ifaceMethod->ResultNeedsCast() &&
         ifaceMethod->GetReturnType()->ContainsVariables();
}

namespace {
struct SpecializedResultCast {
  NomTypeRef type = nullptr; // null for no specialization use the generic cast
  bool needsCast = true;     // false for the method result is already a subtype
};
} // namespace

static SpecializedResultCast
GetSpecializedResultCast(const CallTagTarget &target,
                         const NomInterfaceCallTag *callTag,
                         const ReceiverTypeArgs &recvTypeArgs) {
  SpecializedResultCast ret;
  auto ifaceMethod = callTag->GetMethod();
  // a generic method own type arguments are only known per call
  if (recvTypeArgs.iface == nullptr || !NeedsCallTagResultCast(callTag) ||
      ifaceMethod->GetContainer() != recvTypeArgs.iface ||
      ifaceMethod->GetDirectTypeParametersCount() != 0) {
    return ret;
  }
  NomSubstitutionContextList nscl(recvTypeArgs.nomArgs);
  auto returnType = ifaceMethod->GetReturnType(&nscl);
  if (returnType->ContainsVariables()) {
    return ret;
  }
  ret.type = returnType;
  if (target.kind == DispatchTargetKind::Method) {
    NomSubstitutionContextMemberContext nscmc(target.method);
    ret.needsCast =
        !target.method->GetReturnType(&nscmc)->IsSubtype(returnType);
  }
  return ret;
}

// Looks up and caches the address of the result-cast functions the branch
// for callTag calls so building the entry only reads the caches
static void CacheCallTagTargetCastFun(const NomRecord *nomRecord,
                                      const NomInterfaceCallTag *callTag,
                                      const ReceiverTypeArgs &recvTypeArgs) {
  auto target = GetCallTagTarget(nomRecord, callTag);
  if (auto castType = ClosedResultCastType(target, callTag)) {
    LookupRetValCastforT(castType);
  } else if (NeedsCallTagResultCast(callTag)) {
    LookupCallTagRetValCast(callTag);
    auto spec = GetSpecializedResultCast(target, callTag, recvTypeArgs);
    if (spec.type != nullptr && spec.needsCast) {
      LookupRetValCastforT(spec.type);
    }
  }
}

void CacheIMTEntryCastFuns(const NomRecord *nomRecord,
                           const NomInterfaceCallTag *callTag,
                           const IMTCallTagSet &parentCallTags,
                           const ReceiverTypeArgs &recvTypeArgs) {
  CacheCallTagTargetCastFun(nomRecord, callTag, recvTypeArgs);
  for (auto &[parentTag, parentTagFunAddr] : parentCallTags) {
    CacheCallTagTargetCastFun(nomRecord, parentTag, recvTypeArgs);
  }
}

// true if the receiver cast data is the class type the entry was specialized on
static Value *GenerateReceiverTypeArgsMatch(NomBuilder &builder, Function *fun,
                                            Value *receiver,
                                            const ReceiverTypeArgs &args,
                                            BasicBlock *mismatchBlock) {
  auto castData =
      StructuralValueHeader::GenerateReadCastData(builder, receiver);
  auto castDataInt = builder->CreatePtrToInt(castData, numtype(intptr_t));
  auto single = builder->CreateAnd(
      builder->CreateIsNotNull(castData),
      builder->CreateICmpEQ(
          builder->CreateAnd(castDataInt,
                             ConstantInt::get(castDataInt->getType(), 1)),
          ConstantInt::get(castDataInt->getType(), 0)));
  BasicBlock *checkBlock =
      BasicBlock::Create(LLVMCONTEXT, "checkReceiverTypeArgs", fun);
  builder->CreateCondBr(single, checkBlock, mismatchBlock,
                        GetLikelyFirstBranchMetadata());
  builder->SetInsertPoint(checkBlock);
  auto iface = RTClassType::GenerateReadClassDescriptorLink(builder, castData);
  Value *match = builder->CreateICmpEQ(
      builder->CreatePtrToInt(iface, numtype(intptr_t)),
      ConstantInt::get(numtype(intptr_t),
                       reinterpret_cast<intptr_t>(args.rtIface)));
  auto argsPtr = builder->CreatePointerCast(castData, TYPETYPE->getPointerTo());
  for (size_t k = 0; k < args.rtArgs.size(); k++) {
    auto arg = MakeInvariantLoad(
        builder, builder->CreateGEP(argsPtr, MakeInt32(-(int32_t)(k + 1))),
        "receiverTypeArg");
    match = builder->CreateAnd(
        match,
        builder->CreateICmpEQ(
            builder->CreatePtrToInt(arg, numtype(intptr_t)),
            ConstantInt::get(numtype(intptr_t),
                             reinterpret_cast<intptr_t>(args.rtArgs[k]))));
  }
  return match;
}

Value *GenerateBranchResultCast(NomBuilder &builder, Function *fun,
                                const CallTagTarget &target,
                                const NomInterfaceCallTag *callTag,
                                Value *argCallTag, Value **varargs,
                                const ReceiverTypeArgs &recvTypeArgs,
                                Value *result) {
  auto &mod = *fun->getParent();
  if (auto castType = ClosedResultCastType(target, callTag)) {
    // return type is closed but not subtype of interface method return type
    if (NomCastStats) {
      builder->CreateCall(GetIncIMTClosedResultCasts(mod), {});
    }
    auto castFun = builder->CreateIntToPtr(
        ConstantInt::get(
            numtype(intptr_t),
            reinterpret_cast<intptr_t>(LookupRetValCastforT(castType))),
        GetResultCastFunctionType()->getPointerTo());
    auto castCall =
        builder->CreateCall(GetResultCastFunctionType(), castFun, {result});
    castCall->setCallingConv(NOMCC);
    return castCall;
  }
  if (!NeedsCallTagResultCast(callTag)) {
    if (NomCastStats) {
      builder->CreateCall(GetIncIMTUncastResults(mod), {});
    }
    return result;
  }
  auto spec = GetSpecializedResultCast(target, callTag, recvTypeArgs);
  BasicBlock *joinBlock = nullptr;
  PHINode *resultPHI = nullptr;
  if (spec.type != nullptr) {
    BasicBlock *fastBlock =
        BasicBlock::Create(LLVMCONTEXT, "specializedResultCast", fun);
    BasicBlock *genericBlock =
        BasicBlock::Create(LLVMCONTEXT, "genericResultCast", fun);
    joinBlock = BasicBlock::Create(LLVMCONTEXT, "resultCastDone", fun);
    result = EnsurePackedUnpacked(builder, result, REFTYPE);
    auto match = GenerateReceiverTypeArgsMatch(builder, fun, varargs[0],
                                               recvTypeArgs, genericBlock);
    builder->CreateIntrinsic(Intrinsic::expect, {inttype(1)},
                             {match, MakeUInt(1, 1)});
    builder->CreateCondBr(match, fastBlock, genericBlock,
                          GetLikelyFirstBranchMetadata());

    builder->SetInsertPoint(joinBlock);
    resultPHI = builder->CreatePHI(REFTYPE, 2, "resultCast");

    builder->SetInsertPoint(fastBlock);
    if (NomCastStats) {
      builder->CreateCall(spec.needsCast
                              ? GetIncIMTSpecializedResultCasts(mod)
                              : GetIncIMTSpecializedUncastResults(mod),
                          {});
    }
    Value *fastResult = result;
    if (spec.needsCast) {
      auto castFun = builder->CreateIntToPtr(
          ConstantInt::get(
              numtype(intptr_t),
              reinterpret_cast<intptr_t>(LookupRetValCastforT(spec.type))),
          GetResultCastFunctionType()->getPointerTo());
      auto castCall =
          builder->CreateCall(GetResultCastFunctionType(), castFun, {result});
      castCall->setCallingConv(NOMCC);
      fastResult = castCall;
    }
    resultPHI->addIncoming(fastResult, builder->GetInsertBlock());
    builder->CreateBr(joinBlock);

    builder->SetInsertPoint(genericBlock);
  }
  if (NomCastStats) {
    builder->CreateCall(GetIncIMTGenericResultCasts(mod), {});
  }
  auto castFun = builder->CreateIntToPtr(
      ConstantInt::get(
          numtype(intptr_t),
          reinterpret_cast<intptr_t>(LookupCallTagRetValCast(callTag))),
      NomInterfaceCallTag::GetResultCastFunctionType()->getPointerTo());
  auto castArgs = makealloca(Value *, 3 + RTConfig_NumberOfVarargsArguments);
  castArgs[0] = argCallTag;
  for (decltype(RTConfig_NumberOfVarargsArguments) i = 0;
       i <= RTConfig_NumberOfVarargsArguments; i++) {
    castArgs[i + 1] = varargs[i];
  }
  castArgs[2 + RTConfig_NumberOfVarargsArguments] = result;
  auto castCall = builder->CreateCall(
      NomInterfaceCallTag::GetResultCastFunctionType(), castFun,
      ArrayRef<Value *>(castArgs, 3 + RTConfig_NumberOfVarargsArguments));
  castCall->setCallingConv(NOMCC);
  if (joinBlock == nullptr) {
    return castCall;
  }
  resultPHI->addIncoming(castCall, builder->GetInsertBlock());
  builder->CreateBr(joinBlock);
  builder->SetInsertPoint(joinBlock);
  return resultPHI;
}
} // namespace IMTResultCast
} // namespace Runtime
} // namespace Nom
