#include "ErrorInstruction.h"
#include "CompileEnv.h"
#include "CompileHelpers.h"
#include "RTOutput.h"
#include "StringClass.h"
#include <iostream>

using namespace std;
using namespace llvm;
namespace Nom {
namespace Runtime {
ErrorInstruction::ErrorInstruction(RegIndex reg)
    : NomInstruction(OpCode::Error), Register(reg) {}
void ErrorInstruction::Compile(NomBuilder &builder, CompileEnv *env,
                               int lineno) {
  // a String message is printed; any other value gives the generic message
  NomValue message = (*env)[Register];
  if (message->getType() == REFTYPE &&
      message.GetNomType()->IsSubtype(
          NomStringClass::GetInstance()->GetType(), false)) {
    auto failFun = RTOutput_Fail::GetStringMessageElement(*env->Module);
    builder
        ->CreateCall(failFun, builder->CreatePointerCast(message, POINTERTYPE))
        ->setCallingConv(failFun->getCallingConv());
  } else {
    static const char *generic_errorMessage = "ERROR!";
    builder
        ->CreateCall(RTOutput_Fail::GetLLVMElement(*env->Module),
                     GetLLVMPointer(generic_errorMessage))
        ->setCallingConv(
            RTOutput_Fail::GetLLVMElement(*env->Module)->getCallingConv());
  }
  CreateDummyReturn(builder, env->Function);
}
void ErrorInstruction::Print(bool resolve) {
  cout << "ERROR <- " << std::dec << Register << "\n";
}
void ErrorInstruction::FillConstantDependencies(
    NOM_CONSTANT_DEPENCENCY_CONTAINER &result) {}
} // namespace Runtime
} // namespace Nom