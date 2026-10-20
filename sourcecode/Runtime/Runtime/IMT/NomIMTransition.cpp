#include "NomIMTransition.h"
#include "RTCompileConfig.h"
#include <algorithm>
#include <cmath>
#include <exception>

namespace Nom {
namespace Runtime {

NomIMTNode::NomIMTNode(void *imtEntry) : imtEntry(imtEntry) {
  callTags = IMTCallTagSet();
  transitions = TransitionChain();
}

NomIMTNode::NomIMTNode(void *imtEntry, IMTCallTagSet callTags)
    : imtEntry(imtEntry), callTags(callTags) {
  transitions = TransitionChain();
}

bool NomIMTNode::CheckNodeValidity(const NomInterfaceCallTag *callTag) {
  // check if the callTag exists in the callTags list of current IMTNode
  for (auto [tag, addr] : callTags) {
    if (tag == callTag) {
      return true;
    }
  }
  return false;
}

void *NomIMTNode::CheckTransition(const NomInterfaceCallTag *callTag) {
  // check if the callTag exists in the transitions map of the current node
  // if it exists return the imtEntry of the next node otherwise return nullptr
  auto find = transitions.find(callTag);
  if (find != transitions.end()) {
    return find->second->imtEntry;
  }
  return nullptr;
}

void NomIMTNode::AddTransitionNode(const NomInterfaceCallTag *callTag,
                                   NomIMTNode *node) {
  transitions[callTag] = node;
}

NomIMTNode *NomIMTNode::CreateTransitionNode(NomIMTNode *parent,
                                             const NomInterfaceCallTag *callTag,
                                             void *callTagFunAddr,
                                             void *imtEntry) {
  // create a new node with the given imtEntry and callTag
  // add the new node to the parent's transition map with the given callTag
  auto callTagSet = parent->callTags;
  callTagSet.push_back(std::make_pair(callTag, callTagFunAddr));

  auto imtNode = new NomIMTNode(imtEntry, callTagSet);

  return imtNode;
}

/* --------------------------------------------------------------------------*/
/* --------------------------------------------------------------------------*/
/* --------------------------------------------------------------------------*/

NomIMTGraph::NomIMTGraph(const NomRecord *record) : record(record) {
  // create a vector of list which holds all nodes for each imtIndex slot
  nodesPerEntry = IMTNodes(IMTsize, std::list<NomIMTNode *>());
}

NomIMTGraph *NomIMTGraph::GetIMTGraph(NomRecord *record) {
  // return the imtGraph for the given record if it exists
  // otherwise create a new one and store it in the record
  if (record->IMTGraph == nullptr) {
    record->IMTGraph = new NomIMTGraph(record);
  }
  return record->IMTGraph;
}

NomIMTNode *NomIMTGraph::GetIMTNode(int imtIndex, void *imtEntry) {
  // get the list of nodes for the given imtIndex
  // check if the list is empty if so create a new node and add it to the list
  // if the list is not empty check if the imtEntry exists in the list
  // if it exists return the node otherwise throw an exception
  auto &nodes = nodesPerEntry[imtIndex];

  if (nodes.empty()) {
    // if the list is empty, create a new node and add it to the list
    // such that the new node is the first node in the list
    // the node is also the root node for this imtIndex slot
    auto node = new NomIMTNode(imtEntry);
    nodesPerEntry[imtIndex].push_back(node);
    return node;
  }

  for (auto node : nodes) {
    if (node->imtEntry == imtEntry) {
      return node;
    }
  }
  throw std::exception();
}

const NomInterfaceCallTag *NomIMTGraph::FindDiffCallTag(const NomIMTNode *from,
                                                        const NomIMTNode *to) {
  // return the only callTag in the to node's callTags but not in the from
  // node's callTags, such that from can transition to to with that callTag
  // return nullptr if to's callTags is not from's callTags plus one callTag

  // actually guaranteed
  assert(to->callTags.size() == from->callTags.size() + 1);
  // find the only difference for the two sets
  const NomInterfaceCallTag *diff = nullptr;
  for (auto [toTag, _] : to->callTags) {
    auto found = std::find_if(
        from->callTags.begin(), from->callTags.end(),
        [toTag = toTag](const auto &pair) { return pair.first == toTag; });
    if (found == from->callTags.end()) {
      if (diff != nullptr) {
        // more than one callTag differs, so no single transition exists
        return nullptr;
      }
      diff = toTag;
    }
  }
  return diff;
}

void NomIMTGraph::GenerateTransitionRelation(int imtIndex, NomIMTNode *node) {
  // build transition relations between the given node and all the nodes
  // under the imtIndex slot, then add the given node to the slot
  auto &nodes = nodesPerEntry[imtIndex];
  auto size = static_cast<long>(node->callTags.size());
  for (auto otherNode : nodes) {
    // Ensure we are not comparing the same node
    assert(otherNode != node && "Node should not transition to itself");
    // check if satisfies the transition condition
    auto otherSize = static_cast<long>(otherNode->callTags.size());
    auto sizeDiff = otherSize - size;
    if (sizeDiff == 1) {
      // if the size difference is 1 the node can transition to the other
      // node add the transition relation from the given node to the other node
      auto callTag = FindDiffCallTag(node, otherNode);
      if (callTag != nullptr && node->CheckTransition(callTag) == nullptr) {
        node->AddTransitionNode(callTag, otherNode);
      }
    } else if (sizeDiff == -1) {
      // if the size difference is -1 the other node can transition to the given
      // node add the transition relation from the other node to the given node
      auto callTag = FindDiffCallTag(otherNode, node);
      if (callTag != nullptr &&
          otherNode->CheckTransition(callTag) == nullptr) {
        otherNode->AddTransitionNode(callTag, node);
      }
    }
  }
  // Add the new node to the list of nodes for the given imtIndex
  nodes.push_back(node);
}
} // namespace Runtime
} // namespace Nom
