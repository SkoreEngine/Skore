using System;

namespace Skore
{
    public readonly unsafe struct ReflectAttributeBuilder
    {
        public readonly IntPtr Handle;

        public ReflectAttributeBuilder(IntPtr handle)
        {
            Handle = handle;
        }

        public bool IsValid => Handle != IntPtr.Zero;

        public void SetGetValue(IntPtr fnGetValue) => Reflection.Api.AttributeBuilderSetGetValue(Handle, fnGetValue);
    }
}
