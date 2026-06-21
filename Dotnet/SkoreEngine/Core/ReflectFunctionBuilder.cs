using System;

namespace Skore
{
    public readonly unsafe struct ReflectFunctionBuilder
    {
        public readonly IntPtr Handle;

        public ReflectFunctionBuilder(IntPtr handle)
        {
            Handle = handle;
        }

        public bool IsValid => Handle != IntPtr.Zero;

        public void AddParams(StringView* names, FieldProps* props, uint size) => Reflection.Api.FunctionBuilderAddParams(Handle, names, props, size);

        public void SetFnInvoke(IntPtr fnInvoke) => Reflection.Api.FunctionBuilderSetFnInvoke(Handle, fnInvoke);

        public void SetFunctionPointer(IntPtr functionPointer) => Reflection.Api.FunctionBuilderSetFunctionPointer(Handle, functionPointer);

        public void SetReturnProps(FieldProps returnProps) => Reflection.Api.FunctionBuilderSetReturnProps(Handle, &returnProps);

        public void SetIsStatic(bool isStatic) => Reflection.Api.FunctionBuilderSetIsStatic(Handle, (byte)(isStatic ? 1 : 0));

        public ReflectAttributeBuilder AddAttribute(TypeProps props)
        {
            return new ReflectAttributeBuilder(Reflection.Api.FunctionBuilderAddAttribute(Handle, &props));
        }
    }
}
