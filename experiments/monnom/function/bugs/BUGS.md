# MonNom bug fixes for review

These bugs came up while porting the GTP benchmarks to MonNom (`gtp-benchmark-monnom/`). Each one has a test function in `BugTests.mn`, named in the table below.

**Status (2026-10-28):**
- Committed: C1–C9, R1-R15.
- Open: G1 (generic instance methods). It has a test, `Test_GenericMethod`, but no fix yet.

## Contents of this folder

| Path | Contents |
|---|---|
| `BugTests.mn`, `BugTests.mnp` | One test function per bug |
| `run_test.sh` | Compiles and runs the tests |
| `BUGS.md` | This file |

The reference fixes for R1–R14 are still in `bugfixes/` at the repository root: `bugfixes/runtime.patch` (`git diff` of `sourcecode/Runtime`) and `bugfixes/files/` (the fixed versions of the changed runtime files). The compiler patch for C1–C9 was removed after those fixes were committed.

## Running the tests

```bash
T=experiments/monnom/function/bugs/run_test.sh
$T                                         # all tests in one process, -o3
$T Test_Alignment                          # one test only
$T Test_Alignment -- -o0                   # one test, other nom flags
$T -- -o3 --trans                          # all tests, other nom flags
NOMC="dotnet <old>/nomc.dll" $T Test_IfAssign   # another compiler
```

The script works from any directory. It finds `setpath.sh` by searching upwards from its own folder.

The runtime compiles every method of `Main` when it loads the class, so one runtime bug in any test crashes the whole run. A compile error anywhere, such as the open G1 bug, also stops the whole file. With a test name, `run_test.sh` deletes every other test's `# BEGIN Test_X` … `# END Test_X` block before compiling. Use that mode to see each bug on its own.

Each test uses only the construct it is about. Results go through `if`/`else`, which avoids Bool arguments (bug R5). The tests also avoid `++` (bug R10) and calls with more than two arguments (bug R2), except in the tests for those bugs. MonNom `==` on strings compares references, so the string test prints a `<name>: COMPARE` line, followed by the actual text and then the expected text, one line each. `run_test.sh` compares the two lines and prints PASS or FAIL.

To apply all reference runtime fixes at once: `git apply bugfixes/runtime.patch`, then rebuild the runtime. Most of them are already in the tree, so apply single files with `--include=<path>` instead.

## Results

| Test | Bug | Original | Fixed |
|---|---|---|---|
| Test_IfAssign | C1 | FAIL got `1 1` (want `2 11`) | PASS |
| Test_ElseIf | C2 | segfault | PASS |
| Test_ShortCircuit | C3 | FAIL got 4 evaluations (want 2) | PASS |
| Test_NestedClosureCapture | C4 | FAIL got 22 (want 52) | PASS |
| Test_ClosureInRecordInitializer | C5 | abort (`char const*` thrown) | PASS |
| Test_ClosureReadsClassField | C6 | abort (`std::exception*`) | PASS |
| Test_ClosureReadsRecordField | C7 | invalid IR ("Could not verify") | PASS |
| Test_RecordFieldInitializerCall | C8 | abort ("Expected method, but did not encounter method marker") | PASS |
| Test_GenericFieldType | C9 | compile error ("A does not have a method ToString") | PASS |
| Test_Alignment | R1 | abort | PASS |
| Test_ManyArguments | R2 | abort (`std::exception*`) | PASS |
| Test_DynamicManyArguments | R3 | abort | PASS |
| Test_LoaderLibraryKey | R4 | abort | PASS |
| Test_BoolArgument | R5 | abort (`std::exception*`) | PASS |
| Test_LambdaArgumentToTypedMethod | R6 | abort (`std::exception*`) | PASS |
| Test_BitOps | R7 | "Invalid operation on given operands!" | PASS |
| Test_Power | R8 | "Invalid operation on given operands!" | PASS |
| Test_UnaryMinus | R9 | segfault | PASS |
| Test_StringConcat | R10, R11 | abort (`NomString const*` thrown) | PASS |
| Test_FloatEqualsDyn | R12 | invalid IR ("Instruction does not dominate all uses") | PASS |
| Test_InterfaceExtends | R13 | abort (`std::exception*`) | PASS |
| Test_GenericInstantiation | R14 | abort (`NomString const*` thrown) | PASS |
| Test_BareReturn | R15 | compiler crash (`NullReferenceException` in `VisitReturnStmt`) | PASS |
| Test_GenericMethod | G1 | compile error ("U is not a subtype of Int") | not fixed yet |

