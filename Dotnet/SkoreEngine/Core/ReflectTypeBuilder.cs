using System;
using System.Text;

namespace Skore
{
    public readonly unsafe struct ReflectTypeBuilder
    {
        public readonly IntPtr Handle;

        public ReflectTypeBuilder(IntPtr handle)
        {
            Handle = handle;
        }

        public bool IsValid => Handle != IntPtr.Zero;

        public ReflectFieldBuilder AddField(FieldProps props, string name)
        {
            fixed (char* chars = name)
            {
                int byteCount = Encoding.UTF8.GetByteCount(chars, name.Length);
                byte* buffer = stackalloc byte[byteCount];
                Encoding.UTF8.GetBytes(chars, name.Length, buffer, byteCount);
                return new ReflectFieldBuilder(Reflection.Api.TypeBuilderAddField(Handle, &props, new StringView(buffer, (ulong)byteCount)));
            }
        }

        public ReflectFunctionBuilder AddFunction(string name)
        {
            return new ReflectFunctionBuilder(Reflection.InvokeNamed(Handle, Reflection.Api.TypeBuilderAddFunction, name));
        }

        public ReflectConstructorBuilder AddConstructor(FieldProps* props, StringView* names, uint size)
        {
            return new ReflectConstructorBuilder(Reflection.Api.TypeBuilderAddConstructor(Handle, props, names, size));
        }

        public ReflectAttributeBuilder AddAttribute(TypeProps props)
        {
            return new ReflectAttributeBuilder(Reflection.Api.TypeBuilderAddAttribute(Handle, &props));
        }

        public ReflectValueBuilder AddValue(string valueDesc)
        {
            return new ReflectValueBuilder(Reflection.InvokeNamed(Handle, Reflection.Api.TypeBuilderAddValue, valueDesc));
        }

        public void SetFnDestroy(IntPtr fnDestroy) => Reflection.Api.TypeBuilderSetFnDestroy(Handle, fnDestroy);

        public void SetFnCopy(IntPtr fnCopy) => Reflection.Api.TypeBuilderSetFnCopy(Handle, fnCopy);

        public void SetFnDestructor(IntPtr fnDestructor) => Reflection.Api.TypeBuilderSetFnDestructor(Handle, fnDestructor);

        public void SetFnBatchDestructor(IntPtr fnBatchDestructor) => Reflection.Api.TypeBuilderSetFnBatchDestructor(Handle, fnBatchDestructor);

        public void AddBaseType(TypeId typeId) => Reflection.Api.TypeBuilderAddBaseType(Handle, typeId);
    }
}
