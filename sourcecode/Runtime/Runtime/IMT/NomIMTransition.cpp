#include "NomIMTransition.h"

namespace Nom {
namespace Runtime {

namespace NomIMTNodePool {

// A map from imt constant to NomIMTNode
std::unordered_map<llvm::Constant *, std::shared_ptr<NomIMTNode>> node_pool;

// get a root node for imt which is constructed at construction time
std::shared_ptr<NomIMTNode> get(llvm::Constant *_imt) {
  if (node_pool.find(_imt) != node_pool.end()) {
    return node_pool[_imt];
  }
  assert("unexist IMT");
}

// set a root node for imt at construction time
void set(llvm::Constant *_imt, std::shared_ptr<NomIMTNode> node) {
  node_pool[_imt] = node;
}
} // namespace NomIMTNodePool

void NomIMTNode::CreateIMTNode(llvm::Constant *_imt) {
  auto node = std::shared_ptr<NomIMTNode>(new NomIMTNode(_imt));
  NomIMTNodePool::set(_imt, node);
}

} // namespace Runtime
} // namespace Nom
