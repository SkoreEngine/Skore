using System;

namespace Skore
{
    public readonly unsafe struct ReflectField
    {
        public readonly IntPtr Handle;

        public ReflectField(IntPtr handle)
        {
            Handle = handle;
        }

        public bool IsValid => Handle != IntPtr.Zero;

        public string Name => Reflection.Api.FieldGetName(Handle).ToString();

        public uint Index => Reflection.Api.FieldGetIndex(Handle);

        public ref readonly FieldProps Props => ref *Reflection.Api.FieldGetProps(Handle);

        public void Get(IntPtr instance, IntPtr dest, nuint destSize) => Reflection.Api.FieldGet(Handle, instance, dest, destSize);

        public void Set(IntPtr instance, IntPtr src, nuint srcSize) => Reflection.Api.FieldSet(Handle, instance, src, srcSize);

        public T Get<T>(IntPtr instance) where T : unmanaged
        {
            T value = default;
            Reflection.Api.FieldGet(Handle, instance, (IntPtr)(&value), (nuint)sizeof(T));
            return value;
        }

        public void Set<T>(IntPtr instance, T value) where T : unmanaged
        {
            Reflection.Api.FieldSet(Handle, instance, (IntPtr)(&value), (nuint)sizeof(T));
        }

        public IntPtr GetObject(IntPtr instance) => Reflection.Api.FieldGetObject(Handle, instance);

        public IntPtr GetAttribute(TypeId attributeId) => Reflection.Api.FieldGetAttribute(Handle, attributeId);
    }
}