- **Original** means the original runtime for R1–R14 and the original compiler for C1–C9, with each test run alone.
- **Fixed** means both reference patches applied. With them, the first 22 tests pass under `-o0`, `-o3`, `-o3 --trans` and `-o3 --trans --sync`.
- `Test_BitOps` now also checks `<<` and `>>` (see R7). With the original compiler it fails earlier, with the parse error `extraneous input '<'`.
- Several R tests also depend on another R fix: R1, R3 and R4 need R2's argument-array fixes before they can show their own bug.

---

## Compiler bugs (C#), in `compiler.patch`

### C1: a local assigned inside an `if` keeps its old value after the `if`
- **Symptom:** after `x = 1; if (x < 5) { x = 2; }`, `x` is still 1. A loop that updates a variable inside an `if` also loses the update, because its back edge carries the stale register.
- **Cause:** `MergePhiNode.MergeVariableCopy.FinalizeMerges` (`TypeChecker/Instructions/PhiNode.cs`) creates the phi register in its private field `reg`, but never assigns it to `CurrentRegister`. `env.MergeIn(mergephi)` then copies the pre-branch register back into the variable. The phi is emitted, but nothing reads it. `LoopPhiNode` does not have this bug, because it wraps its register.
- **Fix:** set `CurrentRegister = reg;` after creating the phi register.

### C2: `elseif` conditions are never evaluated
- **Symptom:** any `elseif` crashes when the runtime compiles the method: a `CondBranch` reads a register that was never written.
- **Cause:** in `VisitIfStmt` (`TypeChecker/CodeTransformer.cs`), the loop over `ElseIfs` computes `guard` but never adds the guard's instructions to `instructions`. The first guard is added correctly.
- **Fix:** `instructions.AddRange(guard);` after computing each `elseif` guard.

### C3: `&&` and `||` do not short-circuit
- **Symptom:** the right operand is always evaluated, along with its side effects. For example, `i < n && a.Get(i) > 0` evaluates `a.Get(i)` even when `i >= n`.
- **Cause:** `VisitBinOpExpr` compiles `And`/`Or` as a plain `BinOpInstruction` on two values that have both already been evaluated.
- **Fix:** a new `TransformShortCircuit` lowers `a && b` to `t = a; if (t) { t = b; }` and `a || b` to `t = a; if (!t) { t = b; }`. It reuses the same `MergePhiNode`/`TargetPhiNode` construction as `VisitIfStmt`, with a hidden temporary declared in a fresh `ScopeTransformEnvironment` around the current environment. That works in any context, including struct initializers, which do not allow new locals.

### C4: a closure inside a closure reads the outer closure's captures from its own closure object
- **Symptom:** in `(m) => () => p * 10 + m`, the inner closure reads `p` wrongly: the test gets 22 instead of 52.
- **Cause:** `LambdaTransformEnvironment`'s indexer captures a variable only if `ParentEnv.GetLocalVariable` finds it. In the outer lambda, `p` is a captured *field*, so that lookup fails and the code falls through to `return ParentEnv[ident]`. That returns the outer lambda's `LambdaFieldReference`, which the inner body then reads from its own `#0`.
- **Fix:** when `ParentEnv[ident]` returns a `LambdaFieldReference` or a `LocalVariableRef`, capture it again: add a closure field whose source is that reference, read in the parent environment when the closure is created. Separately, `Compile` enumerated the lazy `argResults` twice, once for registers and once for instructions. Reading a captured field emits a new instruction each time, so the closure got the wrong register. It is now materialized with `.ToList()`.

### C5: a closure inside a record field initializer inside a closure reads captures from the wrong frame
- **Symptom:** `(m) => new (dyn payload = () => p * 10 + m) {…}` crashes or reads garbage.
- **Cause:** same place as C4. `StructInitializerTransformEnvironment` returns its own captures as `LocalVariableRef`s to the initializer's argument registers, and the inner lambda used those registers in its own frame.
- **Fix:** covered by the C4 rule, which also captures `LocalVariableRef` results.

