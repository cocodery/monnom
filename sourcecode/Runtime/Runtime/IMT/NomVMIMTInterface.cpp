#include "NomVMIMTInterface.h"
#include "AvailableExternally.h"
#include "CastStats.h"
#include "Defs.h"
#include "NomIMTAsyncCompiler.h"
#include "NomIMTUtils.h"
#include "NomIMTransition.h"
#include "NomInterfaceCallTag.h"
#include "NomJIT.h"
#include "NomJITLight.h"
#include "NomMethod.h"
#include "NomRecord.h"
#include "RTRecord.h"
#include "RTVTable.h"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <llvm/IR/Constant.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/MathExtras.h>
#include <string>
#include <unordered_map>

using namespace llvm;
using namespace Nom::Runtime;

llvm::Function *GetIMTTransition(llvm::Module *mod) {
  // only called in record-call-tag fallback path
  Function *ret = mod->getFunction("CPP_NOM_GetIMTTransition");
  if (ret == nullptr) {
    FunctionType *funType = FunctionType::get(
        VOIDTYPE, // return type
        {INTTYPE, INTTYPE, INTTYPE, INTTYPE, INTTYPE, INTTYPE}, false);
    ret = Function::Create(funType, Function::ExternalLinkage,
                           "CPP_NOM_GetIMTTransition", mod);
  }
  return ret;
}

namespace {
struct RecordDescriptorLayout {
  size_t size;
  size_t imtOffset;
};
} // namespace

static const RecordDescriptorLayout &GetRecordDescriptorLayout() {
  static const RecordDescriptorLayout layout = [] {
    auto vtableLayout =
        GetNomJITDataLayout().getStructLayout(RTVTable::GetLLVMType());
    return RecordDescriptorLayout{
        RTRecord::GetLLVMLayout()->getSizeInBytes(),
        vtableLayout->getElementOffset(
            static_cast<unsigned>(RTVTableFields::InterfaceMethodTable))};
  }();
  return layout;
}

static inline char *ReadVTable(void *receiver) {
  return *reinterpret_cast<char **>(receiver);
}

static inline void **GetIMTArray(char *vtable) {
  return reinterpret_cast<void **>(vtable +
                                   GetRecordDescriptorLayout().imtOffset);
}

// RTVTable is at the first address of an object and normally unchanged
// let <vtable, imtIndex, newIMTEntry> to check whether exists an vtable
static void MoveToNextDescriptor(void *receiver, NomRecord *nomRecord,
                                 NomIMTGraph *imtGraph, int imtIndex,
                                 void *newIMTEntry, bool newIsMethod) {
  auto vtable = ReadVTable(receiver);
  auto nextVTable = imtGraph->FindDescriptor(vtable, imtIndex, newIMTEntry);
  if (nextVTable == nullptr) {
    auto &layout = GetRecordDescriptorLayout();
    const uintptr_t lowBits = 32;
    auto mem =
        reinterpret_cast<uintptr_t>(std::malloc(layout.size + 2 * lowBits));
    nextVTable = reinterpret_cast<char *>(
        alignTo(mem, lowBits) +
        (reinterpret_cast<uintptr_t>(vtable) & (lowBits - 1)));
    std::memcpy(nextVTable, vtable, layout.size);
    GetIMTArray(nextVTable)[imtIndex] = newIMTEntry;
    imtGraph->AddDescriptor(vtable, imtIndex, newIMTEntry, nextVTable);
    if (NomCastStats) {
      RT_NOM_STATS_IncRecordIMTCopies();
    }
  }
  *reinterpret_cast<char **>(receiver) = nextVTable;
  if (nomRecord->StartDescriptor == nullptr) {
    // initial assignment of the record structure
    nomRecord->StartDescriptor = vtable;
  }
  if (newIsMethod && nomRecord->StartDescriptor == vtable) {
    // exclude lambdas getting the record start descriptor
    // as record methods are fixed for the record structure
    nomRecord->StartDescriptor = nextVTable;
  }
}

