using System;
using System.Collections.Generic;
using System.Text;
using System.Linq;
using Nom.Language;

namespace Nom.TypeChecker.StdLib
{
    internal class Bool : AStdClass
    {
        public static Bool Instance = new Bool();
        private Bool() : base("Bool", Object.Instance) { }
        private List<IMethodSpec> methods = null;
        public override IEnumerable<IMethodSpec> Methods
        {
            get
            {
                if (methods == null)
                {
                    methods = new List<IMethodSpec>();
                    methods.Add(new MethodSpec("ToString", this, new TypeParametersSpec(new List<ITypeParameterSpec>()), new ParametersSpec(new List<IParameterSpec>()), String.Instance.ClassType));
                }
                return methods;
            }
        }
    }
}
