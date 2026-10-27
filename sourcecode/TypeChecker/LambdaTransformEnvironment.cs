using System;
using System.Collections.Generic;
using System.Text;
using Nom.Language;
using Nom.Parser;
using System.Linq;

namespace Nom.TypeChecker
{
    public class LambdaTransformEnvironment : ICodeTransformEnvironment
    {
        private class Register : IRegister
        {
            public Register(int index)
            {
                Index = index;
            }

            public int Index { get; }
        }

        private List<Register> registers = new List<Register>();
        private ICodeTransformEnvironment ParentEnv;
        private TDLambda lambda;
        private IEnumerable<ArgumentDeclDef> Parameters;

        public ITDLambda Lambda => lambda;
        internal LambdaTransformEnvironment(ICodeTransformEnvironment parentEnv, IEnumerable<ArgumentDeclDef> args, Language.IType expectedReturnType, ISourceSpan locs = null)
        {
            Parameters = args.ToList();
            registers.Add(new Register(0));
            ParentEnv = parentEnv;
            //not registering for changes in parent env because that would unnecessarily put stuff into the closure

            StructContext = new StructTypeCheckLookup(ParentEnv.Context);
            // the parameter and return types were resolved in the parent context; the
            // lambda only knows the type variables it captures (its closure type
            // parameters), so e.g. the class's T in (T x) => x must become a captured one
            Parameters = Parameters.Select(argdef => new ArgumentDeclDef(argdef.Identifier, StructContext.CaptureType(argdef.Type))).ToList();

            foreach (var argdef in Parameters)
            {
                AddLocalVariable(argdef.Identifier, argdef.Type, CreateRegister());
            }

            lambda = ParentEnv.Context.Container.CreateLambda(new TypeParametersSpec(new List<ITypeParameterSpec>()), new ParametersSpec(Parameters), new Language.DynamicType());

            Context = StructContext.PushVariables(lambda.TypeParameters);
            ExpectedReturnType = StructContext.CaptureType(expectedReturnType).InjectOptional();
        }

        public IExprTransformResult Compile(Block b, ISourceSpan locs)
        {
            IStmtTransformResult str = b.Visit(CodeTransformer.Instance, this);
            if (!str.AlwaysGoesElsewhere)
            {
                if (ExpectedReturnType.HasElem && !StdLib.StdLib.VoidType.IsSubtypeOf(ExpectedReturnType.Elem, false))
                {
                    throw new TypeCheckException("Not all paths in the lambda at @0 return a value", locs);
                }
                BestReturnType = StdLib.StdLib.VoidType;
                str = new StmtTransformResult(str.Snoc(new ReturnVoidInstruction()), this, true, false, true);
            }
            List<ArgumentDeclDef> consparameters = new List<ArgumentDeclDef>();

            foreach (CaptureField cf in fields.Values)
            {
                consparameters.Add(new ArgumentDeclDef(cf.Field.Identifier, cf.Field.Type));
            }

            lambda.Instructions = str;
            lambda.RegisterCount = this.registers.Count;
            int index = 0;
            foreach (PhiNode pn in lambda.Instructions.Where(x => x is PhiNode))
            {
                pn.Index = index++;
            }
            lambda.ClosureParameters = new ParametersSpec(consparameters);
            lambda.ClosureTypeParameters = new TypeParametersSpec(StructContext.StructTypeParameters);
            // IEnumerable<IExprTransformResult> argResults = fields.Select(f => f.Value.Source.GenerateReadAccess(ParentEnv));
            IEnumerable<IExprTransformResult> argResults = fields.Select(f => f.Value.Source.GenerateReadAccess(ParentEnv)).ToList();
            var cc = new CreateClosureInstruction(lambda, StructContext.ConstructorTypeArguments, argResults.Select(ar => ar.Register), ParentEnv.CreateRegister());
            lambda.ReturnType = BestReturnType;
            return new ExprTransformResult(new Language.DynamicType(), cc.Register, argResults.Flatten().Snoc(cc), locs);
        }

