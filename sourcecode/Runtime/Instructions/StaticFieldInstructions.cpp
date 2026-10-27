#include "StaticFieldInstructions.h"
#include "BoolClass.h"
#include "CompileEnv.h"
#include "CompileHelpers.h"
#include "FloatClass.h"
#include "IntClass.h"
#include "NomAlloc.h"
#include "NomClass.h"
#include "NomConstants.h"
#include "NomJIT.h"
#include "NomType.h"
#include "NullClass.h"
#include "TypeOperations.h"
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace llvm;
using namespace std;
namespace Nom {
namespace Runtime {

// names of the initializer functions emitted since the last module was added
// to the JIT (see RunPendingStaticFieldInitializers)
static std::vector<std::string> &PendingStaticFieldInitializers() {
  static std::vector<std::string> pending;
  return pending;
}

// the field's initial value, packed (REFTYPE)
static llvm::Value *GenerateInitialValue(NomBuilder &builder,
                                         llvm::Module &mod,
                                         const NomStaticField &field,
                                         NomTypeRef fieldType) {
  auto nullObject = [&]() {
    return builder->CreatePointerCast(
        NomNullObject::GetInstance()->GetLLVMElement(mod), REFTYPE);
  };
  switch (field.InitKind) {
  case StaticFieldInitKind::Int:
    return PackInt(builder, MakeInt<int64_t>((int64_t)field.InitPayload));
  case StaticFieldInitKind::Float: {
    double value;
    std::memcpy(&value, &field.InitPayload, sizeof(double));
    return PackFloat(builder, ConstantFP::get(FLOATTYPE, value));
  }
  case StaticFieldInitKind::Bool:
    return PackBool(builder, MakeUInt(1, field.InitPayload != 0 ? 1 : 0));
  case StaticFieldInitKind::Null:
    return nullObject();
  case StaticFieldInitKind::String:
    return builder->CreatePointerCast(
        NomConstants::GetString(field.InitPayload)->getObject(mod), REFTYPE);
  case StaticFieldInitKind::None:
  default:
    // without an initializer: 0, 0.0, false or null, by the field's type
    if (fieldType->IsSubtype(NomIntClass::GetInstance()->GetType(), false)) {
      return PackInt(builder, MakeInt<int64_t>(0));
    }
    if (fieldType->IsSubtype(NomFloatClass::GetInstance()->GetType(), false)) {
      return PackFloat(builder, ConstantFP::get(FLOATTYPE, 0.0));
    }
    if (fieldType->IsSubtype(NomBoolClass::GetInstance()->GetType(), false)) {
      return PackBool(builder, MakeUInt(1, 0));
    }
    return nullObject();
  }
}

static llvm::Constant *SlotConstant(void **slot) {
  return ConstantExpr::getIntToPtr(MakeInt<intptr_t>((intptr_t)slot),
                                   REFTYPE->getPointerTo());
}

void AllocateStaticFields(const NomClass *cls, llvm::Module &mod) {
  auto &fields = cls->GetStaticFields();
  if (fields.empty()) {
    return;
  }
  // one initializer function per class, storing every field's initial value
  std::string initName = "MONNOM_STATICINIT_" + *cls->GetSymbolName();
  Function *initFun = Function::Create(
      FunctionType::get(Type::getVoidTy(LLVMCONTEXT), false),
      GlobalValue::LinkageTypes::ExternalLinkage, initName, &mod);
  initFun->setCallingConv(CallingConv::C);
  NomBuilder builder;
  builder->SetInsertPoint(BasicBlock::Create(LLVMCONTEXT, "start", initFun));
  // static field types cannot use type variables
  NomSubstitutionContextMemberContext nscmc(cls);
  for (auto &field : fields) {
    if (field.Slot != nullptr) {
      throw new std::exception(); // the class was loaded twice
    }
    // uncollectable memory is scanned, so the value in the slot stays alive
    field.Slot = (void **)gcalloc_uncollectable(sizeof(void *));
    *field.Slot = nullptr;
    MakeStore(builder,
              GenerateInitialValue(builder, mod, field,
                                   NomConstants::GetType(&nscmc, field.Type)),
              SlotConstant(field.Slot), AtomicOrdering::NotAtomic);
  }
  builder->CreateRetVoid();
  PendingStaticFieldInitializers().push_back(initName);
}

void RunPendingStaticFieldInitializers() {
  auto &pending = PendingStaticFieldInitializers();
  for (auto &name : pending) {
    auto symbol = NomJIT::Instance().lookup(name);
    if (!symbol) {
      throw symbol.takeError();
    }
    reinterpret_cast<void (*)()>(symbol->getAddress())();
  }
  pending.clear();
}

llvm::Value *AStaticFieldInstruction::GetSlotPointer() const {
  // the class is loaded through this instruction's constant dependencies
  const NomClass *cls = NomConstants::GetClass(OwnerClass)->GetClass();
  auto name = NomConstants::GetString(FieldName)->GetText()->ToStdString();
  for (auto &field : cls->GetStaticFields()) {
    if (NomConstants::GetString(field.Name)->GetText()->ToStdString() == name) {
      if (field.Slot == nullptr) {
        throw new std::exception(); // the class has not been loaded
      }
      return SlotConstant(field.Slot);
    }
  }
  throw new std::exception(); // the class has no such static field
}

NomTypeRef AStaticFieldInstruction::GetFieldType(CompileEnv *env) const {
  NomSubstitutionContextMemberContext nscmc(env->Context);
  return NomConstants::GetType(&nscmc, FieldType);
}

void AStaticFieldInstruction::PrintField(bool resolve) const {
  NomConstants::PrintConstant(OwnerClass, resolve);
  cout << ".";
  NomConstants::PrintConstant(FieldName, resolve);
  cout << " : ";
  NomConstants::PrintConstant(FieldType, resolve);
}

void AStaticFieldInstruction::FillFieldDependencies(
    NOM_CONSTANT_DEPENCENCY_CONTAINER &result) const {
  result.push_back(FieldName);
  result.push_back(OwnerClass);
  result.push_back(FieldType);
}

ReadStaticFieldInstruction::ReadStaticFieldInstruction(
    const RegIndex reg, const ConstantID fieldName, const ConstantID ownerClass,
    const ConstantID fieldType)
    : NomValueInstruction(reg, OpCode::ReadStaticField),
      AStaticFieldInstruction(fieldName, ownerClass, fieldType) {}

void ReadStaticFieldInstruction::Compile(NomBuilder &builder, CompileEnv *env,
                                         int lineno) {
  NomTypeRef fieldType = GetFieldType(env);
  // the field was initialized when its class was loaded, so this is a plain
  // load (not invariant: other code may write the field at any time)
  Value *value = MakeLoad(builder, GetSlotPointer(), "staticField",
                          AtomicOrdering::NotAtomic);
  // in the representation of the field's type, like instance field reads
  RegisterValue(
      env, NomValue(EnsurePackedUnpacked(builder, value,
                                         fieldType->GetLLVMType()),
                    fieldType));
}

void ReadStaticFieldInstruction::Print(bool resolve) {
  cout << "ReadStaticField ";
  PrintField(resolve);
  cout << " -> #" << std::dec << WriteRegister << "\n";
}

void ReadStaticFieldInstruction::FillConstantDependencies(
    NOM_CONSTANT_DEPENCENCY_CONTAINER &result) {
  FillFieldDependencies(result);
}

WriteStaticFieldInstruction::WriteStaticFieldInstruction(
    const RegIndex valueReg, const ConstantID fieldName,
    const ConstantID ownerClass, const ConstantID fieldType)
    : NomInstruction(OpCode::WriteStaticField),
      AStaticFieldInstruction(fieldName, ownerClass, fieldType),
      ValueRegister(valueReg) {}

void WriteStaticFieldInstruction::Compile(NomBuilder &builder, CompileEnv *env,
                                          int lineno) {
  NomValue value = (*env)[ValueRegister];
  MakeStore(builder, EnsurePackedUnpacked(builder, value, REFTYPE),
            GetSlotPointer(), AtomicOrdering::NotAtomic);
}

void WriteStaticFieldInstruction::Print(bool resolve) {
  cout << "WriteStaticField ";
  PrintField(resolve);
  cout << " from #" << std::dec << ValueRegister << "\n";
}

void WriteStaticFieldInstruction::FillConstantDependencies(
    NOM_CONSTANT_DEPENCENCY_CONTAINER &result) {
  FillFieldDependencies(result);
}
} // namespace Runtime
} // namespace Nom
