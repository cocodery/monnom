#include "LoadNullConstantInstruction.h"
#include "NomClassType.h"
#include "NullClass.h"
#include <iostream>

using namespace std;

namespace Nom {
namespace Runtime {
void LoadNullConstantInstruction::Compile(NomBuilder &builder, CompileEnv *env,
                                          int lineno) {
  RegisterValue(
      env, NomValue(NomNullObject::GetInstance()->GetLLVMElement(
                        *builder->GetInsertBlock()->getParent()->getParent()),
                    NomNullClass::GetInstance()->GetType(), false));
}
void LoadNullConstantInstruction::Print(bool resolve) {
  cout << "LoadNull ";
  cout << " -> #" << std::dec << WriteRegister;
  cout << "\n";
}
void LoadNullConstantInstruction::FillConstantDependencies(
    NOM_CONSTANT_DEPENCENCY_CONTAINER &result) {}
} // namespace Runtime
} // namespace Nom