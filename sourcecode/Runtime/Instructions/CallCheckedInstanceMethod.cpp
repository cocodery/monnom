#include "CallCheckedInstanceMethod.h"
#include "CallingConvConf.h"
#include "CastStats.h"
#include "CompileHelpers.h"
#include "FloatClass.h"
#include "IMT.h"
#include "IntClass.h"
#include "NomAlloc.h"
#include "NomClass.h"
#include "NomClassType.h"
#include "NomInterface.h"
#include "NomInterfaceCallTag.h"
#include "NomMethod.h"
#include "NomVMIMTInterface.h"
#include "RTCast.h"
#include "RTCompileConfig.h"
#include "RTConfig.h"
#include "RefValueHeader.h"
#include "TypeOperations.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/raw_os_ostream.h"
#include <NomVMInterface.h>
#include <cstdio>
#include <iostream>

using namespace llvm;
using namespace std;
namespace Nom {
namespace Runtime {
CallCheckedInstanceMethod::~CallCheckedInstanceMethod() {}
void CallCheckedInstanceMethod::Compile(NomBuilder &builder, CompileEnv *env,
                                        int lineno) {
  env->basicBlockTerminated = false;
  NomSubstitutionContextMemberContext nscmc(env->Context);

  NomInstantiationRef<const NomMethod> method =
      NomConstants::GetMethod(Method)->GetMethod(&nscmc);

  bool rawInvoke = false;
  if ((!method.Elem->IsFinal()) && method.Elem->GetName().empty() &&
      NomLambdaOptimizationLevel > 0) {
    rawInvoke = true;
  }

  NomSubstitutionContextList nscl(method.TypeArgs);

  int additionalArgs = 0;
  auto recContainer = method.Elem->GetContainer();
  if (rawInvoke) {
    additionalArgs +=
        1; // used to indicate which interface the raw invocation is based on
  } else if (recContainer->IsInterface()) {
    additionalArgs += 1; // these are for calling the IMT method, no need for
                         // them when invoking directly
  }

  auto substitutedSig = method.Elem->Signature(&nscl);

  auto argarr =
      makealloca(Value *, max(method.Elem->GetArgumentCount() +
                                  method.Elem->GetDirectTypeParametersCount() +
                                  1 + additionalArgs,
                              (size_t)RTConfig_NumberOfVarargsArguments + 2));
  argarr += additionalArgs;

  for (decltype(RTConfig_NumberOfVarargsArguments) i = 0;
       i <= RTConfig_NumberOfVarargsArguments; i++) {
    argarr[i] = ConstantPointerNull::get(REFTYPE);
  }

  argarr[0] = (*env)[Receiver];

  // method.TypeArgs holds the class's type arguments followed by the method's
  // own; the method's may contain the caller's type variables
  auto directTypeArgCount = method.Elem->GetDirectTypeParametersCount();
  auto classTypeArgCount = method.TypeArgs.size() - directTypeArgCount;
  for (size_t i = 1; i <= directTypeArgCount; i++) {
    argarr[i] = method.TypeArgs[classTypeArgCount + i - 1]
                    ->GenerateRTInstantiation(builder, env);
  }

  for (int i = env->GetArgCount() - 1; i >= 0; i--) {
    argarr[i + 1 + method.Elem->GetDirectTypeParametersCount()] =
        env->GetArgument(i);
  }

  FunctionType *ftype = method.Elem->GetLLVMFunctionType();
  bool uncertainty = true;

  Type *pt;
  Type *argt;
  // every parameter: [0] receiver, [1..T] type arguments (skipped), then the
  // value arguments
  for (int i = env->GetArgCount() + method.Elem->GetDirectTypeParametersCount();
       i >= 0; i--) {
    if (i > 0 && i <= method.Elem->GetDirectTypeParametersCount()) {
      continue;
    }
    pt = ftype->getParamType(i);
    argt = argarr[i]->getType();
    if (pt != argt) {
      if (pt->isIntegerTy() && argt->isPointerTy()) {
        argarr[i] = UnpackInt(builder, argarr[i]);
        continue;
      }
      if (pt->isDoubleTy() && argt->isPointerTy()) {
        argarr[i] = UnpackFloat(builder, argarr[i]);
        continue;
      }
      // a Bool computed by a comparison is a raw i1, e.g. (x < 5).ToString()
      if (pt->isPointerTy() && argt->isIntegerTy(1)) {
        argarr[i] = PackBool(builder, argarr[i]);
        continue;
      }
      if (pt->isPointerTy() && argt->isIntegerTy()) {
        argarr[i] = PackInt(builder, argarr[i]);
        continue;
      }
      if (pt->isPointerTy() && argt->isDoubleTy()) {
        argarr[i] = PackFloat(builder, argarr[i]);
        continue;
      }
      llvm::raw_os_ostream out(std::cout);
      pt->print(out);
      std::cout << "\n";
      argt->print(out);
      out.flush();
      std::cout.flush();
      throw new std::exception();
    }
  }
  env->ClearArguments();

  auto mod = builder->GetInsertBlock()->getParent()->getParent();

  if (method.Elem->IsFinal()) {
    if (NomCastStats) {
      builder->CreateCall(GetIncFinalInstanceMethodCalls(*mod), {});
    }
    Function *fun = method.Elem->GetLLVMFunction(env->Module);
    auto call = builder->CreateCall(
        fun, llvm::ArrayRef<llvm::Value *>(
                 argarr, method.Elem->GetArgumentCount() +
                             method.Elem->GetDirectTypeParametersCount() + 1));
    call->setCallingConv(NOMCC);
    RegisterValue(env, NomValue(call, method.Elem->GetReturnType(&nscl), true));
    return;
  }

  NomClassTypeRef inttype = NomIntClass::GetInstance()->GetType();
  NomClassTypeRef floattype = NomFloatClass::GetInstance()->GetType();

  Value *receiver = (*env)[Receiver];

  if (rawInvoke) {
    // builder->CreateCall(
    //     GetPrint(mod),
    //     {ConstantInt::get(
    //          INTTYPE,
    //          reinterpret_cast<uint64_t>(new std::string(
    //              "Raw invocation of method: " + method.Elem->GetName() +
    //              "\n")),
    //          false),
    //      llvm::ConstantInt::get(INTTYPE, 1, false)});

    if (NomCastStats) {
      builder->CreateCall(GetIncTypedRawInvokes(*mod), {});
    }
    auto recNV = (*env)[Receiver];
    llvm::Value *methodptr = builder->CreatePointerCast(
        RefValueHeader::GenerateReadRawInvoke(builder, recNV),
        GetIMTFunctionType()->getPointerTo());
    auto argsArrSize = method.Elem->GetArgumentCount() +
                       method.Elem->GetDirectTypeParametersCount() + 1 -
                       RTConfig_NumberOfVarargsArguments;
    if (method.Elem->GetArgumentCount() +
            method.Elem->GetDirectTypeParametersCount() >
        RTConfig_NumberOfVarargsArguments) {
      llvm::Value *argsasarr = builder->CreateAlloca(
          POINTERTYPE, MakeInt32(argsArrSize), "argarray");

      builder->CreateIntrinsic(
          llvm::Intrinsic::lifetime_start, {POINTERTYPE},
          {MakeInt<int64_t>(
               GetNomJITDataLayout().getTypeAllocSize(POINTERTYPE) *
               argsArrSize),
           builder->CreatePointerCast(argsasarr, POINTERTYPE)});
      for (int j = argsArrSize - 1; j >= 0; j--) {
        MakeStore(builder,
                  WrapAsPointer(builder,
                                argarr[j + RTConfig_NumberOfVarargsArguments]),
                  builder->CreateGEP(argsasarr, MakeInt32(j), "arginarray"),
                  AtomicOrdering::NotAtomic);
      }
      argarr[RTConfig_NumberOfVarargsArguments] =
          builder->CreatePointerCast(argsasarr, POINTERTYPE);
    }
    for (int j = 0; j <= RTConfig_NumberOfVarargsArguments; j++) {
      argarr[j] = WrapAsPointer(builder, argarr[j]);
    }
    argarr -= 1;
    argarr[0] =
        builder->CreatePointerCast(NomInterfaceCallTag::GetCallTag(method.Elem)
                                       ->GetLLVMElement(*env->Module),
                                   POINTERTYPE);
    auto call = builder->CreateCall(
        GetIMTFunctionType(), methodptr,
        llvm::ArrayRef<llvm::Value *>(argarr,
                                      2 + RTConfig_NumberOfVarargsArguments),
        "rawInvoke_" + method.Elem->GetContainer()->GetName()->ToStdString());
    call->setCallingConv(NOMCC);
    auto result = EnsurePackedUnpacked(
        builder, call, method.Elem->GetLLVMFunctionType()->getReturnType());
    RegisterValue(env,
                  NomValue(result, method.Elem->GetReturnType(&nscl), true));
  } else if (method.Elem->GetContainer()->IsInterface()) {
    // builder->CreateCall(
    //     GetPrint(mod),
    //     {ConstantInt::get(
    //          INTTYPE,
    //          reinterpret_cast<uint64_t>(new std::string(
    //              "Interface call to method: " + method.Elem->GetName() +
    //              "\n")),
    //          false),
    //      llvm::ConstantInt::get(INTTYPE, 1, false)});

    Value *invariantID = nullptr;
    auto argsArrSize = method.Elem->GetArgumentCount() +
                       method.Elem->GetDirectTypeParametersCount() + 1 -
                       RTConfig_NumberOfVarargsArguments;
    if (method.Elem->GetArgumentCount() +
            method.Elem->GetDirectTypeParametersCount() >
        RTConfig_NumberOfVarargsArguments) {
      if (NomCastStats) {
        builder->CreateCall(GetIncExtendedInterfaceMethodCalls(*mod), {});
      }
      llvm::Value *argsasarr = builder->CreateAlloca(
          POINTERTYPE, MakeInt32(argsArrSize), "argarray");

      builder->CreateIntrinsic(
          llvm::Intrinsic::lifetime_start, {POINTERTYPE},
          {MakeInt<int64_t>(
               GetNomJITDataLayout().getTypeAllocSize(POINTERTYPE) *
               argsArrSize),
           builder->CreatePointerCast(argsasarr, POINTERTYPE)});
      for (int j = argsArrSize - 1; j >= 0; j--) {
        MakeStore(builder,
                  WrapAsPointer(builder,
                                argarr[j + RTConfig_NumberOfVarargsArguments]),
                  builder->CreateGEP(argsasarr, MakeInt32(j), "arginarray"),
                  AtomicOrdering::NotAtomic);
      }
      argarr[RTConfig_NumberOfVarargsArguments] =
          builder->CreatePointerCast(argsasarr, POINTERTYPE);
    } else {
      if (NomCastStats) {
        builder->CreateCall(GetIncInterfaceMethodCalls(*mod), {});
      }
    }
    for (int j = 0; j <= RTConfig_NumberOfVarargsArguments; j++) {
      argarr[j] = WrapAsPointer(builder, argarr[j]);
    }
    argarr -= 1;

    auto ict = NomInterfaceCallTag::GetCallTag(method.Elem);
    argarr[0] = builder->CreatePointerCast(ict->GetLLVMElement(*env->Module),
                                           POINTERTYPE);

    // builder->CreateCall(GetWriteFunCallTag(mod),
    //                     {builder->CreatePtrToInt(argarr[0],
    //                     numtype(intptr_t)),
    //                      llvm::ConstantInt::get(
    //                          INTTYPE, reinterpret_cast<intptr_t>(ict),
    //                          false)});

    // builder->CreateCall(GetPrint(mod),
    //                     {builder->CreatePtrToInt(argarr[0],
    //                     numtype(intptr_t)),
    //                      llvm::ConstantInt::get(INTTYPE, 0, false)});

    // builder->CreateCall(
    //     GetPrint(mod),
    //     {ConstantInt::get(INTTYPE,
    //                       reinterpret_cast<uint64_t>(new std::string(
    //                           "Should go to IMT slot " +
    //                           std::to_string(method.Elem->GetIMTIndex()) +
    //                           " for method: " + method.Elem->GetName() +
    //                           "\n")),
    //                       false),
    //      llvm::ConstantInt::get(Type::getIntNTy(LLVMCONTEXT,
    //      bitsin(uint64_t)),
    //                             1, false)});

    Value *methodptr = ObjectHeader::GetInterfaceMethodTableFunction(
        builder, env, Receiver, MakeInt32(method.Elem->GetIMTIndex()), lineno);

    auto call =
        builder->CreateCall(GetIMTFunctionType(), methodptr,
                            llvm::ArrayRef<llvm::Value *>(
                                argarr, RTConfig_NumberOfVarargsArguments + 2));
    call->setName("calling " + method.Elem->GetName());
    call->setCallingConv(NOMCC);
    auto rettype = method.Elem->GetLLVMFunctionType()->getReturnType();
    Value *result;
    if (rettype->isDoubleTy()) {
      result = builder->CreateBitCast(builder->CreatePtrToInt(call, INTTYPE),
                                      FLOATTYPE);
    } else {
      result = builder->CreateBitOrPointerCast(call, rettype);
    }
    if (RTConfig_EnableTransition && rettype == REFTYPE) {
      // in normal version return value from RCT is casted in ICT
      // but IMT Transition get return value from IMT Entry without cast
      result = RTCast::GenerateCast(builder, env, result,
                                    method.Elem->GetReturnType(&nscl));
    }
    RegisterValue(env,
                  NomValue(result, method.Elem->GetReturnType(&nscl), true));
    if (invariantID != nullptr) {
      builder->CreateIntrinsic(
          llvm::Intrinsic::invariant_end, {POINTERTYPE},
          {invariantID,
           MakeInt<int64_t>(
               GetNomJITDataLayout().getTypeAllocSize(POINTERTYPE) *
               argsArrSize),
           argarr[RTConfig_NumberOfVarargsArguments + 1]});
    }
  } else { // for dynamic call
    if (NomCastStats) {
      builder->CreateCall(GetIncDirectClassMethodCalls(*mod), {});
    }
    // builder->CreateCall(
    //     GetPrint(mod),
    //     {ConstantInt::get(INTTYPE,
    //                       reinterpret_cast<uint64_t>(
    //                           new std::string("Direct call to non-final \
    //                                           method "
    //                                           "without interface: " +
    //                                           method.Elem->GetName() +
    //                                           "\n")),
    //                       false),
    //      llvm::ConstantInt::get(INTTYPE, 1, false)});

    Value *methodptr = ObjectHeader::GetDispatchMethodPointer(
        builder, env, Receiver, lineno, method);
    // receiver, the method's own type arguments, then the value arguments
    auto call = builder->CreateCall(
        method.Elem->GetLLVMFunctionType(), methodptr,
        llvm::ArrayRef<llvm::Value *>(
            argarr, method.Elem->GetArgumentCount() +
                        method.Elem->GetDirectTypeParametersCount() + 1));
    call->setName("calling " + method.Elem->GetName());
    call->setCallingConv(NOMCC);
    RegisterValue(env, NomValue(call, method.Elem->GetReturnType(&nscl), true));
  }
}
void CallCheckedInstanceMethod::Print(bool resolve) {
  cout << "Call #" << std::dec << Receiver;
  cout << ".";
  NomConstants::PrintConstant(Method, resolve);
  cout << " -> #" << std::dec << WriteRegister;
  cout << "\n";
}
void CallCheckedInstanceMethod::FillConstantDependencies(
    NOM_CONSTANT_DEPENCENCY_CONTAINER &result) {
  result.push_back(Method);
  result.push_back(TypeArgs);
}
} // namespace Runtime
} // namespace Nom
