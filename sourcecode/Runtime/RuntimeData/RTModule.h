#pragma once

#include "NomJIT.h"
#include "NomModule.h"
#include "NomTypeRegistry.h"
#include "RTClass.h"
#include "llvm/IR/Module.h"
#include <forward_list>

namespace Nom {
namespace Runtime {
class RTModule {
private:
  std::forward_list<RTClass> classes;
  std::forward_list<RTClassType> classTypes;
  std::unique_ptr<llvm::Module> theModule;
  static std::forward_list<void *> &structRecords();
  static std::forward_list<void *> &lambdaRecords();
  static std::forward_list<RuntimeInstantiationDictionary *> &
  instantiationDictionaries();

public:
  static void ClearCaches();
  RTModule(NomModule *mod);
  ~RTModule();
};

} // namespace Runtime
} // namespace Nom
