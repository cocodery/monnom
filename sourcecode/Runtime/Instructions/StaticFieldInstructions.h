#pragma once
#include "NomInstruction.h"
#include "NomTypeDecls.h"
#include "NomValueInstruction.h"

namespace llvm {
class Module;
}

namespace Nom {
namespace Runtime {
class NomClass;

// Static fields belong to their class. When a class is loaded (its module is
// compiled by RTModule), AllocateStaticFields allocates a slot for each of its
// static fields and emits an initializer function into the module, which
// stores each field's initial value (its initializer, or 0, 0.0, false or null
// by its type). RTModule runs these functions with
// RunPendingStaticFieldInitializers right after adding the module to the JIT,
// before any of its code runs: the initial values can be runtime objects
// (booleans, null, strings, boxed numbers) that only have addresses then. A
// slot is GC-scanned memory kept for the whole process and holds the packed
// value, so reading a static field is a single load.
void AllocateStaticFields(const NomClass *cls, llvm::Module &mod);
void RunPendingStaticFieldInitializers();

// The parts both static field instructions share: the field, by owning class
// and name, and its type.
class AStaticFieldInstruction {
public:
  const ConstantID FieldName;
  const ConstantID OwnerClass;
  const ConstantID FieldType;
  AStaticFieldInstruction(const ConstantID fieldName,
                          const ConstantID ownerClass,
                          const ConstantID fieldType)
      : FieldName(fieldName), OwnerClass(ownerClass), FieldType(fieldType) {}

protected:
  // the field's slot, allocated when its class was loaded, as an LLVM constant
  // of type REFTYPE*
  llvm::Value *GetSlotPointer() const;
  NomTypeRef GetFieldType(CompileEnv *env) const;
  void PrintField(bool resolve) const;
  void FillFieldDependencies(NOM_CONSTANT_DEPENCENCY_CONTAINER &result) const;
};

class ReadStaticFieldInstruction : public NomValueInstruction,
                                   public AStaticFieldInstruction {
public:
  ReadStaticFieldInstruction(const RegIndex reg, const ConstantID fieldName,
                             const ConstantID ownerClass,
                             const ConstantID fieldType);
  ~ReadStaticFieldInstruction() override {}

  virtual void Compile(NomBuilder &builder, CompileEnv *env,
                       int lineno) override;
  virtual void Print(bool resolve = false) override;
  virtual void
  FillConstantDependencies(NOM_CONSTANT_DEPENCENCY_CONTAINER &result) override;
};

class WriteStaticFieldInstruction : public NomInstruction,
                                    public AStaticFieldInstruction {
public:
  const RegIndex ValueRegister;
  WriteStaticFieldInstruction(const RegIndex valueReg,
                              const ConstantID fieldName,
                              const ConstantID ownerClass,
                              const ConstantID fieldType);
  ~WriteStaticFieldInstruction() override {}

  virtual void Compile(NomBuilder &builder, CompileEnv *env,
                       int lineno) override;
  virtual void Print(bool resolve = false) override;
  virtual void
  FillConstantDependencies(NOM_CONSTANT_DEPENCENCY_CONTAINER &result) override;
};
} // namespace Runtime
} // namespace Nom
