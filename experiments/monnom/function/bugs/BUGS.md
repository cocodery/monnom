# MonNom bug fixes for review

These bugs came up while porting the GTP benchmarks to MonNom (`gtp-benchmark-monnom/`). Each one has a test function in `BugTests.mn`, named in the table below.

**Status (2026-10-28):**
- Committed: C1–C9, R1-R15, G1, K2, K7, K8, K11.
- Applied in the working tree but not committed: K1 (casting a structural value to several unrelated interfaces), K12 (a method called by its bare name inside a lambda).

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
KNOWN=1 $T                                 # also the known-issue tests K3–K6 (stops at the first one)
NOMC="dotnet <old>/nomc.dll" $T Test_IfAssign   # another compiler
```

A full run leaves out the tests for the known issues K3–K6 (see "Known remaining issues"), which still fail. They are recognized by the issue ID on their block line, as in `# BEGIN Test_StaticField K2`. Run one by name to see its failure, or set `KNOWN=1` to include them all.

The script works from any directory. It finds `setpath.sh` by searching upwards from its own folder.

The runtime compiles every method of `Main` when it loads the class, so one runtime bug in any test crashes the whole run. A compile error anywhere also stops the whole file. With a test name, `run_test.sh` deletes every other test's `# BEGIN Test_X` … `# END Test_X` block before compiling. Use that mode to see each bug on its own.

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
| Test_StructuralSecondInterface | K1 | `UNIMPLEMENTED!` | PASS |
| Test_StaticField | K2 | compiler `NotImplementedException` | PASS |
| Test_PairSwap | K7 | compile error ("SwapPair<B, A> does not have a constructor that matches the given arguments") | PASS |
| Test_LambdaClassTypeVar | K8 | "ArrayRef … Invalid index!" assertion | PASS |
| Test_LambdaBareMethodCall | K12 | compiler `NotImplementedException` | PASS |
| Test_GenericMethod | G1 | compile error ("U is not a subtype of Int") | PASS |

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
- **R14c, infinite loop:** in `NomInterface::GetMethod`, the loop that copies a method's type arguments after the context's never incremented `i`. That branch is only reached by calls to generic instance methods; see G1, which also fixes how the branch builds the list.

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

## G1: generic instance methods

An instance method with its own type parameters, called as `obj.M<X>(…)`, could not run. This applies to methods like `fun Apply<U>(Fun<T, U> f) : U`, whether or not the class itself is generic. Generic **static** methods, such as `Enumerable.Map<T, S>` in `experiments/monnom/float`, take a different path and already worked.

The root cause is the same throughout: the code that handles method calls was written for methods whose only type parameters are the class's. A method's **own** type parameters were left out in four places in the compiler, and the parameter positions they occupy were miscounted in six places in the runtime. Nothing had ever called such a method, so each problem hid the next.

`Test_GenericMethod` covers every call path: a class (`GenCell.Apply<Int>`), a generic class with `U = T` and with `U ≠ T` (`GenBox<Int>.Apply<Int>`, `.Apply<Float>`), an interface (`IGenApply.Apply<Int>` on a `GenCell`), a dynamic receiver (`d.Apply<Int>`), generic code passing its own `T` (`GenUser<T>` calling `b.Apply<T>`), and two type parameters plus two values (`Second<U, W>`, typed and dynamic; the second type argument then sits in the overflow array).

### Compiler (`sourcecode/TypeChecker`, `sourcecode/Language`)

1. **The call's type arguments were never bound to the method's type parameters.** `MethodReferenceVisitor.FilterMethodList` (`TypeChecker/TypeExtensions.cs`) substituted only the class's type arguments (`env.Push(cls.Substitutions)`). So `U` stayed `U` in:
   - the parameter types used to pick the method;
   - the call's return type: "U is not a subtype of Int";
   - the method constant written to the bytecode, `Apply<TypeVar U>` instead of `Apply<Int>`. At runtime that read the caller's type variable `U`, which doesn't exist: an `Invalid index!` assertion.

   **Fix:** `methodEnv = env.Push(method.TypeParameters, typeArguments)`, the same thing generic static methods already do (`ns.PArguments.Push(m.TypeParameters, method.Arguments)` in `CodeTransformEnvironment.GetStaticMethod`), used for the parameter types, the return type and the method constant.
