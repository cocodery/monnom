using System;
using System.Collections.Generic;
using System.Text;
using System.IO;
using System.Linq;

namespace Nom.Bytecode
{
    /// <summary>
    /// Shared encoding of ReadStaticField and WriteStaticField: the register, and the field's
    /// name, owning class and type. The field itself, with its initial value, is declared by
    /// the class (StaticFieldRep), and the runtime allocates it when it loads the class.
    /// </summary>
    public abstract class AStaticFieldInstruction : AInstruction
    {
        public int Register { get; }
        public IConstantRef<IStringConstant> FieldName { get; }
        public IConstantRef<IConstant> OwnerClass { get; }
        public IConstantRef<ITypeConstant> FieldType { get; }
        protected AStaticFieldInstruction(OpCode opCode, int register, IConstantRef<IStringConstant> fieldName, IConstantRef<IConstant> ownerClass, IConstantRef<ITypeConstant> fieldType) : base(opCode)
        {
            Register = register;
            FieldName = fieldName;
            OwnerClass = ownerClass;
            FieldType = fieldType;
        }

        protected override void WriteArguments(Stream ws)
        {
            ws.WriteValue(Register);
            ws.WriteValue(FieldName.ConstantID);
            ws.WriteValue(OwnerClass.ConstantID);
            ws.WriteValue(FieldType.ConstantID);
        }

        protected static (int, IConstantRef<IStringConstant>, IConstantRef<IConstant>, IConstantRef<ITypeConstant>) ReadArguments(Stream s, IReadConstantSource rcs)
        {
            var register = s.ReadInt();
            var fieldName = rcs.ReferenceStringConstant(s.ReadULong());
            var ownerClass = rcs.ReferenceConstant(s.ReadULong());
            var fieldType = rcs.ReferenceTypeConstant(s.ReadULong());
            return (register, fieldName, ownerClass, fieldType);
        }
    }

    /// <summary>Register is the target register</summary>
    public class ReadStaticFieldInstruction : AStaticFieldInstruction
    {
        public ReadStaticFieldInstruction(int targetReg, IConstantRef<IStringConstant> fieldName, IConstantRef<IConstant> ownerClass, IConstantRef<ITypeConstant> fieldType)
            : base(OpCode.ReadStaticField, targetReg, fieldName, ownerClass, fieldType)
        {
        }

        public static ReadStaticFieldInstruction Read(Stream s, IReadConstantSource rcs)
        {
            var (reg, name, owner, type) = ReadArguments(s, rcs);
            return new ReadStaticFieldInstruction(reg, name, owner, type);
        }
    }

    /// <summary>Register holds the value to write</summary>
    public class WriteStaticFieldInstruction : AStaticFieldInstruction
    {
        public WriteStaticFieldInstruction(int valueReg, IConstantRef<IStringConstant> fieldName, IConstantRef<IConstant> ownerClass, IConstantRef<ITypeConstant> fieldType)
            : base(OpCode.WriteStaticField, valueReg, fieldName, ownerClass, fieldType)
        {
        }

        public static WriteStaticFieldInstruction Read(Stream s, IReadConstantSource rcs)
        {
            var (reg, name, owner, type) = ReadArguments(s, rcs);
            return new WriteStaticFieldInstruction(reg, name, owner, type);
        }
    }
}
