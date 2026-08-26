#include "Defs.h"
#include "BoolClass.h"
#include "CastInstruction.h"
#include "GlobalNameAddressLookupList.h"
#include "NomAlloc.h"
#include "NomClassType.h"
#include "NomJIT.h"
#include "ObjectHeader.h"
#include "RefValueHeader.h"
#include "StringClass.h"
#include "llvm/Support/raw_os_ostream.h"
#include <iostream>

int isInWarmup() { return _isInWarmup; }

namespace Nom {
namespace Runtime {
llvm::PointerType *BytecodeTypes::GetRefType() {
  // static llvm::Type * refType =
  // (llvm::StructType::create(Nom::Runtime::TheContext)->getPointerTo());
  static llvm::PointerType *refType =
      RefValueHeader::GetLLVMType()->getPointerTo();
  return refType;
}

llvm::PointerType *BytecodeTypes::GetTypeType() {
  static llvm::PointerType *typeType =
      RTTypeHead::GetLLVMType()->getPointerTo();
  return typeType;
}

std::vector<std::string> globalsForAddressLookup;
void RegisterGlobalForAddressLookup(std::string name) {
  globalsForAddressLookup.push_back(name);
}
std::vector<std::string> &GetGlobalsForAddressLookup() {
  return globalsForAddressLookup;
}
} // namespace Runtime
} // namespace Nom

extern "C" DLLEXPORT void RT_NOM_PRINT_STORE(void *val, void *addr) {
  std::cout << "(";
  std::cout << std::hex << (intptr_t)val;
  std::cout << "->";
  std::cout << std::hex << (intptr_t)addr;
  std::cout << ")";
  std::cout.flush();
}

extern "C" DLLEXPORT void RT_NOM_PRINT_LOAD(void *val, void *addr) {
  std::cout << "(";
  std::cout << std::hex << (intptr_t)val;
  std::cout << "<-";
  std::cout << std::hex << (intptr_t)addr;
  std::cout << ")";
  std::cout.flush();
}