using System;

namespace Skore
{
    public readonly unsafe struct ReflectValueBuilder
    {
        public readonly IntPtr Handle;

        public ReflectValueBuilder(IntPtr handle)
        {
            Handle = handle;
        }

        public bool IsValid => Handle != IntPtr.Zero;

        public void SetFnGetValue(IntPtr fnGetValue) => Reflection.Api.ValueBuilderSetFnGetValue(Handle, fnGetValue);

        public void SetFnGetCode(IntPtr fnGetCode) => Reflection.Api.ValueBuilderSetFnGetCode(Handle, fnGetCode);

        public void SetFnCompare(IntPtr fnCompare) => Reflection.Api.ValueBuilderSetFnCompare(Handle, fnCompare);

        public void SetFnSetToPointer(IntPtr fnSetToPointer) => Reflection.Api.ValueBuilderSetFnSetToPointer(Handle, fnSetToPointer);
    }
}
