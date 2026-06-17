using System;

namespace Skore
{
    public readonly unsafe struct ReflectConstructor
    {
        public readonly IntPtr Handle;

        public ReflectConstructor(IntPtr handle)
        {
            Handle = handle;
        }

        public bool IsValid => Handle != IntPtr.Zero;

        public ReflectParam[] GetParams()
        {
            Span<IntPtr> span = Reflection.Api.ConstructorGetParams(Handle);
            int count = (int)span.Size;
            var result = new ReflectParam[count];
            for (int i = 0; i < count; i++)
            {
                result[i] = new ReflectParam(span[(ulong)i]);
            }
            return result;
        }

        public IntPtr NewObject(IntPtr[]? paramValues)
        {
            if (paramValues == null || paramValues.Length == 0)
            {
                return Reflection.Api.ConstructorNewObject(Handle, null);
            }

            fixed (IntPtr* p = paramValues)
            {
                return Reflection.Api.ConstructorNewObject(Handle, p);
            }
        }

        public void Construct(IntPtr memory, IntPtr[]? paramValues)
        {
            if (paramValues == null || paramValues.Length == 0)
            {
                Reflection.Api.ConstructorConstruct(Handle, memory, null);
                return;
            }

            fixed (IntPtr* p = paramValues)
            {
                Reflection.Api.ConstructorConstruct(Handle, memory, p);
            }
        }
    }
}
