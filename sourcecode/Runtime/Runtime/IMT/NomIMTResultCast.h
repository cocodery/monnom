#pragma once

#include "NomIMTransition.h"
#include "NomInterfaceCallTag.h"
#include "NomRecord.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {
class Function;
class Value;
} // namespace llvm

namespace Nom {
namespace Runtime {
class NomBuilder;
class NomInterface;
namespace IMTUtils {
struct CallTagTarget;
} // namespace IMTUtils

namespace IMTResultCast {

// The interface type the receiver was cast to
// iface is null when the receiver offers nothing to specialize on
struct ReceiverTypeArgs {
  const NomInterface *iface = nullptr;
  const void *rtIface = nullptr;             // RTInterface descriptor
  llvm::SmallVector<NomTypeRef, 4> nomArgs;  // type-parameter
  llvm::SmallVector<const void *, 4> rtArgs; // runtime types
};
ReceiverTypeArgs ReadReceiverTypeArgs(void *receiver);

// Compiles the result casts the entry's branches call
void CacheIMTEntryCastFuns(const NomRecord *nomRecord,
                           const NomInterfaceCallTag *callTag,
                           const IMTCallTagSet &parentCallTags,
                           const ReceiverTypeArgs &recvTypeArgs);

// Casts a branch resultto the interface method return type
llvm::Value *GenerateBranchResultCast(NomBuilder &builder, llvm::Function *fun,
                                      const IMTUtils::CallTagTarget &target,
                                      const NomInterfaceCallTag *callTag,
                                      llvm::Value *argCallTag,
                                      llvm::Value **varargs,
                                      const ReceiverTypeArgs &recvTypeArgs,
                                      llvm::Value *result);
} // namespace IMTResultCast
} // namespace Runtime
} // namespace Nom
