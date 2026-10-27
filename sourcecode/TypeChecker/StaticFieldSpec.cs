using System;
using System.Collections.Generic;
using System.Text;
using System.Linq;
using Nom.Language;

namespace Nom.TypeChecker
{
    /// <summary>
    /// The kind of literal a static field is initialized with; the runtime gives the field
    /// this value the first time it is read before being written
    /// </summary>
    public enum StaticFieldInitKind : byte
    {
        None = 0,
        Int = 1,
        Float = 2,
        Bool = 3,
        Null = 4,
        String = 5
    }

    public class StaticFieldSpec : IStaticFieldSpec, IStaticFieldDecl
    {
        public StaticFieldSpec(INamespaceSpec container, Parser.Identifier name, IType type, Visibility visibility, bool isReadonly, StaticFieldInitKind initKind = StaticFieldInitKind.None, long initInt = 0, double initFloat = 0.0, string initString = null)
        {
            Container = container;
            Identifier = name;
            Type = type;
            Visibility = visibility;
            IsReadonly = isReadonly;
            InitKind = initKind;
            InitInt = initInt;
            InitFloat = initFloat;
            InitString = initString;
        }
        public INamespaceSpec Container { get; }
        public Parser.Identifier Identifier { get; }
        public string Name => Identifier.Name;
        public IType Type { get; }
        public Visibility Visibility { get; }
        public bool IsReadonly { get; }
        public bool IsVolatile => false;

        public StaticFieldInitKind InitKind { get; }
        /// <summary>the value for Int, and 0/1 for Bool</summary>
        public long InitInt { get; }
        public double InitFloat { get; }
        public string InitString { get; }

        /// <summary>
        /// A static field is shared by all instantiations of its class, so its type must not
        /// mention type variables
        /// </summary>
        public static bool ContainsTypeVariables(IType type)
        {
            TypeVisitor<object, bool> tv = new TypeVisitor<object, bool>();
            tv.DefaultAction = (t, o) => false;
            tv.VisitTypeVariable = (t, o) => true;
            tv.VisitClassType = (t, o) => t.Substitutions.Any(kvp => ContainsTypeVariables(kvp.Value.AsType));
            tv.VisitInterfaceType = (t, o) => t.Substitutions.Any(kvp => ContainsTypeVariables(kvp.Value.AsType));
            tv.VisitMaybeType = (t, o) => ContainsTypeVariables(t.PotentialType);
            tv.VisitProbablyType = (t, o) => ContainsTypeVariables(t.PotentialType);
            return type.Visit(tv);
        }
    }
}