        private class CaptureField
        {
            public CaptureField(TDLambdaField field, IVariableReference source)
            {
                Field = field;
                Source = source;
            }
            public TDLambdaField Field { get; }
            public IVariableReference Source { get; }
        }
        private Dictionary<string, CaptureField> fields = new Dictionary<string, CaptureField>();
        public IOptional<IVariableReference> this[Identifier ident]
        {
            get
            {
                if (localVariables.ContainsKey(ident.Name))
                {
                    return new LocalVariableRef(ident, localVariables[ident.Name]).InjectOptional();
                }
                if (fields.ContainsKey(ident.Name))
                {
                    return new LambdaFieldReference(ident, fields[ident.Name].Field).InjectOptional();
                }
                var plv = ParentEnv.GetLocalVariable(ident.Name);
                if (plv.HasElem)
                {
                    TDLambdaField fs = new TDLambdaField(ident, StructContext.CaptureType(plv.Elem.Type), lambda);
                    lambda.AddFieldDef(fs);
                    fields.Add(ident.Name, new CaptureField(fs, new LocalVariableRef(ident, plv.Elem)));
                    return new LambdaFieldReference(ident, fs).InjectOptional();
                }
                var outer = ParentEnv[ident];

                if (outer.HasElem && outer.Elem is CheckedFieldReference refr && !refr.Receiver.Any())
                {
                    IVariableReference thisField = new ATopLevelCodeTransformEnvironment.CheckedThisFieldReference(ident, refr.FieldSpec);
                    return thisField.InjectOptional();
                }

                if (outer.HasElem && (outer.Elem is LocalVariableRef // a register in the parent frame 
                    || outer.Elem is LambdaFieldReference // parent is a lambda and request a captured field
                    || (outer.Elem is IInstanceFieldReference && outer.Elem.IsReadOnly)))
                {
                    TDLambdaField fs = new TDLambdaField(ident, StructContext.CaptureType(outer.Elem.Type), lambda);
                    lambda.AddFieldDef(fs);
                    fields.Add(ident.Name, new CaptureField(fs, outer.Elem));
                    return new LambdaFieldReference(ident, fs).InjectOptional();
                }

                if (outer.HasElem && outer.Elem is IInstanceFieldReference && this[new Identifier("this", ident.Locs)].HasElem)
                {   // this block resolve mutable captured field for the enclosing object
                    // a general way for this to read fields
                    IVariableReference thisField = new ThisRelativeFieldReference(ident, outer.Elem.Type);
                    return thisField.InjectOptional();
                }
                return outer;
            }
        }

        private class LambdaFieldReference : IInstanceFieldReference
        {
            public LambdaFieldReference(Identifier ident, TDLambdaField lf)
            {
                Identifier = ident;
                LambdaField = lf;
            }
            public TDLambdaField LambdaField { get; }
            public IInstanceFieldReference OptimisticVersion => this;

            public Identifier Identifier { get; }

            public bool IsReadOnly => true;

            public bool IsInitialized => true;

            public Language.IType Type => LambdaField.Type;

            public IExprTransformResult GenerateReadAccess(ICodeTransformEnvironment env)
            {
                var instruction = new ReadLambdaFieldInstruction(LambdaField, env.CreateRegister());
                return new ExprTransformResult(Type, instruction.Register, instruction.Singleton());
            }

            public IStmtTransformResult GenerateWriteAccess(IExprTransformResult value, ICodeTransformEnvironment env)
            {
                throw new TypeCheckException("Captured local variables are read-only!");
            }
        }

        private class ThisRelativeFieldReference : IVariableReference
        {
            public ThisRelativeFieldReference(Identifier ident, Language.IType type)
            {
                Identifier = ident;
                Type = type;
            }
            public Identifier Identifier { get; }
            public Language.IType Type { get; }
            public bool IsReadOnly => false;
            public bool IsInitialized => true;

            private IVariableReference FieldOfThis(ICodeTransformEnvironment env, out IExprTransformResult thisexpr)
            {
                thisexpr = env[new Identifier("this", Identifier.Locs)].Elem.GenerateReadAccess(env);
                var field = env.GetFieldAccessVariable(thisexpr, Identifier);
                if (!field.HasElem)
                {
                    throw new TypeCheckException("Field $0 is not accessible from this closure!", Identifier);
                }
                return field.Elem;
            }

            public IExprTransformResult GenerateReadAccess(ICodeTransformEnvironment env)
            {
                var read = FieldOfThis(env, out var thisexpr).GenerateReadAccess(env);
                return new ExprTransformResult(read.Type, read.Register, thisexpr.Concat(read).Distinct().ToList());
            }

            public IStmtTransformResult GenerateWriteAccess(IExprTransformResult value, ICodeTransformEnvironment env)
            {
                var write = FieldOfThis(env, out var thisexpr).GenerateWriteAccess(value, env);
                return new StmtTransformResult(thisexpr.Concat(write).Distinct().ToList(), env);
            }
        }

        private Language.IType bestReturnType = BotType.Instance;
        public Language.IType BestReturnType
        {
            get => ExpectedReturnType.Extract(ert => ert.IsSubtypeOf(bestReturnType, false) ? ert : bestReturnType, bestReturnType);
            set { bestReturnType = bestReturnType.Join(value); }
        }

        public IOptional<Language.IType> ExpectedReturnType { get; }

        public StructTypeCheckLookup StructContext { get; }
        public ITypeCheckLookup<Language.IType, Language.ITypeArgument> Context { get; }

