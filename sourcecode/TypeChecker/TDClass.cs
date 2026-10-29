using System;
using System.Collections.Generic;
using System.Text;
using System.Linq;
using Nom.Parser;
using Nom.Language;

namespace Nom.TypeChecker
{
    internal class TDClass : ATDNamed<TDClassDef>, ITDClass
    {


        public TDClass(Program program, string name, IEnumerable<TDTypeArgDecl> arguments, IOptional<ITDChild> parentNamespace) : base(program, name, arguments, parentNamespace)
        {

        }


        public bool IsFinal
        {
            get;
            private set;
        }

        public bool IsAbstract
        {
            get;
            private set;
        }

        public IOptional<IParamRef<IClassSpec, Language.IType>> SuperClass
        {
            get;
            private set;
        } = new ClassRef<Language.IType>(StdLib.Object.Instance, new TypeEnvironment<Language.IType>()).InjectOptional();



        public TDClass GetClass(DeclIdentifier name)
        {
            if (name.Arguments.Count() == 0 && TDNamespaces.Any(ns => ns.Name == name.Name.Name))
            {
                throw new TypeCheckException("$1 is already a namespace, not a class", name);
            }
            if (TDInterfaces.Any(iface => iface.Name == name.Name.Name && iface.TypeParameters.Count() == name.Arguments.Count()))
            {
                throw new TypeCheckException("$1 is already an interface, not a class", name);
            }
            TDClass ret = TDClasses.FirstOrDefault(cls => cls.Name == name.Name.Name && cls.TypeParameters.Count() == name.Arguments.Count());
            if (ret == null)
            {
                ret = new TDClass(Program, name.Name.Name, name.Arguments.Select((arg, i) => new TDTypeArgDecl(arg.Name.Name, this.OverallTypeParameterCount + i)), this.InjectOptional());
                this.AddClass(ret);
            }
            return ret;
        }

        public bool IsInheritanceInitialized => Definitions.All(d => d.IsInheritanceInitialized);

        private List<StaticMethodDef> staticMethods = new List<StaticMethodDef>();
        public IEnumerable<StaticMethodDef> StaticMethods => staticMethods.ToList();
        IEnumerable<IStaticMethodSpec> IClassSpec.StaticMethods => StaticMethods;
        IEnumerable<IStaticMethodDef> IClass.StaticMethods => StaticMethods;

        private List<FieldSpec> fields = new List<FieldSpec>();
        private List<FieldSpec> localFields = new List<FieldSpec>();
        public IEnumerable<IFieldDecl> LocalFields => localFields.ToList();
        public IEnumerable<FieldSpec> Fields => fields.ToList();
        IEnumerable<IFieldSpec> IClassSpec.Fields => Fields;
        IEnumerable<IFieldDecl> IClass.Fields => Fields;


        private List<ConstructorDef> constructors = new List<ConstructorDef>();
        public IEnumerable<ConstructorDef> Constructors => constructors.ToList();
        IEnumerable<IConstructorSpec> IClassSpec.Constructors => Constructors;
        IEnumerable<IConstructorDef> IClass.Constructors => Constructors;

        public IEnumerable<IInstanceSpec> Instances => throw new NotImplementedException();

        private List<TDLambda> lambdas = new List<TDLambda>();
        public IEnumerable<ITDLambda> Lambdas => lambdas.ToList();

        new private List<MethodDef> methods = new List<MethodDef>();
        public override IEnumerable<MethodDeclDef> Methods => methods.ToList();

        IEnumerable<IMethodDef> IClass.Methods => methods.ToList();

        public override IEnumerable<IParamRef<IInterfaceSpec, Language.IType>> ImmediateSuperTypes => SuperClass.AsEnumerable().Concat(base.ImmediateSuperTypes);

        private List<StaticFieldSpec> staticFields = new List<StaticFieldSpec>();
        IEnumerable<IStaticFieldDecl> IClass.StaticFields => staticFields.ToList();
        public IEnumerable<IStaticFieldSpec> StaticFields => staticFields.ToList();

        private List<TDStruct> structs = new List<TDStruct>();
        public IEnumerable<ITDStruct> Structs => structs.ToList();

        public override Ret Visit<Arg, Ret>(INamespaceSpecVisitor<Arg, Ret> visitor, Arg arg = default(Arg))
        {
            return visitor.VisitClassSpec(this, arg);
        }

        public override Ret Visit<Arg, Ret>(ITDChildVisitor<Arg, Ret> visitor, Arg arg = default(Arg))
        {
            return visitor.VisitClass(this, arg);
        }

