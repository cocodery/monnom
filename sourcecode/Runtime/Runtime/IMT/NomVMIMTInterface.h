#pragma once

#include "AvailableExternally.h"
#include "DLLExport.h"
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>

using namespace llvm;
using namespace Nom::Runtime;

extern llvm::Function *GetGetCallTag(llvm::Module *mod);

extern "C" DLLEXPORT const CallTagExternally *
CPP_NOM_GetCallTag(llvm::Function *fun);