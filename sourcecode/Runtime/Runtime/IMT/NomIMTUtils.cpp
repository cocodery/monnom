#include "NomIMTUtils.h"
#include "CallingConvConf.h"
#include "IMT.h"
#include "NomBuilder.h"
#include "NomJIT.h"
#include "NomRecordMethod.h"
#include "RTCompileConfig.h"
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
#include <memory>
#include <string>

namespace Nom {
namespace Runtime {
namespace IMTUtils {
void *CompileIMTEntryFunction(NomRecord *nomRecord, int imtIndex) {
  static int transitionCount = 0;

  std::string name = "MONNOM_RT_RECORDIMT_TRANSITON_" +
                     std::to_string(transitionCount) +
                     *nomRecord->GetSymbolName() + "_" + std::to_string(imtIndex);

  auto &jit = NomJIT::Instance();
  auto mod = std::make_unique<llvm::Module>(name, LLVMCONTEXT);
  mod->setDataLayout(jit.getDataLayout());

  llvm::Function *fun = Function::Create(
      GetIMTFunctionType(), llvm::Function::ExternalLinkage, name, mod.get());
  fun->setCallingConv(NOMCC);
  BasicBlock *startBlock = BasicBlock::Create(LLVMCONTEXT, "", fun);
  NomBuilder builder;
  builder->SetInsertPoint(startBlock);

  auto argiter = fun->arg_begin();
  auto argarr = makealloca(Value *, 2 + RTConfig_NumberOfVarargsArguments);
  argarr[0] = argiter;

  auto callTag = argiter;
  argiter++;
  auto varargs = makealloca(Value *, RTConfig_NumberOfVarargsArguments + 1);
  for (decltype(RTConfig_NumberOfVarargsArguments) i = 0;
       i <= RTConfig_NumberOfVarargsArguments; i++) {
    varargs[i] = argiter;
    argarr[i + 1] = argiter;
    argiter++;
  }

  for (auto &meth : nomRecord->Methods) {
    if (meth->GetIMTIndex() == imtIndex) {

    }
  }

  return nullptr;
}
} // namespace IMTUtils
} // namespace Runtime
} // namespace Nom