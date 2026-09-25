#pragma once

#include "AvailableExternally.h"
#include "DLLExport.h"
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>

using namespace llvm;
using namespace Nom::Runtime;

extern llvm::Function *GetWriteFunCallTag(llvm::Module *mod);
extern llvm::Function *GetReadFunCallTag(llvm::Module *mod);

extern "C" DLLEXPORT void CPP_NOM_WriteFunCallTag(intptr_t funAddr,
                                                  intptr_t callTagAddr);
extern "C" DLLEXPORT intptr_t CPP_NOM_ReadFunCallTag(intptr_t funAddr);