### C6: a closure in a class method reads the class's fields from the closure object
- **Symptom:** `() => V + 1` or `() => this.V + 2` inside a class method crashes when compiled.
- **Causes:**
  - **`this`:** in a method environment, `this` is a `LocalVariableRef` to register 0. It came back from `ParentEnv[ident]` and was used as is, so inside the lambda it meant the closure.
  - **Implicit fields:** `FindLocalField` returns a `CheckedFieldReference` whose receiver is fixed to the method's `ThisRegister`.
- **Fixes:**
  - **`this`:** captured by the C4 rule.
  - **Implicit fields:** a `CheckedFieldReference` with an instruction-less receiver becomes `ATopLevelCodeTransformEnvironment.CheckedThisFieldReference`. That resolves `this` again in the environment that reads it, which inside a lambda is the captured `this`, so reads and writes stay live.

### C7: a closure in a record method reads the record's fields from the closure object
- **Symptom:** `new (dyn v = p) { fun Get() { return () => v + 1; } }` produces invalid IR or reads the wrong value.
- **Cause:** a record method returns a `StructFieldReference`, which reads from implicit register 0.
- **Fix:** a read-only instance field reference is captured by value: it cannot change, so the snapshot is exact. A mutable instance field becomes the new `ThisRelativeFieldReference`. On each access it reads the captured `this` and goes through the normal field lookup, which for records is a dynamic field access. Writes inside the closure are visible outside, as the test checks with `42 51 52`.

### C8: a record field initialized with a call or closure corrupts the bytecode
- **Symptom:** the runtime fails to load the file: "Expected method, but did not encounter method marker".
- **Cause:** `StructFieldRep.WriteByteCode` (`Bytecode/Reps/StructFieldRep.cs`) writes `InitializerInstructions.Count()`. A call instruction (`ACallInstruction`), and closure creation is one, writes its arguments as separate `Argument` instructions, so it occupies `InstructionCount` bytecode instructions. Every other writer sums `InstructionCount`.
- **Fix:** write the sum of `InstructionCount`, still as `int32`, which is what the runtime reads. The C# reader `StructFieldRep.Read` now advances by each instruction's `InstructionCount`, to match.

### C9: field types of generic class instances ignore the type arguments
- **Symptom:** for `Pair<Int, String> p`, `p.First` has type `A`, so `p.First.ToString()` fails to type-check.
- **Cause:** `CheckedFieldReference.Type` returns `FieldSpec.Type` unsubstituted. `FieldLookupVisitor.VisitClassType` creates the reference without the class type's `Substitutions`.
- **Fix:** `CheckedFieldReference` takes an optional, already-substituted type. `VisitClassType` passes `field.Type.Substitute(type.Substitutions)`, and reads and writes both use it.

---

## Runtime bugs (C++), in `runtime.patch`

### R1: every load and store claims 64-byte alignment
- **Symptom:** a call with 6 arguments to a record through an interface segfaults at `-o1` and above, and works at `-o0`. gdb shows `vmovaps %ymm0, 0x8(%rsp)`: an aligned 32-byte vector store to an address that is not 32-byte aligned.
- **Cause:** `MakeStore` and `MakeLoad` (`Common/CompileHelpers.cpp`, six places) set `llvm::Align(DataLayout.getTypeSizeInBits(T))`. `llvm::Align` is in bytes, so every 8-byte value claimed 64-byte alignment. Once the optimizer merges neighbouring stores into vector stores, that crashes. This affects every object field access, so it is a likely cause of many of the benchmarks' structural-configuration segfaults.
- **Fix:** use `GetNomJITDataLayout().getABITypeAlign(T)`.

### R2: calls with more arguments than the varargs slots
With the default `RTConfig_NumberOfVarargsArguments = 2`, an IMT-style call passes `(calltag, receiver, arg1, slot)`. When there are more arguments, the last slot points to an array holding the rest.
- **Writer:** `CallCheckedInstanceMethod.cpp`, in both the raw-invoke and the interface path.
  - It allocated `N - V` entries instead of `N + 1 - V`.
  - It stored `argarr[j+1]` at `array[j]` for `j >= 2`. That loses one argument and writes past the end.
  - Fix: store `argarr[V + k]` at `array[k]`.
- **Readers** (`NomClass.cpp` ×4, `NomRecord.cpp`, `NomInterfaceCallTag.cpp`, `Runtime/IMT/NomIMTUtils.cpp`):
  - They did `GEP(varargs[V], k)` on `i8*` (`POINTERTYPE`), which steps `k` bytes and loads one `i8`. Fix: cast to `i8**` first.
  - They loaded with `!invariant.group`, but the interface call-tag rewrites the array entries in place, so plain loads are used now.
