using System;
using System.Collections.Generic;
using System.Text;
using System.IO;
using Nom.Language;

namespace Nom.Bytecode
{
    /// <summary>
    /// A static field declared by a class. The runtime allocates it when it loads the class and
    /// gives it its initial value: InitPayload is the Int value, the Float's bits, 0/1 for Bool,
    /// or the string constant's ID (see TypeChecker.StaticFieldInitKind); without an initializer
    /// (InitKind None) the field starts as the default of its type.
    /// </summary>
    public class StaticFieldRep : IStaticFieldSpec
    {
        public StaticFieldRep(IClassSpec container, IConstantRef<IStringConstant> nameConstant, IConstantRef<ITypeConstant> typeConstant, bool isReadonly, Visibility visibility, byte initKind, ulong initPayload)
        {
            Container = container;
            NameConstant = nameConstant;
            TypeConstant = typeConstant;
            IsReadonly = isReadonly;
            Visibility = visibility;
            InitKind = initKind;
            InitPayload = initPayload;
        }
        INamespaceSpec IMember.Container => Container;
        public IClassSpec Container { get; }

        public IConstantRef<IStringConstant> NameConstant { get; }
        public IConstantRef<ITypeConstant> TypeConstant { get; }
        public bool IsReadonly { get; }
        public bool IsVolatile => false;
        public string Name => NameConstant.Constant.Value;
        public IType Type => TypeConstant.Constant.Value;
        public Visibility Visibility { get; }
        public byte InitKind { get; }
        public ulong InitPayload { get; }

        public void WriteByteCode(Stream ws)
        {
            ws.WriteByte((byte)BytecodeInternalElementType.StaticField);
            ws.WriteValue(NameConstant.ConstantID);
            ws.WriteValue(TypeConstant.ConstantID);
            ws.WriteByte((byte)Visibility);
            ws.WriteByte(IsReadonly ? (byte)FieldRep.FieldRepFlags.ReadOnly : (byte)FieldRep.FieldRepFlags.None);
            ws.WriteByte(InitKind);
            ws.WriteValue(InitPayload);
        }
        public static StaticFieldRep Read(IClassSpec container, Stream s, IReadConstantSource rcs)
        {
            byte tag = s.ReadActualByte();
            if (tag != (byte)BytecodeInternalElementType.StaticField)
            {
                throw new NomBytecodeException("Bytecode malformed!");
            }
            var nameconst = rcs.ReferenceStringConstant(s.ReadULong());
            var tconst = rcs.ReferenceTypeConstant(s.ReadULong());
            var visibility = (Visibility)s.ReadActualByte();
            var flags = (FieldRep.FieldRepFlags)s.ReadActualByte();
            var initKind = s.ReadActualByte();
            var initPayload = s.ReadULong();
            return new StaticFieldRep(container, nameconst, tconst, (flags & FieldRep.FieldRepFlags.ReadOnly) == FieldRep.FieldRepFlags.ReadOnly, visibility, initKind, initPayload);
        }
    }
}
