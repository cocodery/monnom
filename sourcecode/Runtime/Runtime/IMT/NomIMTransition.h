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

using IMTCallTagSet = std::list<std::pair<const NomInterfaceCallTag *, void *>>;
using TransitionChain =
    std::unordered_map<const NomInterfaceCallTag *, NomIMTNode *>;

class NomIMTNode {
  friend class NomIMTGraph;

private:
  void *imtEntry;              // hold IMT entry function and locate node
  IMTCallTagSet callTags;      // for build new IMT entry function
  TransitionChain transitions; // for find next node if exists

public:
  NomIMTNode(const NomIMTNode &) = delete;
  NomIMTNode &operator=(const NomIMTNode &) = delete;
  explicit NomIMTNode(void *imtEntry);
  explicit NomIMTNode(void *imtEntry, IMTCallTagSet callTags);

  bool CheckNodeValidity(const NomInterfaceCallTag *callTag);

  void *CheckTransition(const NomInterfaceCallTag *callTag);
  void AddTransitionNode(const NomInterfaceCallTag *callTag, NomIMTNode *node);

  static NomIMTNode *CreateTransitionNode(NomIMTNode *parent,
                                          const NomInterfaceCallTag *callTag,
                                          void *callTagFunAddr, void *imtEntry);
};

/* --------------------------------------------------------------------------*/
/* --------------------------------------------------------------------------*/
/* --------------------------------------------------------------------------*/

using IMTGraphs = std::unordered_map<const NomRecord *, NomIMTGraph *>;
using IMTNodes = std::vector<std::list<NomIMTNode *>>;

class NomIMTGraph {
private:
  const NomRecord *record; // each record holds one graph
  IMTNodes nodesPerEntry;  // all imt node under per slot

public:
  NomIMTGraph(const NomIMTGraph &) = delete;
  NomIMTGraph &operator=(const NomIMTGraph &) = delete;
  explicit NomIMTGraph(const NomRecord *record);

  static NomIMTGraph *GetIMTGraph(NomRecord *record);

  NomIMTNode *GetIMTNode(int imtIndex, void *imtEntry);

  void GenerateTransitionRelation(int imtIndex, NomIMTNode *node);
};

} // namespace Runtime
} // namespace Nom
