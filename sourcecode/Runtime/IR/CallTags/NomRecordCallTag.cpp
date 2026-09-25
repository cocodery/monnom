#include "NomRecordCallTag.h"
#include "AvailableExternally.h"
#include "CallingConvConf.h"
#include "CompileHelpers.h"
#include "Defs.h"
#include "IMT.h"
#include "NomInterfaceCallTag.h"
#include "NomLambdaCallTag.h"
#include "NomNameRepository.h"
#include "NomString.h"
#include "NomVMIMTInterface.h"
#include "NomVMInterface.h"
#include "RTCompileConfig.h"
#include "RTVTable.h"
#include "RefValueHeader.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_os_ostream.h"
#include <cstdint>
#include <iostream>
#include <llvm/IR/Constant.h>
#include <unordered_map>

using namespace std;
using namespace llvm;
namespace Nom {
namespace Runtime {
struct RCTTupleHash {
  std::size_t
  operator()(std::tuple<const std::string *, int, int> &s) const noexcept {
    return std::hash<std::string>()(*std::get<0>(s)) +
           (std::get<1>(s) * 93739 + std::get<2>(s)) * 45569;
  }
  std::size_t operator()(
      const std::tuple<const std::string *, int, int> &s) const noexcept {
    return std::hash<std::string>()(*std::get<0>(s)) +
           (std::get<1>(s) * 93739 + std::get<2>(s)) * 45569;
  }
};
struct RCTTupleEq {
  bool
  operator()(std::tuple<const std::string *, int, int> &lhs,
             std::tuple<const std::string *, int, int> &rhs) const noexcept {
    return std::equal_to<std::string>()(*std::get<0>(lhs), *std::get<0>(rhs)) &&
           std::get<1>(lhs) == std::get<1>(rhs) &&
           std::get<2>(lhs) == std::get<2>(rhs);
  }
  bool
  operator()(const std::tuple<const std::string *, int, int> &lhs,
             std::tuple<const std::string *, int, int> &rhs) const noexcept {
    return std::equal_to<std::string>()(*std::get<0>(lhs), *std::get<0>(rhs)) &&
           std::get<1>(lhs) == std::get<1>(rhs) &&
           std::get<2>(lhs) == std::get<2>(rhs);
  }
};
NomRecordCallTag::NomRecordCallTag(const std::string &name, int typeargcount,
                                   int argcount)
    : name(name), typeargcount(typeargcount), argcount(argcount) {}
const NomRecordCallTag *NomRecordCallTag::GetCallTag(const std::string &name,
                                                     int typeargcount,
                                                     int argcount) {
  static unordered_map<std::string, NomRecordCallTag *> callTags;
  std::string key = name + "/" + std::to_string(typeargcount) + "/" +
                    std::to_string(argcount);
  auto findResult = callTags.find(key);
  if (findResult == callTags.end()) {
    auto newrct = new NomRecordCallTag(name, typeargcount, argcount);
    callTags[key] = newrct;
  }
  return callTags[key];
}
NomRecordCallTag::~NomRecordCallTag() {}
llvm::Constant *NomRecordCallTag::createLLVMElement2(
    llvm::Module &mod, llvm::GlobalValue::LinkageTypes linkage) const {
  Function *fun =
      mod.getFunction("MONNOM_RT_RCT_" + name + "/" + to_string(typeargcount) +
                      "/" + to_string(argcount));
  if (fun == nullptr) {
    fun = Function::Create(GetIMTFunctionType(), linkage,
                           "MONNOM_RT_RCT_" + name + "/" +
                               to_string(typeargcount) + "/" +
                               to_string(argcount),
                           mod);
    fun->setCallingConv(NOMCC);

    NomBuilder builder;

    BasicBlock *startBlock = BasicBlock::Create(LLVMCONTEXT, "", fun);
    builder->SetInsertPoint(startBlock);

    auto argiter = fun->arg_begin();
    auto callTag = argiter; // the target interface call tag of the call
    argiter++;
    auto receiver = (NomValue)argiter; // the receiver of the call
    argiter++;

    auto vtable = RefValueHeader::GenerateReadVTablePointer(
        builder, receiver); // RTVTable
    // auto imt_entry = RTVTable::GenerateReadInterfaceMethodTableEntry(
    //     builder, vtable,
    //     MakeInt32((NomString(name).HashCode() + (GetOffset() * 4177)) %
    //               IMTsize)); // IMTEntry

    llvm::raw_os_ostream out(std::cout);
    if (verifyFunction(*fun, &out)) {
      out.flush();
      std::cout << "Could not verify Record Call Tag function ";
      std::cout << name << "/" << typeargcount << "/" << argcount;
      fun->print(out);
      out.flush();
      std::cout.flush();
      throw new std::exception();
    }
  }
  return fun;
}
llvm::Constant *NomRecordCallTag::createLLVMElement(
    llvm::Module &mod, llvm::GlobalValue::LinkageTypes linkage) const {
  Function *fun =
      mod.getFunction("MONNOM_RT_RCT_" + name + "/" + to_string(typeargcount) +
                      "/" + to_string(argcount));
  if (fun == nullptr) {
    fun = Function::Create(GetIMTFunctionType(), linkage,
                           "MONNOM_RT_RCT_" + name + "/" +
                               to_string(typeargcount) + "/" +
                               to_string(argcount),
                           mod);
    fun->setCallingConv(NOMCC);

    NomBuilder builder;

    BasicBlock *startBlock = BasicBlock::Create(LLVMCONTEXT, "", fun);
    builder->SetInsertPoint(startBlock);

    // builder->CreateCall(
    //     GetPrint(&mod),
    //     {ConstantInt::get(
    //          Type::getIntNTy(LLVMCONTEXT, bitsin(uint64_t)),
    //          reinterpret_cast<uint64_t>(new std::string(
    //              "Call record call tag: /" + to_string(typeargcount) + "/" +
    //              to_string(argcount) + " in RCT-fun\n")),
    //          false),
    //      llvm::ConstantInt::get(Type::getIntNTy(LLVMCONTEXT,
    //      bitsin(uint64_t)),
    //                             1, false)});

    auto argiter = fun->arg_begin();
    auto callTag = argiter;
    argiter++;
    auto receiver = argiter;
    argiter++;

    auto argarr = makealloca(Value *, 2 + RTConfig_NumberOfVarargsArguments);
    argarr[0] = NomLambdaCallTag::GetCallTag(typeargcount, argcount)
                    ->GetLLVMElement(mod);
    argarr[1] = receiver;
    for (decltype(RTConfig_NumberOfVarargsArguments) i = 0;
         i < RTConfig_NumberOfVarargsArguments; i++) {
      argarr[i + 2] = argiter;
      argiter++;
    }
    auto vtable = RefValueHeader::GenerateReadVTablePointer(builder, receiver);
    auto dpair = RTVTable::GenerateFindDynamicDispatcherPair(
        builder, builder->CreatePointerCast(receiver, REFTYPE), vtable,
        NomNameRepository::Instance().GetNameID(name));

    //
    // callTag is runtime potnier to the function header not the llvm::Function
    // TODO: use the callTag to find the NomCallTag object
    llvm::Value *callTagAddr = builder->CreateCall(
        GetReadFunCallTag(&mod),
        {builder->CreatePtrToInt(callTag, numtype(intptr_t))});

    // TODO: Call the transition method
    // Arguments: RTVTable of the receiver, NomInterfaceCallTag
    // Return   : Return the updated IMT Entry
    // auto imtarray = RTVTable::GenerateReadInterfaceMethodTable(builder, vtable);
    //

    auto target = builder->CreateExtractValue(dpair, {0});
    argarr[1] = builder->CreatePointerCast(
        builder->CreateExtractValue(dpair, {1}), POINTERTYPE);

    auto callResult = builder->CreateCall(
        GetIMTFunctionType(), target,
        ArrayRef<Value *>(argarr, 2 + RTConfig_NumberOfVarargsArguments));
    callResult->setCallingConv(NOMCC);
    if (NOMCC == CallingConv::Fast) {
      callResult->setTailCallKind(CallInst::TailCallKind::TCK_MustTail);
    }
    builder->CreateRet(callResult);

    llvm::raw_os_ostream out(std::cout);
    if (verifyFunction(*fun, &out)) {
      out.flush();
      std::cout << "Could not verify Record Call Tag function ";
      std::cout << name << "/" << typeargcount << "/" << argcount;
      fun->print(out);
      out.flush();
      std::cout.flush();
      throw new std::exception();
    }
  }
  return fun;
}
llvm::Constant *NomRecordCallTag::findLLVMElement(llvm::Module &mod) const {
  return mod.getFunction("MONNOM_RT_RCT_" + name + "/" +
                         to_string(typeargcount) + "/" + to_string(argcount));
}
} // namespace Runtime
} // namespace Nom