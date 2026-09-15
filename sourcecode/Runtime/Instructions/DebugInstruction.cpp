#include "DebugInstruction.h"
#include "NomConstants.h"
#include "NomVMInterface.h"
#include <iostream>

using namespace std;
namespace Nom {
namespace Runtime {

DebugInstruction::DebugInstruction(const ConstantID message)
    : NomInstruction(OpCode::Debug), messageConstant(message) {}

DebugInstruction::~DebugInstruction() {}

void DebugInstruction::Compile(NomBuilder &builder, CompileEnv *env,
                               int lineno) {
  GenerateLLVMDebugPrint(
      builder, env->Module,
      NomConstants::GetString(messageConstant)->GetText()->ToStdString());
}
void DebugInstruction::Print(bool resolve) {
  cout << "DEBUG ";
  NomConstants::PrintConstant(messageConstant, resolve);
  cout << "\n";
}
void DebugInstruction::FillConstantDependencies(
    NOM_CONSTANT_DEPENCENCY_CONTAINER &result) {
  result.push_back(messageConstant);
}
} // namespace Runtime
} // namespace Nom
