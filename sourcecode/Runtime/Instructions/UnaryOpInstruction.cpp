#include "UnaryOpInstruction.h"
#include "BoolClass.h"
#include "CompileHelpers.h"
#include "FloatClass.h"
#include "IntClass.h"
#include "NomClassType.h"
#include "NomDynamicType.h"
#include "NomValue.h"
#include "RTClass.h"
#include "RTOutput.h"
#include "RefValueHeader.h"
#include <iostream>
#include <stdio.h>

using namespace std;
using namespace llvm;
namespace Nom {
namespace Runtime {
const char *GetUnaryOpName(UnaryOperation op) {
  switch (op) {
  case UnaryOperation::Negate:
    return "-";
  case UnaryOperation::Not:
    return "!";
  }
}
UnaryOpInstruction::UnaryOpInstruction(const UnaryOperation op,
                                       const RegIndex arg, const RegIndex reg)
    : NomValueInstruction(reg, OpCode::UnaryOp), Operation(op), Arg(arg) {}
UnaryOpInstruction::~UnaryOpInstruction() {}
void UnaryOpInstruction::Compile(NomBuilder &builder, CompileEnv *env,
                                 int lineno) {
  switch (this->Operation) {
  case UnaryOperation::Negate: {
    BasicBlock *currentBlock = builder->GetInsertBlock();
    Function *fun = currentBlock->getParent();
    BasicBlock *refObjectBlock = nullptr, *intBlock = nullptr,
               *floatBlock = nullptr, *boolBlock = nullptr;
    Value *intValue, *floatValue, *boolValue;

    NomValue val = ((*env)[Arg]);

    RefValueHeader::GenerateRefOrPrimitiveValueSwitch(
        builder, val, &refObjectBlock, nullptr, nullptr, true, &intBlock,
        &intValue, &floatBlock, &floatValue, &boolBlock, &boolValue);
    if (refObjectBlock != nullptr) {
      RTOutput_Fail::MakeBlockFailOutputBlock(
          builder, "Tried to negate non-numeric value!", refObjectBlock);
    }
    if (boolBlock != nullptr) {
      RTOutput_Fail::MakeBlockFailOutputBlock(
          builder, "Tried to numerically negate boolean value!", boolBlock);
    }

    BasicBlock *mergeBlock = nullptr;
    PHINode *mergePHI = nullptr;
    Value *mergeResult = nullptr;
    NomTypeRef singleCaseType = nullptr;
    int cases = (intBlock != nullptr) + (floatBlock != nullptr);
    if (cases > 1) {
      mergeBlock = BasicBlock::Create(LLVMCONTEXT, "negateMerge", fun);
      builder->SetInsertPoint(mergeBlock);
      mergePHI = builder->CreatePHI(REFTYPE, 2, "negateOut");
      mergeResult = mergePHI;
    }

    if (intBlock != nullptr) {
      builder->SetInsertPoint(intBlock);
      auto negatedInt = builder->CreateNeg(intValue, "negatedInt");
      if (mergePHI != nullptr) {
        mergePHI->addIncoming(PackInt(builder, negatedInt),
                              builder->GetInsertBlock());
        builder->CreateBr(mergeBlock);
      } else {
        singleCaseType = NomIntClass::GetInstance()->GetType();
        mergeResult = negatedInt;
      }
    }

    if (floatBlock != nullptr) {
      builder->SetInsertPoint(floatBlock);
      auto negatedFloat = builder->CreateFNeg(floatValue, "negatedFloat");
      if (mergePHI != nullptr) {
        mergePHI->addIncoming(PackFloat(builder, negatedFloat),
                              builder->GetInsertBlock());
        builder->CreateBr(mergeBlock);
      } else {
        singleCaseType = NomFloatClass::GetInstance()->GetType();
        mergeResult = negatedFloat;
      }
    }

    if (mergePHI) {
      builder->SetInsertPoint(mergeBlock);
      RegisterValue(env,
                    NomValue(mergeResult, &NomDynamicType::Instance(), false));
    } else {
      RegisterValue(env, NomValue(mergeResult, singleCaseType, false));
    }
    return; // missing return in the case
  }
  case UnaryOperation::Not: {
    RegisterValue(
        env, NomValue(PackBool(builder, builder->CreateNot(EnsureUnpackedBool(
                                            builder, env, (*env)[Arg]))),
                      NomBoolClass::GetInstance()->GetType(), false));
    return;
  }
  }
}
void UnaryOpInstruction::Print(bool resolve) {
  cout << "UnaryOp";
  cout << " " << GetUnaryOpName(Operation) << " #";
  cout << std::dec << Arg;
  cout << " -> #" << std::dec << WriteRegister;
  cout << "\n";
}
void UnaryOpInstruction::FillConstantDependencies(
    NOM_CONSTANT_DEPENCENCY_CONTAINER &result) {}
} // namespace Runtime
} // namespace Nom