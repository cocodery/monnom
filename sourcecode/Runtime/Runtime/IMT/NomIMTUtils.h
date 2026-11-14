#pragma once

#include "NomIMTResultCast.h"
#include "NomIMTransition.h"
#include "NomInterfaceCallTag.h"
#include "NomRecord.h"
#include "RTVTable.h"
#include <memory>

namespace llvm {
class Module;
} // namespace llvm

namespace Nom {
namespace Runtime {
namespace IMTUtils {
// Prefix of the names of IMT entries, their result casts and their modules
extern const char IMTEntryPrefix[];

struct CallTagTarget {
  DispatchTargetKind kind;
  NomRecordMethod *method;     // only for DispatchTargetKind::Method
  const NomRecordField *field; // only for DispatchTargetKind::Field
  void *methodAddr;
};
const CallTagTarget &GetCallTagTarget(const NomRecord *nomRecord,
                                      const NomInterfaceCallTag *callTag);
void SetCallTagTarget(const NomRecord *nomRecord,
                      const NomInterfaceCallTag *callTag, uint64_t kind,
                      uint64_t addr);

std::unique_ptr<llvm::Module>
BuildIMTEntryModule(NomRecord *nomRecord, int imtIndex,
                    const NomInterfaceCallTag *callTag, void *callTagFunAddr,
                    const IMTCallTagSet &parentCallTags,
                    const IMTResultCast::ReceiverTypeArgs &recvTypeArgs);
// Whether mod was built by BuildIMTEntryModule
bool IsIMTEntryModule(const llvm::Module &mod);
// Optimizes a module built by BuildIMTEntryModule
void OptimizeIMTEntryModule(llvm::Module &mod);

void *
CompileIMTEntryFunction(NomRecord *nomRecord, int imtIndex,
                        const NomInterfaceCallTag *callTag,
                        void *callTagFunAddr, NomIMTNode *imtNode,
                        const IMTResultCast::ReceiverTypeArgs &recvTypeArgs);
} // namespace IMTUtils
} // namespace Runtime
} // namespace Nom