2. **A type variable was never a subtype of anything but itself.** `TypeVariable.IsSubtypeOf` (`Language/TypeVariable.cs`) had `DefaultAction = false`, so `T ≤ Top` was false. `FilterMethodList` checks each type argument against the method's bound, so `b.Apply<T>(…)` inside generic code found no method. **Fix:** fall back to the variable's upper bound, as `IsSupertypeOf` already falls back to its lower bound.
3. **Generic methods never matched as overrides.** `MethodDeclDef.Overrides` and `MethodReference.Overrides` compared the signatures directly. The two methods' `U`s are different parameters (`TDTypeArgDeclDef` equality is index *and* parent method), so `GenCell.Apply<U>` didn't implement `IGenApply.Apply<U>`. Both stayed in the class's method list, and every `c.Apply<…>` call was ambiguous (`NotImplementedException` in `FilterMethodList`). **Fix:** read the other method's type parameters as this method's before comparing (the existing `//TODO: type parameter compatibility`; bound compatibility is still a TODO).
4. **Interface method signatures could not use their own type parameters.** `InitializeMembersPass.HandleInterface` (`TypeChecker/TypeChecker.cs`) resolved the parameter and return types without pushing the method's type parameters, unlike the class branch next to it: "U<> not found!". **Fix:** push them, as for class methods. Record methods had the same scoping bug (`CodeTransformer.VisitStructExpr`), which is fixed too, but they are now rejected with a clear error (see below).

### Runtime (`sourcecode/Runtime`)

A class method's LLVM parameters are `[0]` receiver, `[1..T]` the method's type arguments (`TYPETYPE`), `[T+1..]` the value arguments.

5. **Dispatchers treated the last type argument as a value.** `NomClass::GetDynamicDispatcherLookup` and `NomClass::GetRawInvokeFunction` (`IR/Data/NomClass.cpp`), and the record IMT entries in `Runtime/IMT/NomIMTUtils.cpp`, checked `j >= T` instead of "receiver or `j > T`". Any class with a generic method threw while being compiled (`EnsurePackedUnpacked(REFTYPE → TYPETYPE)`), even if the method was never called. **Fix:** `j == 0 || j > T`.
6. **`CastedValueCompileEnv::GetTypeArgument` (`Common/CompileEnv.cpp`) could not read a method's own type arguments.** It is used when a value argument's type mentions the method's type parameter (`Fun<Int, U>`). It had four bugs:
   - it used `i > instanceCount` instead of `>=`, so the method's first type parameter read past the class's type arguments;
   - it subtracted the class's count twice;
   - it returned the raw `i8*` slot instead of a `TYPETYPE`;
   - in the overflow case it cast the array pointer itself to `TYPETYPE` and loaded a type head instead of an entry.

   Separately, `NomClass` (×2) and `NomIMTUtils` passed `paramCount` where the number of **value** arguments is expected.
7. **`NomInterface::GetMethod`'s third branch listed the method's type arguments twice** (`IR/Data/NomInterface.cpp`). `context` already ends with them, because `NomMethodConstant::GetMethod` builds it as the class's type arguments followed by the method's. **Fix:** take only the class part of `context`, then the call's type arguments, which gives exactly `GetTypeParametersCount()` entries. This is also the branch with R14c's missing `i++`.
8. **`CallCheckedInstanceMethod::Compile` (`Instructions/CallCheckedInstanceMethod.cpp`) built typed calls wrongly:**
   - It read the method's `i`-th type argument from `TypeArgs[i]`, but that list starts with the class's type arguments. It is now `TypeArgs[classCount + i - 1]`, and it uses `GenerateRTInstantiation`, so a type argument that contains the caller's type variables works too (R14b).
   - The loop that converts arguments to the parameter types stopped at `GetArgCount()`, so with type arguments it missed the last value arguments.
   - The class (vtable) path passed only the receiver and the value arguments, without the type arguments ("Incorrect number of arguments passed to called function").