extern "C" DLLEXPORT void
CPP_NOM_GetIMTTransition(void *callTagAddr, void *callTagFunAddr,
                         void *nomRecordAddr, void *receiver,
                         uint64_t targetKind, uint64_t targetAddr) {
  // std::cout << "Enter IMT Entry Compilcation" << std::endl;
  // nomRecordAddr for locate the IMTGraph
  auto nomRecord = reinterpret_cast<NomRecord *>(nomRecordAddr);
  // a lambda (no NomObjPtr) would reach the record call-tag when it is invoked
  // through a functional interface without lambda optimization
  if (nomRecord == nullptr) {
    return;
  }
  // link entries compiled in the background since the last fallback into
  // their graphs first so the lookup below can find them
  NomIMTAsync::InstallFinishedIMTEntries();

  // callTagAddr of the interface call-tag for get interface method index in IMT
  auto callTag = reinterpret_cast<NomInterfaceCallTag *>(callTagAddr);
  // callTagFunAddr of the interface call-tag for insert as branch condition
  auto imtIndex = callTag->GetMethod()->GetIMTIndex();
  auto imtEntry = GetIMTArray(ReadVTable(receiver))[imtIndex];

  // get imtGraph for nomRecord
  auto imtGraph = NomIMTGraph::GetIMTGraph(nomRecord);

  // std::cout << callTag->GetKey() << std::endl;
  // std::cout << callTag->GetMethod()->GetIMTIndex() << std::endl;
  // std::cout << nomRecord->GetName() << std::endl;
  // std::cout << "The " << imtIndex << "th IMT Entry of Record "
  //           << nomRecord->GetName() << " is: " << imtEntry << std::endl;

  // check if the imtGraph has the imtNode for the imtIndex
  // get the imtNode for the imtEntry under the imtIndex slot
  auto imtNode = imtGraph->GetIMTNode(imtIndex, imtEntry);

  void *newIMTEntry = nullptr;
  if (imtNode->CheckNodeValidity(callTag)) {
    // if the imtNode has the callTag, the object's entry already handles it
    // for example if a function under the imtIndex is called multiple times
    // the imtNode will be reused and the imtEntry will be the same
    // std::cout << "The callTag " << callTag->GetKey()
    //           << " is already in the imtNode and return current imtEntry "
    //           << imtEntry << std::endl;
    if (NomCastStats) {
      RT_NOM_STATS_IncIMTEntriesHit();
    }
    return;
  } else if (auto tmpEntry = imtNode->CheckTransition(callTag)) {
    // if the callTag is in the imtNode's transition chain
    // return the imtEntry of the new imtNode
    // this is how an object follows what another object of its record compiled
    // std::cout
    //     << "The callTag " << callTag->GetKey()
    //     << " is in the imtNode's transition chain, return the new imtEntry "
    //     << tmpEntry << std::endl;
    newIMTEntry = tmpEntry;
    if (NomCastStats) {
      RT_NOM_STATS_IncIMTGraphTransitions();
    }
  } else if (NomIMTAsync::IsIMTEntryPending(imtGraph, imtIndex)) {
    // an entry for this slot is still compiling
    // leave the slot alone and let this call go through dynamic dispatch
    // std::cout << "The worker thread works on the compilation" << std::endl;
    return;
  } else if (NomIMTAsync::UseAsyncIMTCompile()) {
    // compile in the background while this call goes on to dynamic dispatch
    // the entry reaches this object from the graph on a later fallback
    // std::cout << "The callTag " << callTag->GetKey()
    //           << " async compile a new imtEntry for it" << std::endl;
    IMTUtils::SetCallTagTarget(nomRecord, callTag, targetKind, targetAddr);
    NomIMTAsync::CompileIMTEntryFunctionAsync(nomRecord, imtGraph, imtIndex,
                                              imtEntry, callTag, callTagFunAddr,
                                              imtNode);
    return;
  } else {
    // if the callTag is not in the imtNode
    // the callTag is a new interface method for the imtNode
    // create a new imtEntry for the new callTag
    // std::cout << "The callTag " << callTag->GetKey()
    //           << " sync compile a new imtEntry for it" << std::endl;
    IMTUtils::SetCallTagTarget(nomRecord, callTag, targetKind, targetAddr);
    newIMTEntry = IMTUtils::CompileIMTEntryFunction(
        nomRecord, imtIndex, callTag, callTagFunAddr, imtNode);
    auto newIMTNode = NomIMTNode::CreateTransitionNode(
        imtNode, callTag, callTagFunAddr, newIMTEntry);
    imtGraph->GenerateTransitionRelation(imtIndex, newIMTNode);
    if (NomCastStats) {
      RT_NOM_STATS_IncIMTEntriesCompiled();
    }
  }
  // install the entry so later calls through the IMT reach it directly
  // std::cout << "Install new entry " << newIMTEntry << " to slot " << imtIndex
  //           << " over oringal entry " << imtEntry << std::endl;
  assert(newIMTEntry != nullptr && "new compiled IMT entry cannot be nullptr");
  if (NomCastStats) {
    RT_NOM_STATS_IncIMTEntriesInstalled();
  }
  MoveToNextDescriptor(receiver, nomRecord, imtGraph, imtIndex, newIMTEntry,
                       static_cast<DispatchTargetKind>(targetKind) ==
                           DispatchTargetKind::Method);
}
