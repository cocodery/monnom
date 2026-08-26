#pragma once
#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Metadata.h"
namespace Nom {
namespace Runtime {
llvm::MDNode *getStructDescriptorInvariantNode();
llvm::MDNode *getGeneralInvariantNode();
llvm::MDNode *GetLikelyFirstBranchMetadata();
llvm::MDNode *GetLikelySecondBranchMetadata();
llvm::MDNode *
GetBranchWeightsForBlocks(llvm::ArrayRef<llvm::BasicBlock *> blocks);
llvm::MDNode *GetBranchWeights(llvm::ArrayRef<uint64_t> weights);
} // namespace Runtime
} // namespace Nom