using System;

namespace Skore
{
    public readonly unsafe struct ReflectValue
    {
        public readonly IntPtr Handle;

        public ReflectValue(IntPtr handle)
        {
            Handle = handle;
        }

        public bool IsValid => Handle != IntPtr.Zero;

        public string Desc => Reflection.Api.ValueGetDesc(Handle).ToString();

        public long Code => Reflection.Api.ValueGetCode(Handle);

        public IntPtr Value => Reflection.Api.ValueGetValue(Handle);

        public bool Compare(IntPtr value) => Reflection.Api.ValueCompare(Handle, value) != 0;

        public void SetToPointer(IntPtr pointer) => Reflection.Api.ValueSetToPointer(Handle, pointer);
    }
}
