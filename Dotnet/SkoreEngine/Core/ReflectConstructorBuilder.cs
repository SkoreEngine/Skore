using System;

namespace Skore
{
    public readonly unsafe struct ReflectConstructorBuilder
    {
        public readonly IntPtr Handle;

        public ReflectConstructorBuilder(IntPtr handle)
        {
            Handle = handle;
        }

        public bool IsValid => Handle != IntPtr.Zero;

        public void SetPlacementNewFn(IntPtr placementNew) => Reflection.Api.ConstructorBuilderSetPlacementNewFn(Handle, placementNew);

        public void SetNewObjectFn(IntPtr newObject) => Reflection.Api.ConstructorBuilderSetNewObjectFn(Handle, newObject);

        public void SetUserData(IntPtr userData) => Reflection.Api.ConstructorBuilderSetUserData(Handle, userData);
    }
}
