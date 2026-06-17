using System;

namespace Skore
{
    public readonly unsafe struct ReflectFunction
    {
        public readonly IntPtr Handle;

        public ReflectFunction(IntPtr handle)
        {
            Handle = handle;
        }

        public bool IsValid => Handle != IntPtr.Zero;

        public string Name => Reflection.Api.FunctionGetName(Handle).ToString();

        public string SimpleName => Reflection.Api.FunctionGetSimpleName(Handle).ToString();

        public bool IsStatic => Reflection.Api.FunctionIsStatic(Handle) != 0;

        public FieldProps GetReturn()
        {
            FieldProps props = default;
            Reflection.Api.FunctionGetReturn(Handle, &props);
            return props;
        }

        public ReflectParam[] GetParams()
        {
            Span<IntPtr> span = Reflection.Api.FunctionGetParams(Handle);
            int count = (int)span.Size;
            var result = new ReflectParam[count];
            for (int i = 0; i < count; i++)
            {
                result[i] = new ReflectParam(span[(ulong)i]);
            }
            return result;
        }

        public void Invoke(IntPtr instance, IntPtr ret, IntPtr[]? paramValues)
        {
            if (paramValues == null || paramValues.Length == 0)
            {
                Reflection.Api.FunctionInvoke(Handle, instance, ret, null);
                return;
            }

            fixed (IntPtr* p = paramValues)
            {
                Reflection.Api.FunctionInvoke(Handle, instance, ret, p);
            }
        }

        public void Invoke(IntPtr instance, IntPtr ret, IntPtr* paramValues) => Reflection.Api.FunctionInvoke(Handle, instance, ret, paramValues);

        public IntPtr GetFunctionPointer() => Reflection.Api.FunctionGetFunctionPointer(Handle);

        public IntPtr GetAttribute(TypeId attributeId) => Reflection.Api.FunctionGetAttribute(Handle, attributeId);
    }
}
