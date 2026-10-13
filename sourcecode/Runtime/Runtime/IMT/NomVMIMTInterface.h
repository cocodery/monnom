#pragma once

#include "AvailableExternally.h"
#include "DLLExport.h"
#include "NomIMTUtils.h"
#include "RTVTable.h"
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>

using namespace llvm;
using namespace Nom::Runtime;

extern llvm::Function *GetIMTTransition(llvm::Module *mod);
extern "C" DLLEXPORT void *CPP_NOM_GetIMTTransition(void *callTagAddr,
                                                    void *callTagFunAddr,
                                                    void *nomRecordAddr,
                                                    void **imtArray);
