#pragma once
#include "CompileEnv.h"
#include "NomTypeDecls.h"
#include <memory>
#include <vector>

namespace Nom {
namespace Runtime {
class NomSubstitutionContext;
class NomSignature {
public:
  NomTypeRef ReturnType;
  std::vector<NomTypeRef> ArgumentTypes;
  std::vector<NomTypeParameterRef> TypeArguments;
  NomSignature();
  NomSignature(const std::vector<NomTypeParameterRef> &&typeArgs,
               const std::vector<NomTypeRef> &&argTypes,
               const NomTypeRef returnType);

  ~NomSignature();

  NomSignature Substitute(const NomSubstitutionContext *context) const;

  bool SatisfiesArguments(const NomSignature &other,
                          bool optimistic = false) const;

  bool Satisfies(const NomSignature &other, bool optimistic = false) const;

  // Returns true iff any argument or return type could possibly contain a
  // primitive but isn't guaranteed to
  bool HasPrimitiveUncertainty() const;
};
} // namespace Runtime
} // namespace Nom