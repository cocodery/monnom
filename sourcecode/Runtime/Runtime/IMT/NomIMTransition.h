#pragma once
#include "Defs.h"
#include "NomInterfaceCallTag.h"
#include "NomRecord.h"
#include "RTCompileConfig.h"
#include "RTConfig.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include <cassert>
#include <list>
#include <llvm/IR/Constants.h>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

namespace llvm {
class Module;
class Constant;
} // namespace llvm

namespace Nom {
namespace Runtime {

class NomIMTNode;
class NomIMTGraph;

using IMTCallTagSet = std::set<const NomInterfaceCallTag *>;
using IMTGraphs = std::unordered_map<const NomRecord *, NomIMTGraph *>;
using IMTNodes = std::vector<NomIMTNode *>;

class NomIMTNode {
  friend class NomIMTGraph;

private:
  const int imtIndex;
  void *imtEntry;
  const IMTCallTagSet callTags;
  std::unordered_map<const NomInterfaceCallTag *, NomIMTNode *> transitions;

public:
  NomIMTNode(const NomIMTNode &) = delete;
  NomIMTNode &operator=(const NomIMTNode &) = delete;
};

class NomIMTGraph {
private:
  const NomRecord *record; // each record holds one graph
  IMTNodes roots;          // root node - per IMT entry
  IMTNodes nodes;          // all imt node

public:
  NomIMTGraph(const NomIMTGraph &) = delete;
  NomIMTGraph &operator=(const NomIMTGraph &) = delete;

  explicit NomIMTGraph(const NomRecord *record);

  static IMTGraphs &NomIMTGraphsforAll();
  static NomIMTGraph *GetIMTGraph(NomRecord *record);
};

} // namespace Runtime
} // namespace Nom
