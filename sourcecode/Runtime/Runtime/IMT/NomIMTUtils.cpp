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
void *CompileIMTEntryFunction(NomRecord *nomRecord, int imtIndex,
                              void *callTagFunAddr, NomIMTNode *imtNode) {
  static int transitionCount = 0;

  std::string name = "MONNOM_RT_RECORDIMT_ " + *nomRecord->GetSymbolName() +
                     "_" + std::to_string(imtIndex) + "_" +
                     std::to_string(transitionCount++);

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

  auto argCallTag = argiter;
  argiter++;
  auto varargs = makealloca(Value *, RTConfig_NumberOfVarargsArguments + 1);
  for (decltype(RTConfig_NumberOfVarargsArguments) i = 0;
       i <= RTConfig_NumberOfVarargsArguments; i++) {
    varargs[i] = argiter;
    argarr[i + 1] = argiter;
    argiter++;
  }

  if (jit.addModule(std::move(mod))) {
    throw new std::exception();
  }
  auto sym = jit.lookup(name);
  if (!sym) {
    throw sym.takeError();
  }
  return reinterpret_cast<void *>(sym->getAddress());
}
} // namespace IMTUtils
} // namespace Runtime
} // namespace Nom