        private Dictionary<string, LocalVariable> localVariables = new Dictionary<string, LocalVariable>();
        public IVariableReference AddLocalVariable(Identifier ident, Language.IType type)
        {
            return AddLocalVariable(ident, type, null);
        }
        public IVariableReference AddLocalVariable(Identifier ident, Language.IType type, IRegister register)
        {
            if (ident.Name == "this" || ident.Name == "dis")
            {
                throw new TypeCheckException("$1 is a reserved name!", ident);
            }
            if (localVariables.ContainsKey(ident.Name))
            {
                throw new TypeCheckException("A variable named $1 is already defined in this scope!", ident);
            }
            LocalVariable lv = new LocalVariable(ident.Name, type, register);
            lv.RegisterChange += RaiseVariableRegisterChanged;
            localVariables.Add(lv.Name, lv);
            return new LocalVariableRef(ident, lv);
        }

        public IOptional<ICallableReference> AsCallable(IExprTransformResult value, IEnumerable<Language.IType> argTypes)
        {
            var meth = GetMethod(value, new AArgIdentifier<Identifier, Language.IType>(new Identifier("", value.Locs), new List<Language.IType>()), argTypes);
            if (!meth.HasElem)
            {
                throw new InternalException("(Optimistic) callable should have fitting invoke method");
            }
            return meth;
        }

        public IRegister CreateRegister()
        {
            Register reg = new Register(registers.Count);
            registers.Add(reg);
            return reg;
        }

        public IOptional<ICallableReference> GetConstructor(IParamRef<IClassSpec, Language.IType> cls, IEnumerable<Language.IType> argTypes)
        {
            return ParentEnv.GetConstructor(cls, argTypes);
        }

        public ILocalVariable GetCurrentVersion(ILocalVariable var)
        {
            if (!localVariables.ContainsKey(var.Name))
            {
                throw new InternalException("Local variable does not exist!");
            }
            return localVariables[var.Name];
        }

        public IOptional<IVariableReference> GetFieldAccessVariable(IExprTransformResult value, Identifier ident)
        {
            return ParentEnv.GetFieldAccessVariable(value, ident);
        }

        public IOptional<ICallableReference> GetLocalCallable(IArgIdentifier<Identifier, Language.IType> callableName, IEnumerable<Language.IType> argTypes)
        {
            if (callableName.Arguments.Count() == 0 && localVariables.ContainsKey(callableName.Name.Name))
            {
                return AsCallable(new LocalVariableRef(callableName.Name, localVariables[callableName.Name.Name]).GenerateReadAccess(this), argTypes);
            }
            var parentResult = ParentEnv.GetLocalCallable(callableName, argTypes);
            if (parentResult.HasElem)
            {
                throw new NotImplementedException(); //TODO: need to make parent refer back to this environment when generating this pointer method access
            }
            return parentResult;
        }

        public IOptional<ILocalVariable> GetLocalVariable(string name)
        {
            return localVariables.GetOptional(name);
        }

        public IOptional<ICallableReference> GetMethod(IExprTransformResult value, IArgIdentifier<Identifier, Language.IType> method, IEnumerable<Language.IType> argTypes)
        {
            return ParentEnv.GetMethod(value, method, argTypes);
        }

        public IOptional<IVariableReference> GetStaticFieldAccessVariable(IParamRef<INamespaceSpec, Language.IType> ns, Identifier ident)
        {
            return ParentEnv.GetStaticFieldAccessVariable(ns, ident);
        }

        public IOptional<ICallableReference> GetStaticMethod(IParamRef<INamespaceSpec, Language.IType> ns, IArgIdentifier<Identifier, Language.IType> method, IEnumerable<Language.IType> argTypes)
        {
            return ParentEnv.GetStaticMethod(ns, method, argTypes);
        }

        public void MergeIn(ICodeTransformEnvironment env)
        {
            foreach (LocalVariable lv in localVariables.Values)
            {
                lv.CurrentRegister = env.GetCurrentVersion(lv).CurrentRegister;
            }
        }

        public PhiNode GetBreakPHI(int levels)
        {
            throw new TypeCheckException("No loop to break out of!");
        }

        public PhiNode GetContinuePHI(int levels)
        {
            throw new TypeCheckException("No loop to continue!");
        }

        public ILocalVariable GetTopmostVersion(ILocalVariable var)
        {
            return GetCurrentVersion(var);
        }

        public bool DescendedFrom(ICodeTransformEnvironment ancestor)
        {
            return ancestor == this || ParentEnv.DescendedFrom(ancestor);

        }
        public event Action<ICodeTransformEnvironment, ILocalVariable, IRegister, IRegister> VariableRegisterChanged;
        public void RaiseVariableRegisterChanged(ICodeTransformEnvironment e, ILocalVariable v, IRegister cr, IRegister nr)
        {
            if (VariableRegisterChanged != null)
            {
                VariableRegisterChanged(e, v, cr, nr);
            }
        }
        public void RaiseVariableRegisterChanged(ILocalVariable v, IRegister cr, IRegister nr)
        {
            if (VariableRegisterChanged != null)
            {
                VariableRegisterChanged(this, v, cr, nr);
            }
        }
    }
}