### Not supported: record methods with type parameters

A record method's LLVM parameters are `[type arguments…, receiver, arguments…]` (`NomRecordMethod::GetLLVMFunctionType`, the same layout as a lambda body), but every call passes the receiver first. For `T = 0` the two orders agree. Supporting `T > 0` means reordering the parameters in every record dispatch path: `NomRecord::GetDynamicDispatcherLookup`, the record call tag, and the IMT entries. Until then the compiler rejects them: "Record method … cannot have type parameters".

---

## K1: casting a structural value to several unrelated interfaces

A record or lambda can be cast to any number of interfaces. The second cast to an interface that is not a supertype of the first one printed `UNIMPLEMENTED!` and aborted:

```
dyn r = new { fun KA() : Int { … } fun KB() : Int { … } };
ITwoFacedA a = r;
ITwoFacedB b = r;   // UNIMPLEMENTED!
```

**Cause:** a structural value remembers the types it was cast to in its header's cast data (`StructuralValueHeader`, field `CastData`). The design reserves three states: null before the first cast, a single type pointer, and a list of types tagged with bit 0. Only the first two were implemented:
- In the cast (`GenerateMonotonicStructuralCast`), a second cast compared the single type with the target. If it wasn't a subtype, or the cast data was a list, the cast went to the `UNIMPLEMENTED!` block.
- The interface call tag (`NomInterfaceCallTag::createLLVMElement`) checks a method's result against the return type promised by the interface the value was cast to. For a list it also went to `UNIMPLEMENTED!`.

**Fix:**
- **List and adding a type** (`RuntimeData/Headers/StructuralValueHeader.h`/`.cpp`): a list node is `StructuralCastListNode { Type, Next }`, in GC memory. The cast data points to the first node, with bit 0 set. Boehm recognizes interior pointers (`GC_get_all_interior_pointers()` is 1), so the tagged pointer keeps the list alive. The new C function `RT_NOM_AddStructuralCastType` adds a type at the front with a compare-and-swap loop. On the first addition, the single type so far becomes the second node. Nodes are never changed once published, so readers need no locking.
- **The cast:** a value cast before now checks each of its cast types, the single one or every list entry, against the target. If one is a subtype, the cast succeeds, as before. Otherwise the cast runs the same checks as a first cast: a lambda needs a functional interface, and a record needs an invoke method if the interface has one. It then adds the target type with `RT_NOM_AddStructuralCastType`. A first cast still writes the single type directly. Both variants of `GenerateMonotonicStructuralCast` are changed. The one with a run-time target type has no callers at present.
- **The call tag:** for a value with a list, the result is passed to the return-value check function (`NOM_RT_CRT_<interface>`) of every type in the list, with that type's arguments. That's the same check the single-type case does for a mismatching interface. Interfaces that don't have the method ignore the call.

`Test_StructuralSecondInterface` casts one record to three unrelated interfaces, then again to one already in its list, then to `IGetter<Int>` in a generic method, which is a type with variables and a substitution stack. It calls a method through each of them. `ITwoFacedC.KC()` returns a record, whose result goes through the list check. As a negative check (not in the test), a `KC` that returns `5` fails with "Cast failed!" when the value has a list, exactly as when it was cast to `ITwoFacedC` only.

**forth:** all 324 configurations were regenerated with `tools/bench.sh` (in a scratch copy) and run with and without the fix. The same 144 pass both ways. The configurations that printed `UNIMPLEMENTED!` drop from 29 to 20. Those 20 fail in a different place: a value that is neither a lambda nor a record (the third case of `GenerateStructuralValueKindSwitch`) is cast to an interface. Most other failures are nondeterministic: 45 configurations change outcome between two runs of the same runtime. So the remaining forth failures can't be compared run by run.

---

## K2: static fields