- **Lambdas** (`IR/Data/NomLambda.cpp`, `Common/CompileEnv.cpp` has the `// TODO: implement`): the lambda body never loaded the overflow arguments, so argument 3 was a null register. They are now loaded from the array in `NomLambdaBody::createLLVMElement`.

### R3: dynamic calls with more than two arguments
`CallDispatchBestMethod.cpp` had three problems:
- **Address space:** `CreateAlloca(POINTERTYPE, dispargcount - 2)` passes the plain integer as the address-space argument, so the alloca ended up in `addrspace(1)` and the IR was invalid.
- **Array size:** the array was one entry too small.
- **Out of bounds:** the array pointer was written to `argbuf[V + 2]`, one past the end of `argbuf`.

The fix allocates `MakeInt32(dispargcount - (V - 1))` entries and stores the array pointer in `argbuf[V + 1]`, which is what the dispatcher reads.

### R4: the loader's library table is keyed by a dangling pointer
- **Symptom:** a "class not found" error (a thrown `NomString`) that comes and goes with unrelated changes, such as the number of method arguments, or project names like `Forth`, `Fsm`, `Lnm` and `Acquire`.
- **Cause:** `Loader::GetLibrary` (`Parse/Loader.cpp`) stores `libraries[name] = lib`. The `StringDict` key is the caller's `std::string*`, and `NomClassConstant::EnsureClassLoaded` passes a stack local. Later lookups hash and compare through the dangling pointer.
- **Fix:** `libraries[lib->Name] = lib`. `lib->Name` points into the manifest, which lives for the whole process.

### R5: a comparison passed as a `Bool` argument
- **Symptom:** `F(x < 5)` throws while compiling.
- **Cause:** in both `GenerateFunctionCall` overloads (`Common/CompileHelpers.cpp`), a `REFTYPE` (boxed) parameter accepts `INTTYPE`, `FLOATTYPE` or pointers, but not a raw `i1`.
- **Fix:** box it with `PackBool`.

### R6: lambda arguments passed on to methods with `Int`/`Float` parameters
- **Symptom:** `(dyn x) => Main.Half(x)`, where `Half` takes a `Float`, throws in `GenerateFunctionCall`.
- **Cause:** a lambda's argument registers are the raw `i8*` IMT arguments. `NomLambdaBody::createLLVMElement` called `RTCast::GenerateCast` on each one and discarded the result.
- **Fix:** each argument is converted to `REFTYPE`, cast-checked, and stored in the register in the declared type's representation, unboxed for `Int`/`Float`.

### R7: `&`, `|`, `^`, `<<` and `>>` on `Int`
- **Cause:** `BinOpInstruction::CompileIntInt` had no cases for them, so it returned a poison value, which the caller turns into "Invalid operation on given operands!". `<<` and `>>` also failed earlier, in the parser: the lexer's `<<`/`>>` tokens are commented out, because a `>>` token would break nested type arguments like `A<B<C>>`.
- **Fix:**
  - Add `CreateAnd`, `CreateOr` and `CreateXor`.
  - For the shifts, `Parser/Special/NominalGradual.g4` gets two `expr` alternatives that match two adjacent `LANGLE`/`RANGLE` tokens. They sit between `+`/`-` and `&`, so `a + 1 << 2` is `(a + 1) << 2`. The type checker already sent them to `BinOpInstruction` for `Int`.
  - `CompileIntInt` implements `<<` as `CreateShl` and `>>` as `CreateAShr`, which keeps the sign. Only the low 6 bits of the shift amount are used, as in Java, because LLVM shifts by 64 or more are poison.

### R8: `**`
- **Cause:** there was no `Power` case for `Int` or `Float`.
- **Fix:** `Int ** Int` is exponentiation by squaring in IR, and a negative exponent gives 0, as integer division would. `Float ** Float` uses the `llvm.pow` intrinsic. Mixed operands already go through `CompileFloatFloat`.

### R9: unary minus
`UnaryOpInstruction.cpp`, the `Negate` case, had four problems:
- **Fallthrough:** with no `return`, it fell through into the `Not` case and registered a second value. That caused the segfault.
- **Wrong float branch:** the float branch set the insert point to `intBlock` and applied an integer `CreateNeg` to `intValue`.
- **Unboxed values in a boxed phi:** unboxed values were added to a `REFTYPE` phi.
- **Missing merge for `dyn`:** setting `cases = -1` for the error cases suppressed the merge block. For `dyn` values that left the int block unterminated.

