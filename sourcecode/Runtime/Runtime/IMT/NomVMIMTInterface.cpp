#include "NomVMIMTInterface.h"
#include "AvailableExternally.h"
#include "Defs.h"
#include "NomInterfaceCallTag.h"
#include <iostream>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>

using namespace llvm;
using namespace Nom::Runtime;

llvm::Function *GetGetCallTag(llvm::Module *mod) {
  Function *ret = mod->getFunction("CPP_NOM_GetCallTag");
  if (ret == nullptr) {
    FunctionType *funType =
        FunctionType::get(POINTERTYPE, {POINTERTYPE}, false);

    ret = Function::Create(funType, Function::ExternalLinkage,
                           "CPP_NOM_GetCallTag", mod);
  }
  return ret;
}

extern "C" DLLEXPORT const CallTagExternally *
CPP_NOM_GetCallTag(llvm::Function *fun) {
  auto call_tag = dynamic_cast<const NomInterfaceCallTag *>(
      CallTagExternally::GetCallTag(fun));
  return call_tag;
}