Static fields were parsed, but every other layer was missing:
- **Type checker:** `TDClass.StaticFields` was always empty (`//TODO: implement static fields`). Reading or writing one (`StaticFieldReference`), `Class.Field` lookups, and initializers all threw `NotImplementedException`.
- **Bytecode generator:** it threw on the type checker's `ReadStaticFieldInstruction`/`WriteStaticFieldInstruction`.
- **Bytecode format and runtime:** they had no static field support at all.

`Test_StaticField` checks:
- `Main`'s own static field, as `Main.X` and as a bare name;
- `SFCounter`'s static fields: literal initializers of every kind, the defaults without one, `readonly`, and `private`;
- reads and writes from a constructor, an instance method, a static method, a lambda and another class;
- static methods of another class: arguments and results, a bare call to another static method of the same class, recursion, a call from an instance method by bare name, type parameters (`Pick<T>`), a factory (`Make`), and a call inside a lambda.
- static fields holding class objects: one of another class (`Keep : Box`), and one of the class's own type (`Latest : SFCounter`, set by `Make`). The test checks that writing a field doesn't change an object read from it earlier, and that methods can be called and fields read on the stored object.

It passes in a full run at `-o0`, `-o3`, `-o3 --trans` and `-o3 --trans --sync`.

### What is supported

- **Declarations:** `[visibility] static [readonly] Type Name [= literal];` in a class.
- **Initializers:** an `Int` (also negative), `Float`, `Bool`, `String` or `null` literal, which is all the grammar allows (`defaultexpr`). Other forms, and a literal whose type doesn't fit the field, are compile errors.
- **Defaults:** without an initializer, a field starts as `0`, `0.0`, `false` or `null`, depending on its type.
- **Access:** as a bare name in the class's instance methods, static methods and constructors, and in lambdas and records inside them. Elsewhere it's `Class.Field`, which includes accessor chains (`SFCounter.Last.V`). Visibility is checked like for static methods.
- **`readonly`:** a `readonly` static field can only be set by its initializer. Writing it is the compile error "Static field … is read-only".
- **No type variables:** a static field is shared by all instantiations of its class, so its type must not use type variables ("Static field … cannot have a type that uses type variables").

### How it works

**Compiler:**
- `StaticFieldSpec` (`TypeChecker/StaticFieldSpec.cs`) is created in `InitializeMembersPass.HandleClass`, and `TDClass` collects the specs, like instance fields.
- `StaticFieldReference` emits `ReadStaticFieldInstruction`/`WriteStaticFieldInstruction`. Writes convert the value to the field's type first.
- `GetStaticFieldAccessVariable` resolves `Class.Field` in both the expression and the assignment path. The accessor-chain path used to look up `Class.Field` itself as a class, which is fixed too.
- A constructor's `FindField` now also finds static fields.

**Bytecode:**
- **Declarations:** a class's static fields are part of the class record (`Bytecode/Reps/StaticFieldRep.cs`, written by `ClassRep` right after the instance fields, marker `StaticField = 207`). Each record holds the name, type, visibility, `readonly` flag, and the initializer as a kind byte and a 64-bit payload: the `Int` value, the `Float`'s bits, `0`/`1` for `Bool`, or a string constant ID. `ClassRep` also reads these records back, so a project can use the static fields of a library class.
- **Accesses:** two new opcodes, `ReadStaticField = 53` and `WriteStaticField = 54` (`Bytecode/Instructions/StaticFieldInstructions.cs`). Each carries a register, the field name, the owning class and the field type.

The class format changed, so `.mnil` files compiled before this change have to be recompiled.