Fix: return after the case; use `floatBlock`, `floatValue` and `CreateFNeg`; box before the phi; base the merge on the number of numeric cases.

### R10: string `++` is not implemented
- **Cause:** the compiler turns `a ++ b` into a call to `String.Concat(String)`, but the runtime's `NomStringClass` has only `Print`.
- **Fix:** add `Concat` to `NomStringClass::GetInstance()` (`Library/StringClass.cpp`, `.h`), backed by `LIB_NOM_String_Concat_1`, which copies the left string and appends the right one.

### R11: `ToString` strings in unscanned GC memory
- **Cause:** `LIB_NOM_Int_ToString_1` and the `Float` equivalent allocate the `NomString` with `gcalloc_atomic`. Its character buffer is a separate GC allocation (`BoehmAtomicAllocator`), and an `Int` can print to 20 characters, which is more than fit inline. Because the object is not scanned, the GC can free the buffer while the string is still in use.
- **Fix:** `gcalloc`. `Concat` also uses `gcalloc`. A collection-timing bug cannot be shown reliably, so `Test_StringConcat` only exercises it.

### R12: `Float == dyn`
- **Symptom:** "Instruction does not dominate all uses".
- **Cause:** in `BinOpInstruction::Compile`'s equality path, in the block `leftPrimitiveFloatRightPrimitiveInt`, the code converted `rightPrimitiveFloat` instead of `rightPrimitiveInt`.
- **Fix:** convert `rightPrimitiveInt`.

### R13: interface `extends`
- **Symptom:** loading an interface that inherits methods throws.
- **Cause:** `NomInterface::GetCheckReturnTypeFunction` (`IR/Data/NomInterface.cpp`) had `throw … // TODO: implement` for methods whose container is a super-interface.
- **Fix:** take the inherited method's return type through this interface's instantiation of the super-interface (`instantiations`), and cast against this interface's type parameters.

### R14: generic code inside generic classes
- **R14a, call-site argument types substituted twice:** `NomMethodConstant::GetMethod` (`IR/NomConstants.cpp`) resolved the call's argument types with the receiver's type arguments (`&nsc`). They are the call site's types, already in the caller's terms. So `ArrayList<IBox<T>>.Add(IBox<T>)` looked for `Add(IBox<IBox<T>>)`, and that threw the method name. They are now resolved with the caller's `context`.
- **R14b, runtime instantiation of generic types that contain type variables:** `NomClassType::GenerateRTInstantiation` (`IR/Types/NomClassType.cpp`) called `NOM_RTInstantiateClass`, which is never declared in any module, so `getFunction` returned null and it crashed. The C function behind it also treats runtime descriptors as C++ `NomClass*` and returns `GetRTElement()`, which throws. The new code:
  1. builds the environment's type arguments into a `TypeArgumentListStack` frame, with variable `i` at `types[-1-i]` as the subtyping code reads them;
  2. calls the existing `RT_NOM_EnsureUniquedType` on the static type constant;
  3. returns the canonical runtime instance.
- **R14c, infinite loop:** in `NomInterface::GetMethod`, the loop that copies a method's type arguments after the context's never incremented `i`. That branch is only reached by calls to generic instance methods, which G1 still blocks.

### R15: a bare `return;`
R15 is fixed in the compiler (`sourcecode/TypeChecker`), not the runtime.
- **Symptoms:**
  - Any `return;` without a value crashed the compiler with a `NullReferenceException`.
  - Separately, a lambda body that could reach its end without a `return` compiled, but the runtime rejected it: "Basic Block in function 'RT_NOM_LambdaBody_…' does not have terminator!".
- **Causes:**
  - `VisitReturnStmt` (`TypeChecker/CodeTransformer.cs`) started with `stmt.Expr.Visit(…)`, and `Expr` is `null` for a bare `return;`.
  - `LambdaTransformEnvironment.Compile` used the body's instructions as they were. Methods (`TypeChecker.cs`) and record methods (`CodeTransformer.cs`) append a `ReturnVoidInstruction` when the body does not always return. Lambdas did not, so the last block had no terminator.
