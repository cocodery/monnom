#include "NomIMTransition.h"
#include "RTCompileConfig.h"
#include <algorithm>
#include <exception>

namespace Nom {
namespace Runtime {

NomIMTGraph::NomIMTGraph(const NomRecord *record) : record(record) {
  for (auto i = 0; i < IMTsize; i++) {
    // roots.push_back()
  }
}

IMTGraphs &NomIMTGraph::NomIMTGraphsforAll() {
  static IMTGraphs imtGraphs = IMTGraphs();
  return imtGraphs;
}

NomIMTGraph *NomIMTGraph::GetIMTGraph(NomRecord *record) {
  auto &&imtGraphs = NomIMTGraphsforAll();
  auto find = imtGraphs.find(record);

  if (find != imtGraphs.end()) {
    return find->second;
  }

  auto imtGraph = new NomIMTGraph(record);

  return imtGraph;
}
} // namespace Runtime
} // namespace Nom