**Runtime:**
- **Declarations:** `BytecodeReader::ReadStaticField` reads the records into the class (`NomStaticField`, `IR/Data/NomStaticField.h`, in `NomClass::StaticFields`). The fields' types and string initializers count as the class's dependencies (`NomClassLoaded::PushDependencies`), so they are loaded with it.
- **Allocation at class load** (`AllocateStaticFields`, `Instructions/StaticFieldInstructions.cpp`): `RTModule` calls it for every class of a module *before* compiling the classes. It allocates one slot per static field of the class, whether or not anything uses it. A slot is `gcalloc_uncollectable` memory, which the GC scans, so the stored value stays alive. It also emits one function per class, `MONNOM_STATICINIT_<class>`, that stores each field's initial value: the initializer, or `0`, `0.0`, `false` or `null` by the field's type.
- **Initialization:** right after `RTModule` adds the module to the JIT, and before any of its code runs, `RunPendingStaticFieldInitializers` calls these functions once. This can't happen any earlier, because initial values can be runtime objects (`true`, `null`, strings, boxed numbers) that only have addresses once the module is in the JIT. For example, `Int` values that don't fit the packed encoding are boxed here.
- **Accesses:** generated code embeds the slot's address as a constant, so every JIT module, IMT entries included, uses the same slot. A **read is a single load**, with no check whether the field is initialized, followed by the conversion to the field type's representation (unboxed for `Int`/`Float`), like instance field reads. A **write** packs the value and stores it.

---

## K11: `ToString` for `Bool` and `String`

`true.ToString()` and `"x".ToString()`, also through `dyn`, failed at runtime with "No such method!". The runtime's `NomBoolClass` had no methods at all, and `NomStringClass` only had `Print` and `Concat`, whereas `Int` and `Float` define `ToString`. The compiler's standard library didn't declare it either, so a typed call like `b.ToString()` with `b : Bool` didn't compile. K11 was found while testing K2, and has no test of its own.

**Fixes:**
- **Runtime:**
  - `Bool.ToString` (`Library/BoolClass.cpp`, `LIB_NOM_Bool_ToString_1`) returns a new `"true"` or `"false"`. A `Bool` is one of the two objects in `RT_NOM_BOOLEANS`, and its field 0 holds `0` or `1`. The new `NomString` is allocated with `gcalloc`, for the reason given in R11.
  - `String.ToString` (`Library/StringClass.cpp`, `LIB_NOM_String_ToString_1`) returns the string itself, since strings are immutable.

  Both are registered like `Int.ToString`: a final `NomMethodInternal` without arguments that returns `String`.
- **Compiler:** `TypeChecker/StdLib/Bool.cs` and `String.cs` declare `ToString() : String`.
- **Calls:** a `Bool` computed by a comparison is a raw `i1`. When it is the receiver of a typed call, as in `(x < 5).ToString()`, the argument conversion in `CallCheckedInstanceMethod::Compile` used to call `PackInt` on it, which throws. It now boxes it with `PackBool`.

**Checked:** typed and dynamic receivers, raw comparisons, and use inside `++` all give the right text at `-o0`, `-o3` and `-o3 --trans`:

```
true false true false hello true dyn hello!true
```

---

## K7: a generic class naming itself with other type arguments

Inside `class SwapPair<A, B>`, the type `SwapPair<B, A>` was read as `SwapPair<A, B>`. Two errors followed:
- `new SwapPair<B, A>(Second, First)` failed with "SwapPair<B, A> does not have a constructor that matches the given arguments": the constructor wanted `(A, B)` and got `(B, A)`.
- A method declared `: SwapPair<B, A>` returned `SwapPair<A, B>` to its callers, so `SwapPair<String, Int> q = p.Swap()` failed with "SwapPair<Int, String> is not a subtype of SwapPair<String, Int>".

A different class, as in `new Q<B, A>(…)`, was fine.

**Cause:** the type checker looks up a name relative to the current container. `ALookupContext.GetPossibleChildPaths` (`TypeChecker/LookupContext.cs`) compares the name with the path to the container by name and number of type arguments only. When the name is the container itself, it yields the empty path, and `FindChildren` then returns the container's own reference, `Root`. That reference holds the container's own type parameters (`A -> A, B -> B`), so the type arguments written in the name were dropped.

**Fix:** the empty path is now used only for a container without type parameters. A generic container's name is looked up the normal way, through the enclosing namespace, which binds the type arguments written in the name. Writing the class's own parameters, as in `SwapPair<A, B>`, gives the same type as before.

