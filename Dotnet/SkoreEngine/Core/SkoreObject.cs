using System;

namespace Skore
{
    public partial class SkoreObject
    {
        public IntPtr Handle;

        protected SkoreObject()
        {
            Handle = TypeRegistration.AllocInstance(this);
        }

        public SkoreObject(IntPtr handle)
        {
            Handle = handle;
        }
    }
}