        private bool HadSuperClassDefinition = false;
        protected override void CheckFirstDefinition(TDClassDef def)
        {
            base.CheckFirstDefinition(def);
            this.IsFinal = def.IsFinal;
            this.IsAbstract = def.IsAbstract;
        }
        protected override void CheckDefinition(TDClassDef def)
        {
            base.CheckDefinition(def);
            if (this.IsFinal != def.IsFinal)
            {
                CompilerOutput.RegisterException(new TypeCheckException("Class $1: final specifications must be consistent over all definitions", def.Name));
            }
            if (this.IsAbstract != def.IsAbstract)
            {
                CompilerOutput.RegisterException(new TypeCheckException("Class $1: abstract specifications must be consistent over all definitions", def.Name));
            }
        }

        public override void ConsolidateDefinition(TDClassDef def)
        {
            base.ConsolidateDefinition(def);
            if (def.SuperClass.HasElem)
            {
                if (HadSuperClassDefinition)
                {
                    if (!def.SuperClass.Elem.Equals(SuperClass.Elem))
                    {
                        CompilerOutput.RegisterException(new TypeCheckException("Class $1: superclass specifications must be consistent", def.Name));
                    }
                }
                else
                {
                    SuperClass = def.SuperClass;
                    HadSuperClassDefinition = true;
                }
            }
            foreach (StaticMethodDef smd in def.StaticMethodDefinitions)
            {
                if (!staticMethods.Contains(smd))
                {
                    //TODO: check method
                    staticMethods.Add(smd);
                }
            }
            foreach (FieldSpec fd in def.FieldDefinitions)
            {
                if (fields.Any(xfd => xfd.Name == fd.Name)) //TODO: include superclass in field consolidation
                {
                    throw new TypeCheckException("Field $0 already exists", fd.Identifier);
                }
                if (methods.Any(xmd => xmd.Name == fd.Name))
                {
                    throw new TypeCheckException("Field $0 has the same name as a method", fd.Identifier);
                }
                fields.Add(fd);
                localFields.Add(fd);
            }
            foreach (StaticFieldSpec sfd in def.StaticFieldDefinitions)
            {
                if (staticFields.Any(xsfd => xsfd.Name == sfd.Name) || fields.Any(xfd => xfd.Name == sfd.Name))
                {
                    throw new TypeCheckException("Field $0 already exists", sfd.Identifier);
                }
                if (methods.Any(xmd => xmd.Name == sfd.Name) || staticMethods.Any(xsmd => xsmd.Name == sfd.Name))
                {
                    throw new TypeCheckException("Static field $0 has the same name as a method", sfd.Identifier);
                }
                staticFields.Add(sfd);
            }
            foreach (ConstructorDef cd in def.Constructors)
            {
                // if (constructors.Any(xcd => !xcd.Parameters.IsDisjoint(cd.Parameters)))
                // {
                //     throw new TypeCheckException("Class $0 has non-disjoint constructors", def.Name);
                // }
                //overloaded constructors cannot be called yet: a call with dyn arguments can accept
                //several of them (OverloadedConstructorCall.GenerateCall is not implemented), so a
                //class can have only one
                if (constructors.Any())
                {
                    throw new TypeCheckException("Class $0 can have only one constructor", def.Name);
                }
                constructors.Add(cd);
            }
            foreach (MethodDef md in def.MethodDefinitions)
            {
                if (Methods.Any(xmd => xmd.Name == md.Name && xmd.TypeParameters.Count() == md.TypeParameters.Count() && !xmd.Parameters.IsDisjoint(md.Parameters)))
                {
                    throw new TypeCheckException("Method $0 has non-disjoint overloadings", md.Identifier);
                }
                if (fields.Any(xfd => xfd.Name == md.Name))
                {
                    throw new TypeCheckException("Method $0 has the same name as a field", md.Identifier);
                }
                methods.Add(md);
            }
        }

        private bool interfaceImplementationsChecked = false;