`Test_PairSwap` swaps a pair and swaps it back.

---

## K8: lambdas using a class type variable

Inside `class LambdaHolder<T>`, a lambda such as `(T x) => x` compiled, but the runtime failed the assertion `ArrayRef … Invalid index!` while compiling the lambda body. The same happened for a captured local of type `T` (`T c = v; () => c`). With a declared return type, as in `(T x) => { T y = x; return y; } : T`, the compiler already failed, with "T is not a subtype of T".

**Cause:** this is a compiler bug. The old description, a missing substitution context in `NomLambdaBody::createLLVMElement`, was wrong: methods use `GetArgumentTypes(nullptr)` the same way. A lambda is closure-converted. It has no parent context at runtime (`NomClassLoaded::AddLambda` passes `nullptr`), so the only type variables it knows are its closure type parameters. The compiler collects these in the lambda's `StructTypeCheckLookup`: each type variable resolved through it becomes a captured parameter, and `CreateClosure` passes the enclosing type arguments for them. Types written in the lambda body went through that lookup. Three kinds of types didn't:
- the parameter types,
- the declared return type,
- the types of captured variables.

`VisitLambdaExpr` resolves the first two in the enclosing method's context, and the capture code copies the third from the enclosing variable. They kept the class's `T`, which is type variable 0 of the class. The lambda had no closure type parameters, so variable 0 didn't exist in its context. Inside the body, `T y` was the captured `T` while `x` had the class's `T`, so `y = x` didn't type-check either.

**Fix** (`TypeChecker/StructTransformEnvironment.cs`, `TypeChecker/LambdaTransformEnvironment.cs`):
- The new `StructTypeCheckLookup.CaptureType` rewrites a type resolved in the parent context into the lambda's context. It uses the visitor that already does this for the bounds of captured type parameters, and registers each type variable it meets.
- `LambdaTransformEnvironment` applies it to the parameter types and the expected return type before creating the lambda, and to the field type of each captured variable.

Records already resolve their field types in their own lookup, so they didn't need the change.

`Test_LambdaClassTypeVar` checks, with `T = Int` and with `T = Box`: a parameter of type `T`, a declared return type and a local of type `T`, a captured local of type `T`, a nested lambda that captures a `T` parameter, and two `T` parameters, one of them taken from a field.

---

## K12: a method called by its bare name inside a lambda

Inside a lambda in a class method, a call such as `() => Twice(21)` crashed the compiler with `NotImplementedException`, where `Twice` is a static or instance method of the class. Qualified calls (`BareCalls.Twice(21)`, `this.Inc(5)`) worked.

**Cause:** this is a type-checker problem, not a parser one. `Twice(21)` parses as a call of a bare name, and `CodeTransformer` resolves the name with `GetLocalCallable` on the current environment. In a lambda, that is `LambdaTransformEnvironment.GetLocalCallable`. It looked the name up among the lambda's own parameters and locals, and otherwise asked the enclosing method's environment. When the enclosing method found something, it threw on purpose (`//TODO: need to make parent refer back to this environment when generating this pointer method access`). The method's lookup builds its result for the method's own frame:
- a variable is read from the method's register;
- an instance method is called on the method's `this` register.

In the lambda's frame those registers don't exist. Register `#0` is the closure there, as in C4–C7. Using the result would have read the wrong registers.

**Lookup order:** `AMemberCodeTransformEnvirnment.GetLocalCallable` already searches in the order one would expect, so the order didn't need changing:
1. variables: parameters and locals, then fields;
2. instance methods, on `this`;
3. static methods of the enclosing class.

There are no unqualified global functions: a method of another class needs its class name. A variable that holds a function comes before a method of the same name.

