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

llvm::Function *GetIMTTransition(llvm::Module *mod) {
  Function *ret = mod->getFunction("CPP_NOM_GetIMTTransition");
  if (ret == nullptr) {
    FunctionType *funType = FunctionType::get(
        POINTERTYPE, {INTTYPE, INTTYPE, INTTYPE, INTTYPE}, false);
    ret = Function::Create(funType, Function::AvailableExternallyLinkage,
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

  // the record has no method for this call-tag (e.g. a field holding a
  // lambda): no IMT entry can handle it, so the caller falls back to
  // dynamic dispatch
  if (IMTUtils::LookupCallTagMethodAddr(nomRecord, callTag) == nullptr) {
    return nullptr;
  }

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
  } else if (auto tmpEntry = imtNode->CheckTransition(callTag)) {
    // if the callTag is in the imtNode's transition chain
    // return the imtEntry of the new imtNode
    newIMTEntry = tmpEntry;
  } else {
    // if the callTag is not in the imtNode
    // the callTag is a new interface method for the imtNode
    // create a new imtEntry for the new callTag
    newIMTEntry = IMTUtils::CompileIMTEntryFunction(
        nomRecord, imtIndex, callTag, callTagFunAddr, imtNode);
    auto newIMTNode = NomIMTNode::CreateTransitionNode(
        imtNode, callTag, callTagFunAddr, newIMTEntry);
    imtGraph->GenerateTransitionRelation(imtIndex, newIMTNode);
  }

  // install the entry so later calls through the IMT reach it directly
  imtArray[imtIndex] = newIMTEntry;

  return newIMTEntry;
}