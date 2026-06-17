using System;

namespace Skore
{
    public readonly unsafe struct ReflectParam
    {
        public readonly IntPtr Handle;

        public ReflectParam(IntPtr handle)
        {
            Handle = handle;
        }

        public bool IsValid => Handle != IntPtr.Zero;

        public string Name => Reflection.Api.ParamGetName(Handle).ToString();

        public ref readonly FieldProps Props => ref *Reflection.Api.ParamGetProps(Handle);
    }
}