        /// <summary>
        /// Reports every interface method this class does not implement. When
        /// the runtime loads a class, it fills each interface slot of the
        /// method table with a method of the same name from the class or a
        /// superclass whose signature matches strictly, and aborts if there is
        /// none, so these checks mirror that rule rather than the optimistic
        /// one used for overriding. Abstract classes are not checked.
        /// </summary>
        public void CheckInterfaceImplementations(IReference location)
        {
            if (interfaceImplementationsChecked || IsAbstract)
            {
                return;
            }
            interfaceImplementationsChecked = true;

            var implementations = new List<IMethodSpec>();
            var required = new List<(IMethodSpec Method, IInterfaceSpec Interface)>();
            var visitedInterfaces = new HashSet<IInterfaceSpec>();

            IClassSpec cls = this;
            ITypeEnvironment<Language.IType> env = new TypeEnvironment<Language.IType>();
            var visitedClasses = new HashSet<IClassSpec>();
            while (visitedClasses.Add(cls))
            {
                IEnumerable<IMethodSpec> declared = cls is TDClass tdc ? tdc.Methods : cls.Methods;
                implementations.AddRange(cls == this ? declared : declared.Select(m => new MethodReference(m, env)));
                foreach (var iface in cls.Implements)
                {
                    CollectInterfaceMethods(iface, env, required, visitedInterfaces);
                }
                if (!cls.SuperClass.HasElem)
                {
                    break;
                }
                var super = cls.SuperClass.Elem;
                env = super.PArguments.Substitute(env);
                cls = super.Element;
            }

            foreach (var (meth, iface) in required)
            {
                var candidates = implementations.Where(impl => impl.Name == meth.Name).ToList();
                if (candidates.Any(impl => StrictlyImplements(impl, meth)))
                {
                    continue;
                }
                string ifaceMethod = iface.Name + "." + meth.Name;
                var sameArity = candidates.FirstOrDefault(impl => impl.TypeParameters.Count() == meth.TypeParameters.Count() && impl.Parameters.Entries.Count() == meth.Parameters.Entries.Count());
                if (sameArity == null)
                {
                    CompilerOutput.RegisterException(new TypeCheckException("Class $0 does not implement " + ifaceMethod, location));
                }
                else
                {
                    IReference at = sameArity is MethodDeclDef mdd ? (IReference)mdd.Identifier : location;
                    if (!sameArity.ReturnType.IsSubtypeOf(meth.ReturnType, false))
                    {
                        CompilerOutput.RegisterException(new TypeCheckException("Method $0 cannot implement " + ifaceMethod + ": it returns $1, which is not a subtype of $2", at, sameArity.ReturnType, meth.ReturnType));
                    }
                    else
                    {
                        CompilerOutput.RegisterException(new TypeCheckException("Method $0 cannot implement " + ifaceMethod + ": its parameter types do not accept the interface's", at));
                    }
                }
            }
        }

        private static void CollectInterfaceMethods(IParamRef<IInterfaceSpec, Language.IType> iface, ITypeEnvironment<Language.IType> env, List<(IMethodSpec Method, IInterfaceSpec Interface)> required, HashSet<IInterfaceSpec> visited)
        {
            if (!visited.Add(iface.Element))
            {
                return;
            }
            var ifaceEnv = iface.PArguments.Substitute(env);
            IEnumerable<IMethodSpec> declared = iface.Element is TDInterface tdi ? tdi.Methods : iface.Element.Methods;
            foreach (var meth in declared)
            {
                required.Add((new MethodReference(meth, ifaceEnv), iface.Element));
            }
            foreach (var super in iface.Element.Implements)
            {
                CollectInterfaceMethods(super, ifaceEnv, required, visited);
            }
        }

        // The runtime's rule: return types covariant and parameter types
        // contravariant, both without optimism. Generic methods are matched
        // on their counts only, as their type parameters are not unified.
        private static bool StrictlyImplements(IMethodSpec impl, IMethodSpec meth)
        {
            if (impl.TypeParameters.Count() != meth.TypeParameters.Count() || impl.Parameters.Entries.Count() != meth.Parameters.Entries.Count())
            {
                return false;
            }
            if (meth.TypeParameters.Count() > 0)
            {
                return true;
            }
            return impl.ReturnType.IsSubtypeOf(meth.ReturnType, false) && meth.Parameters.Entries.Zip(impl.Parameters.Entries, (ip, cp) => ip.Type.IsSubtypeOf(cp.Type, false)).All(x => x);
        }

        public IParamRef<IClassSpec, Language.IType> MakeClassRef(ITypeEnvironment<Language.IType> env)
        {
            return new ClassRef<Language.IType>(this, env);
        }

        public override TDLambda CreateLambda(ITypeParametersSpec typeParameters, IParametersSpec parameters, Language.IType returnType)
        {
            TDLambda tdl = new TDLambda(typeParameters, parameters, returnType);
            this.lambdas.Add(tdl);
            return tdl;
        }

        public override TDStruct CreateStruct()
        {
            TDStruct tds = new TDStruct();
            this.structs.Add(tds);
            return tds;
        }
        public override INamedType Instantiate(ITypeEnvironment<Language.ITypeArgument> args)
        {
            return new Language.ClassType(this, args);
        }

    }
}
