#include "NomVMIMTInterface.h"
#include "AvailableExternally.h"
#include "Defs.h"
#include "NomInterfaceCallTag.h"
#include <exception>
#include <iostream>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <unordered_map>

using namespace llvm;
using namespace Nom::Runtime;

static std::unordered_map<intptr_t, intptr_t> &GetFunCallTagMap() {
  static std::unordered_map<intptr_t, intptr_t> funCallTagMap;
  return funCallTagMap;
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

llvm::Function *GetReadFunCallTag(llvm::Module *mod) {
  Function *ret = mod->getFunction("CPP_NOM_ReadFunCallTag");
  if (ret == nullptr) {
    FunctionType *funType = FunctionType::get(INTTYPE, {INTTYPE}, false);
    ret = Function::Create(funType, Function::ExternalLinkage,
                           "CPP_NOM_ReadFunCallTag", mod);
  }
  return ret;
}

extern "C" DLLEXPORT void CPP_NOM_WriteFunCallTag(intptr_t funAddr,
                                                  intptr_t callTagAddr) {
  auto &map = GetFunCallTagMap();
  auto it = map.find(funAddr);
  if (it == map.end()) { // for first time check
    // std::cout << "Write - Fun: " << funAddr << " | CallTag: " << callTagAddr
    //           << std::endl;
    map[funAddr] = callTagAddr;
  } else if (it->second != callTagAddr) { // check value as key inserted before
    throw std::exception();
  }
  // key exists && the values are equal
}

extern "C" DLLEXPORT intptr_t CPP_NOM_ReadFunCallTag(intptr_t funAddr) {
  auto &map = GetFunCallTagMap();
  auto it = map.find(funAddr);
  if (it != map.end()) {
    auto callTagAddr = it->second;
    // std::cout << "Read  - Fun: " << funAddr << " | CallTag: " << callTagAddr
    //           << std::endl;
    return callTagAddr;
  }
  throw std::exception();
}