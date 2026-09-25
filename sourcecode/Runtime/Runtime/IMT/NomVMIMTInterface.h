#pragma once

#include "AvailableExternally.h"
#include "DLLExport.h"
#include "RTVTable.h"
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>

using namespace llvm;
using namespace Nom::Runtime;

extern llvm::Function *GetReadFunCallTag(llvm::Module *mod);
extern llvm::Function *GetWriteFunCallTag(llvm::Module *mod);

extern "C" DLLEXPORT intptr_t CPP_NOM_ReadFunCallTag(intptr_t funAddr);
extern "C" DLLEXPORT void CPP_NOM_WriteFunCallTag(intptr_t funAddr,
                                                  intptr_t callTagAddr);

extern llvm::Function *GetIMTTransition(llvm::Module *mod);
extern "C" DLLEXPORT llvm::Function *
CPP_NOM_GetIMTTransition(void *vtable, int64_t callTagAddr,
                         void *callTagFunAddr, void *imtArray);