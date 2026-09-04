#pragma once
#include "Defs.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include <cassert>
#include <list>
#include <llvm/IR/Constants.h>
#include <memory>
#include <unordered_map>
#include <vector>

namespace llvm {
class Module;
class Constant;
} // namespace llvm

namespace Nom {
namespace Runtime {

class NomIMTNode;

class NomIMTNode {
private:
  llvm::Constant *imt;

  std::list<NomIMTNode *> children;

public:
  NomIMTNode(llvm::Constant *_imt) : imt(_imt) {}

  llvm::Constant *GetIMT() const { return imt; }

  static void CreateIMTNode(llvm::Constant *_imt);
};
} // namespace Runtime
} // namespace Nom