**Fix** (`TypeChecker/LambdaTransformEnvironment.cs`): the lambda now follows the same order, with every access made from its own frame:
1. **Variables:** it looks up the name with the lambda's indexer. That finds the lambda's own parameters and locals first. It then finds the enclosing method's variables, which the indexer captures into closure fields, and the enclosing object's fields, read through the captured `this`. The value is read in the lambda's frame and called through `AsCallable`.
2. **Instance methods:** it looks up `this` the same way, which captures it into the closure, and resolves the method with `GetMethod` on that value.
3. **Static methods:** only then does it ask the enclosing environment. Nothing in its frame matched, so what the enclosing environment can still find is a static method of the class. The reference for a static call takes the calling environment when the call is generated, so it doesn't depend on the enclosing frame.

Nested lambdas work the same way: the inner lambda's indexer captures from the outer lambda. Code that compiled before resolves exactly as before. The old method only succeeded for the lambda's own parameters and locals, and those are still checked first.

`Test_LambdaBareMethodCall` checks, in `class BareCalls`:
- a static method from a lambda in an instance method, and from a lambda in a static method;
- an instance method, which changes the object's field, so it really runs on `this`;
- a lambda parameter with a method's name, which is called instead of the method;
- a captured local of the enclosing method with a method's name, which is also called instead of the method;
- an instance method and a static method from a nested lambda.

---

## Known remaining issues, not fixed

The issues are numbered K1–K12. K1 (casting a structural value to several unrelated interfaces), K2 (static fields), K7 (a generic class naming itself with other type arguments), K8 (lambdas using a class type variable), K11 (`ToString` for `Bool` and `String`) and K12 (a method called by its bare name inside a lambda) are fixed; see their sections above. Each remaining issue that can be shown in a small program (K3–K6) has a test in `BugTests.mn`, whose `# BEGIN` line carries the ID. The test shows what the program should do once the issue is fixed. Any types it needs are inside its own `# BEGIN/# END` blocks, so it can't affect the other tests.

| ID | Issue | Test | Fails now with |
|---|---|---|---|
| K3 | `if (x : T)` | `Test_IfType` | compiler `NotImplementedException` |
| K4 | `ifobj` on a value that may be null | `Test_IfObj` | "Local variable b … is never null, then-branch never taken" |
| K5 | Casting `T?` to `T` | `Test_MaybeCast` | "Expected type Box?, but found Box" |
| K6 | `NomMaybeType::GenerateRTInstantiation` | `Test_MaybeInstantiation` | segfault while compiling `Make` |

- **K3, `if (x : T)`:** `CodeTransformer.VisitIfTypeStmt` is `throw new NotImplementedException()`.
- **`T?`:** a declaration such as `Box? b = …`, `ifnull`, and `T!` all work. Two things don't:
  - **K4, `ifobj (b)`** on a value that may be null (here the result of a method returning `Box?`) is rejected with "is never null, then-branch never taken". The null analysis seems inverted for `ifobj`, whose then-branch is the non-null case.
  - **K5, the cast `(Box) b`** from `Box?` fails to type-check with "Expected type Box?, but found Box".
- **K6, `NomMaybeType::GenerateRTInstantiation`** (`IR/Types/NomMaybeType.cpp`): has the same undeclared-function problem as R14b. It calls `env->Module->getFunction("NOM_RTInstantiateMaybe")`, which returns null. Any maybe type containing a type variable that needs a runtime descriptor reaches it, for example `new ArrayList<T?>()` inside a generic class.

Not covered by a test:
- **K9, "Dictionary entry not found" (27 forth configurations):** comes from the dictionary-field code in `RuntimeData/Headers/RTDictionary/`. It has not been investigated, so there is no known small program that triggers it. The failing forth configurations are the reproduction for now.
- **K10, record methods with their own type parameters** (`new { fun M<U>(…) }`): the compiler rejects them on purpose ("Record method … cannot have type parameters"). Record methods take their type arguments before the receiver, but every dispatch path passes the receiver first (see G1). A test belongs with the change that adds support.

The benchmark configurations in `gtp-benchmark-monnom/{forth,sieve,snake,tetris,zombie}/.BM_*` were regenerated and run with all fixes applied. They are generated files, and the folder is now a separate submodule.