- **Fixes:**
  - In `VisitReturnStmt`, a bare `return;` emits `ReturnVoidInstruction` if the expected return type accepts `Void` (`Void`, `dyn`), the same rule as for falling off the end of a method. Otherwise it is a type error: "Return statement at … needs a value of type Int".
  - In `LambdaTransformEnvironment.Compile`, a body that does not always return gets a trailing `ReturnVoidInstruction`. If the lambda's declared return type does not accept `Void`, that is the error "Not all paths in the lambda at … return a value". The runtime's `ReturnVoid` returns the `Void` object, which works for every function that returns a reference, including lambda bodies.
- **Test:** `Test_BareReturn` uses an early `return;` in a `Void` method, a `return;` inside a lambda's `if` that falls off its end on the other path, and a lambda without any `return`.

---

## Open: generic instance methods (G1)

An instance method with its own type parameters, called as `obj.M<X>(…)`, cannot run yet. This applies to methods like `fun Apply<U>(Fun<T, U> f) : U`, whether or not the class itself is generic. Generic **static** methods, such as `Enumerable.Map<T, S>` in `experiments/monnom/float`, take a different path and are not affected.

`Test_GenericMethod` calls `Apply<U>` in three ways:
- on a non-generic class (`GenCell`);
- on a generic class with `U = T` (`GenBox<Int>.Apply<Int>`);
- on a generic class with `U ≠ T` (`GenBox<Int>.Apply<Float>`), so that a wrong type-argument list shows up.

The problems below are listed in the order the test hits them. Each one hides the next.

1. **Compiler, return type not substituted:** the call's type is the method's `U`, not `X`, so `Int a = c.Apply<Int>(…)` fails with "U is not a subtype of Int". This is the same kind of bug as C9, but for method type parameters.
2. **Compiler, type argument written as a type variable:** the method constant encodes the call as `Apply<TypeVar 0>` (in `GenBox<T>`, `TypeVar 1`): the method's own parameter `U`, not the argument `X`. At runtime, `NomMethodConstant::GetMethod` resolves it in the caller's context and fails the assertion `ArrayRef … Invalid index!` in `NomSubstitutionContextMemberContext::GetTypeVariable`.
3. **Runtime, off-by-one in `NomClass::GetDynamicDispatcherLookup` (`IR/Data/NomClass.cpp`):** the parameters are `[0]` receiver, `[1..T]` the method's type arguments (`TYPETYPE`), `[T+1..]` the value arguments. The check `j >= meth->GetDirectTypeParametersCount()` should be `j >`. As it is, the type argument is converted like a value, and `EnsurePackedUnpacked(REFTYPE → TYPETYPE)` throws. This happens as soon as any class with a generic instance method is compiled, for example at `new GenCell(20)`, even if the method is never called.
4. **Runtime, `NomInterface::GetMethod`'s third branch** (R14c's loop): only these calls reach it. The `context` passed in already contains the call's type arguments (`NomMethodConstant::GetMethod` builds it as class arguments followed by method type arguments). The branch then appends `typeArgs` again, so the method's type arguments may end up in the list twice. This can't be checked until 1–3 are fixed.

---

## Known remaining issues, not fixed

- **Casting a structural value to a second unrelated interface:** prints `UNIMPLEMENTED!` (`RuntimeData/Headers/StructuralValueHeader.cpp`, the `castTag` / `singleCastBlock` paths). It needs a list of cast types per value. This is the largest remaining cause of failing structural configurations (76 of forth's 180).
- **Static fields:** not implemented. The type checker throws `NotImplementedException`, and there is no bytecode or runtime support.
- **`if (x : T)`, `ifnull`/`ifobj` and `T?`/`T!`:** not implemented or not working.
- **`NomMaybeType::GenerateRTInstantiation`:** has the same undeclared-function problem as R14b, with `NOM_RTInstantiateMaybe`.
- **`new Pair<B, A>(…)` inside `Pair<A, B>`:** rejected by the type checker.
- **"Dictionary entry not found" (27 forth configurations):** comes from the dictionary-field code in `RuntimeData/Headers/RTDictionary/`. Not investigated.
- **`experiments/monnom/lambdafield`:** no longer compiles, because of the "same name as a record field" check from commit `b87e361d`. That predates these changes.

The benchmark configurations in `gtp-benchmark-monnom/{forth,sieve,snake,tetris,zombie}/.BM_*` were regenerated and run with all fixes applied. They are generated files, and the folder is now a separate submodule.
