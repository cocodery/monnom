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
  for (auto [tag, addr] : callTags) {
    if (tag == callTag) {
      return true;
    }
  }
  return false;
}

void *NomIMTNode::CheckTransition(const NomInterfaceCallTag *callTag) {
  auto find = transitions.find(callTag);
  if (find != transitions.end()) {
    return find->second;
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
  auto callTagSet = parent->callTags;
  callTagSet.push_back(std::make_pair(callTag, callTagFunAddr));

  auto imtNode = new NomIMTNode(imtEntry, callTagSet);
  parent->AddTransitionNode(callTag, imtNode);

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
  static IMTGraphs imtGraphs = IMTGraphs();
  auto find = imtGraphs.find(record);

  if (find != imtGraphs.end()) {
    return find->second;
  }
  auto imtGraph = new NomIMTGraph(record);
  imtGraphs[record] = imtGraph;
  return imtGraph;
}

NomIMTNode *NomIMTGraph::GetIMTNode(int imtIndex, void *imtEntry) {
  auto nodes = nodesPerEntry[imtIndex];

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

void NomIMTGraph::GenerateTransitionRelation(int imtIndex, NomIMTNode *node) {
  auto nodes = nodesPerEntry[imtIndex];
  // Implementation for generating transition relations
  auto size = node->callTags.size();
  for (auto otherNode : nodes) {
    // Ensure we are not comparing the same node
    assert(otherNode != node && "Node should not transition to itself");
    // check if satisfies the transition condition
    auto otherSize = otherNode->callTags.size();
    auto sizeDiff = otherSize - size;
    if (sizeDiff == 1) {
      // if the size difference is 1 the node can transition to the other
      // node add the transition relation from the given node to the other node
    } else if (sizeDiff == -1) {
      // if the size difference is -1 the other node can transition to the given
      // node add the transition relation from the other node to the given node
    }
  }
  // Add the new node to the list of nodes for the given imtIndex
  nodes.push_back(node);
}
} // namespace Runtime
} // namespace Nom
