#pragma once
#include "CompileHelpers.h"
#include "NomBuilder.h"
#include "NomNameRepository.h"
#include "RTOutput.h"
#include "llvm/IR/Instructions.h"
#include <algorithm>
#include <unordered_set>

namespace Nom {
namespace Runtime {
class InstanceDictionary {
public:
  // loads the dictionary pointer stored at dictAddr
  // creating the dictionary on first use
  static llvm::Value *GenerateGetOrCreate(NomBuilder &builder,
                                          llvm::Value *dictAddr);
  static llvm::Value *GenerateLookup(NomBuilder &builder, llvm::Value *dict,
                                     llvm::Value *fieldName);
  static llvm::Value *GenerateSet(NomBuilder &builder, llvm::Value *dict,
                                  llvm::Value *fieldName, llvm::Value *value);
  // a dictionary field invoked through an interface is frozen
  static llvm::Value *GenerateFreeze(NomBuilder &builder, llvm::Value *dict,
                                     llvm::Value *fieldName);
  static llvm::Value *GenerateIsFrozen(NomBuilder &builder, llvm::Value *dict,
                                       llvm::Value *fieldName);

  template <typename MethodList, typename FieldList>
  static void
  AddMethodNameWriteGuard(NomBuilder &builder, llvm::SwitchInst *nameSwitch,
                          const MethodList &methods, const FieldList &fields,
                          const char *errorMessage) {
    llvm::Function *fun = nameSwitch->getFunction();
    llvm::BasicBlock *methodNameBlock = nullptr;
    std::unordered_set<size_t> methodNameIDs;
    for (auto meth : methods) {
      size_t nameID = NomNameRepository::Instance().GetNameID(meth->GetName());
      if (!methodNameIDs.insert(nameID).second // for overload and inheritance
          || // safety check for the same name on field and method
          std::any_of(fields.begin(), fields.end(), [&](auto field) {
            return field->GetName()->ToStdString() == meth->GetName();
          })) {
        continue;
      }
      if (methodNameBlock == nullptr) {
        methodNameBlock = llvm::BasicBlock::Create(builder->getContext(),
                                                   "writeToMethodName", fun);
        RTOutput_Fail::MakeBlockFailOutputBlock(builder, errorMessage,
                                                methodNameBlock);
      }
      nameSwitch->addCase(MakeInt<size_t>(nameID), methodNameBlock);
    }
  }
};
} // namespace Runtime
} // namespace Nom
