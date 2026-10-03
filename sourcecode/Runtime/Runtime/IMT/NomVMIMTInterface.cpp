#include "NomVMIMTInterface.h"
#include "AvailableExternally.h"
#include "Defs.h"
#include "NomIMTUtils.h"
#include "NomIMTransition.h"
#include "NomInterfaceCallTag.h"
#include "NomJIT.h"
#include "NomMethod.h"
#include "NomRecord.h"
#include <cstdint>
#include <exception>
#include <iostream>
#include <llvm/IR/Constant.h>
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
        POINTERTYPE, {INTTYPE, INTTYPE, INTTYPE, INTTYPE}, false);
    ret = Function::Create(funType, Function::ExternalLinkage,
                           "CPP_NOM_GetIMTTransition", mod);
  }
  return ret;
}

extern "C" DLLEXPORT void *CPP_NOM_GetIMTTransition(void *callTagAddr,
                                                    void *callTagFunAddr,
                                                    void *nomRecordAddr,
                                                    void **imtArray) {
  // callTagAddr for get interface method index in IMT
  auto callTag = reinterpret_cast<NomInterfaceCallTag *>(callTagAddr);
  // callTagFunAddr for insert as branch condition
  // nomRecordAddr for locate the IMTGraph
  auto nomRecord = reinterpret_cast<NomRecord *>(nomRecordAddr);
  // imtArray for get the current IMT entry function address
  auto imtIndex = callTag->GetMethod()->GetIMTIndex();
  auto imtEntry = *(imtArray + imtIndex);

  // std::cout << callTag->GetKey() << std::endl;
  // std::cout << callTag->GetMethod()->GetIMTIndex() << std::endl;
  // std::cout << nomRecord->GetName() << std::endl;
  // std::cout << "The " << imtIndex << "th IMT Entry of Record "
  //           << nomRecord->GetName() << " is: " << imtEntry << std::endl;

  // get imtGraph for nomRecord
  auto imtGraph = NomIMTGraph::GetIMTGraph(nomRecord);
  // check if the imtGraph has the imtNode for the imtIndex
  // get the imtNode for the imtEntry under the imtIndex slot
  auto imtNode = imtGraph->GetIMTNode(imtIndex, imtEntry);

  void *newIMTEntry = nullptr;
  if (imtNode->CheckNodeValidity(callTag)) {
    // if the imtNode has the callTag, return the current imtEntry
    // for example if a function under the imtIndex is called multiple times
    // the imtNode will be reused and the imtEntry will be the same
    newIMTEntry = imtEntry;
  } else {
    // if the callTag is not in the imtNode
    // the callTag is a new interface method for the imtNode
    // create a new imtEntry for the new callTag
    newIMTEntry = IMTUtils::CompileIMTEntryFunction(nomRecord, imtIndex,
                                                    callTagFunAddr, imtNode);
    auto newIMTNode = NomIMTNode::CreateTransitionNode(
        imtNode, callTag, callTagFunAddr, newIMTEntry);
    imtGraph->GenerateTransitionRelation(imtIndex, newIMTNode);
  }

  return newIMTEntry;

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