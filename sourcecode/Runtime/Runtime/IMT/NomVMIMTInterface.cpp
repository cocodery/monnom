#include "NomVMIMTInterface.h"
#include "AvailableExternally.h"
#include "Defs.h"
#include "NomInterfaceCallTag.h"
#include "NomJIT.h"
#include "NomMethod.h"
#include "NomRecord.h"
#include "RTVTable.h"
#include <cstdint>
#include <exception>
#include <iostream>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <string>
#include <unordered_map>

using namespace llvm;
using namespace Nom::Runtime;

static std::unordered_map<intptr_t, intptr_t> &GetFunCallTagMap() {
  static std::unordered_map<intptr_t, intptr_t> funCallTagMap;
  return funCallTagMap;
}

llvm::Function *GetReadFunCallTag(llvm::Module *mod) {
  Function *ret = mod->getFunction("CPP_NOM_ReadFunCallTag");
  if (ret == nullptr) {
    FunctionType *funType = FunctionType::get(INTTYPE, {INTTYPE}, false);
    ret = Function::Create(funType, Function::ExternalLinkage,
                           "CPP_NOM_ReadFunCallTag", mod);
  }
  return ret;
}

extern "C" DLLEXPORT intptr_t CPP_NOM_ReadFunCallTag(intptr_t funAddr) {
  auto &map = GetFunCallTagMap();
  auto it = map.find(funAddr);
  if (it != map.end()) {
    auto callTagAddr = it->second;
    std::cout << "Read  - Fun: " << funAddr << " | CallTag: " << callTagAddr
              << std::endl;
    return callTagAddr;
  }
  throw std::exception();
}

llvm::Function *GetWriteFunCallTag(llvm::Module *mod) {
  Function *ret = mod->getFunction("CPP_NOM_WriteFunCallTag");
  if (ret == nullptr) {
    FunctionType *funType = FunctionType::get(Type::getVoidTy(LLVMCONTEXT),
                                              {INTTYPE, INTTYPE}, false);
    ret = Function::Create(funType, Function::ExternalLinkage,
                           "CPP_NOM_WriteFunCallTag", mod);
  }
  return ret;
}

extern "C" DLLEXPORT void CPP_NOM_WriteFunCallTag(intptr_t funAddr,
                                                  intptr_t callTagAddr) {
  auto &map = GetFunCallTagMap();
  auto it = map.find(funAddr);
  if (it == map.end()) { // for first time check
    std::cout << "Write - Fun: " << funAddr << " | CallTag: " << callTagAddr
              << std::endl;
    map[funAddr] = callTagAddr;
  } else if (it->second != callTagAddr) { // check value as key inserted before
    throw std::exception();
  }
  // key exists && the values are equal
}

llvm::Function *GetIMTTransition(llvm::Module *mod) {
  Function *ret = mod->getFunction("CPP_NOM_GetIMTTransition");
  if (ret == nullptr) {
    FunctionType *funType = FunctionType::get(
        POINTERTYPE, {INTTYPE, INTTYPE, INTTYPE, POINTERTYPE}, false);
    ret = Function::Create(funType, Function::ExternalLinkage,
                           "CPP_NOM_GetIMTTransition", mod);
  }
  return ret;
}

extern "C" DLLEXPORT void *CPP_NOM_GetIMTTransition(void *vTable,
                                                    void *callTagAddr,
                                                    void *nomRecordAddr,
                                                    void *imtArray) {
  // vtable for reassign IMT
  // callTagAddr for get interface method index in IMT
  // callTagFunAddr for insert as branch condition
  // imtArrary for copy & rewrite then install to vtable
  auto vtable = reinterpret_cast<RTVTable *>(vTable);
  auto callTag = reinterpret_cast<NomInterfaceCallTag *>(callTagAddr);
  auto nomRecord = reinterpret_cast<NomRecord *>(nomRecordAddr);

  // std::cout << callTag->GetKey() << std::endl;
  // std::cout << callTag->GetMethod()->GetIMTIndex() << std::endl;
  // std::cout << nomRecord->GetName() << std::endl;

  auto imtIndex = callTag->GetMethod()->GetIMTIndex();

  return nullptr;

  // // Build `i64 f() { return 5; }` in a fresh module, JIT it, and return the
  // // native code address. The llvm::Function itself is freed once compiled.
  // static int transitionCount = 0;
  // std::string name =
  //     "MONNOM_RT_IMT_TRANSITION_" + std::to_string(transitionCount++);

  // auto &jit = NomJIT::Instance();
  // auto mod = std::make_unique<Module>(name, LLVMCONTEXT);
  // mod->setDataLayout(jit.getDataLayout());
  // Function *fun = Function::Create(FunctionType::get(INTTYPE, false),
  //                                  Function::ExternalLinkage, name,
  //                                  mod.get());
  // IRBuilder<> builder(BasicBlock::Create(LLVMCONTEXT, "", fun));
  // builder.CreateRet(ConstantInt::get(INTTYPE, 5));

  // if (jit.addModule(std::move(mod))) {
  //   throw new std::exception();
  // }
  // auto sym = jit.lookup(name);
  // if (!sym) {
  //   throw sym.takeError();
  // }
  // return reinterpret_cast<void *>(sym->getAddress());
}