#pragma once
#include "Defs.h"
#include <cstdint>

namespace Nom {
namespace Runtime {
// How a static field is initialized; matches TypeChecker.StaticFieldInitKind
enum class StaticFieldInitKind : unsigned char {
  None = 0,
  Int = 1,
  Float = 2,
  Bool = 3,
  Null = 4,
  String = 5
};

// A static field declared by a class (read from the class's bytecode).
// InitPayload is the Int value, the Float's bits, 0/1 for Bool, or the string
// constant (global ID); with InitKind None the field starts as the default of
// its type. Slot is the field's storage, allocated when the class is loaded
// (AllocateStaticFields) and holding the packed value.
struct NomStaticField {
  ConstantID Name;
  ConstantID Type;
  bool IsReadOnly;
  StaticFieldInitKind InitKind;
  uint64_t InitPayload;
  void **Slot = nullptr;
};
} // namespace Runtime
} // namespace Nom
