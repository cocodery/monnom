#include "NomVMIMTInterface.h"
#include "AvailableExternally.h"
#include "Defs.h"
#include "NomIMTAsyncCompiler.h"
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
  // install entries compiled in the background since the last fallback
  // first, so the slot read below is current
  IMTUtils::InstallFinishedIMTEntries();

  // callTagAddr for get interface method index in IMT
  auto callTag = reinterpret_cast<NomInterfaceCallTag *>(callTagAddr);
  // callTagFunAddr for insert as branch condition
  // nomRecordAddr for locate the IMTGraph
  auto nomRecord = reinterpret_cast<NomRecord *>(nomRecordAddr);
  // imtArray for get the current IMT entry function address
  auto imtIndex = callTag->GetMethod()->GetIMTIndex();
  auto imtEntry = *(imtArray + imtIndex);

  // std::cout << nomRecord->GetName() << ": imtArrary Addr " << imtArray
  //           << std::endl;
  // for (int i = 0; i < IMTsize; i++) {
  //   std::cout << "The " << i << "th IMT Entry of Record "
  //             << nomRecord->GetName() << " is: " << *(imtArray + i)
  //             << std::endl;
  // }

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
    // std::cout << "The callTag " << callTag->GetKey()
    //           << " is already in the imtNode, return the current imtEntry "
    //           << imtEntry << std::endl;
    newIMTEntry = imtEntry;
  } else if (auto tmpEntry = imtNode->CheckTransition(callTag)) {
    // if the callTag is in the imtNode's transition chain
    // return the imtEntry of the new imtNode
    // std::cout
    //     << "The callTag " << callTag->GetKey()
    //     << " is in the imtNode's transition chain, return the new imtEntry "
    //     << tmpEntry << std::endl;
    newIMTEntry = tmpEntry;
  } else if (IMTUtils::IsIMTEntryPending(imtGraph, imtIndex)) {
    // an entry for this slot is still compiling; leave the slot alone and
    // let this call go through dynamic dispatch. A fallback after the
    // compile finishes installs the entry, and a later one extends it
    // with this call-tag if it doesn't cover it.
    return nullptr;
  } else if (IMTUtils::UseAsyncIMTCompile()) {
    // compile in the background while this call goes on to dynamic dispatch
    IMTUtils::CompileIMTEntryFunctionAsync(nomRecord, imtGraph, imtIndex,
                                           callTag, callTagFunAddr, imtNode,
                                           imtArray);
    return nullptr;
  } else {
    // if the callTag is not in the imtNode
    // the callTag is a new interface method for the imtNode
    // create a new imtEntry for the new callTag
    // std::cout << "The callTag " << callTag->GetKey()
    //           << " is not in the imtNode, create a new imtEntry for it"
    //           << std::endl;
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