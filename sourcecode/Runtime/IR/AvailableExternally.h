#pragma once
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Module.h"
#include <cstdint>
#include <exception>
#include <iostream>
#include <llvm/IR/Constant.h>
#include <unordered_map>

namespace Nom {
namespace Runtime {
template <typename T> class AvailableExternally {
private:
  mutable llvm::Module *mainModule = nullptr;

public:
  AvailableExternally() {}
  ~AvailableExternally() {}

  T *GetLLVMElement(llvm::Module &mod) const {
    T *ret = this->findLLVMElement(mod);
    if (ret == nullptr) {
      if (mainModule == nullptr) {
        mainModule = &mod;
        ret = this->createLLVMElement(
            mod, llvm::GlobalValue::LinkageTypes::ExternalLinkage);
      } else {
        ret = this->createLLVMElement(
            mod, llvm::GlobalValue::LinkageTypes::AvailableExternallyLinkage);
      }
    }
    return ret;
  }

protected:
  virtual T *
  createLLVMElement(llvm::Module &mod,
                    llvm::GlobalValue::LinkageTypes linkage) const = 0;
  virtual T *findLLVMElement(llvm::Module &mod) const = 0;
  // T *findMainLLVMElement() const {
  //   if (mainModule != nullptr) {
  //     return findLLVMElement(mainModule);
  //   }
  //   return nullptr;
  // }
  llvm::Module *GetMainModule() const { return mainModule; }
};

class CallTagExternally : public AvailableExternally<llvm::Constant> {
private:
  static auto &tagFunMap() {
    static std::unordered_map<llvm::Constant *, const CallTagExternally *>
        tagFunMap;
    return tagFunMap;
  }

public:
  CallTagExternally() {}
  ~CallTagExternally() {}

  static const CallTagExternally *GetCallTag(llvm::Constant *fun) {
    auto &&ret = tagFunMap().find(fun);
    if (ret != tagFunMap().end()) {
      return ret->second;
    }
    throw std::exception();
  }

  llvm::Constant *GetLLVMElement(llvm::Module &mod) const {
    llvm::Constant *ret = this->findLLVMElement(mod);
    if (ret == nullptr) {
      auto mainModule = GetMainModule();
      if (mainModule == nullptr) {
        mainModule = &mod;
        ret = this->createLLVMElement(
            mod, llvm::GlobalValue::LinkageTypes::ExternalLinkage);
      } else {
        ret = this->createLLVMElement(
            mod, llvm::GlobalValue::LinkageTypes::AvailableExternallyLinkage);
      }
      if (tagFunMap().find(ret) == tagFunMap().end()) {
        tagFunMap()[ret] = this;
      } else {
        throw std::exception();
      }
    }
    return ret;
  }
};

} // namespace Runtime
} // namespace Nom
