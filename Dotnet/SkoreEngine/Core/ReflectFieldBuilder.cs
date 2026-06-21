using System;

namespace Skore
{
    public readonly unsafe struct ReflectFieldBuilder
    {
        public readonly IntPtr Handle;

        public ReflectFieldBuilder(IntPtr handle)
        {
            Handle = handle;
        }

        public bool IsValid => Handle != IntPtr.Zero;

        public void SetSerializer(IntPtr serialize) => Reflection.Api.FieldBuilderSetSerializer(Handle, serialize);

        public void SetDeserialize(IntPtr deserialize) => Reflection.Api.FieldBuilderSetDeserialize(Handle, deserialize);

        public void SetCopy(IntPtr copy) => Reflection.Api.FieldBuilderSetCopy(Handle, copy);

        public void SetGet(IntPtr get) => Reflection.Api.FieldBuilderSetGet(Handle, get);

        public void SetGetObject(IntPtr getObject) => Reflection.Api.FieldBuilderSetGetObject(Handle, getObject);

        public void SetFnSet(IntPtr set) => Reflection.Api.FieldBuilderSetFnSet(Handle, set);

        public void SetFnToResource(IntPtr fnToResource) => Reflection.Api.FieldBuilderSetFnToResource(Handle, fnToResource);

        public void SetFnFromResource(IntPtr fnFromResource) => Reflection.Api.FieldBuilderSetFnFromResource(Handle, fnFromResource);

        public void SetFnGetResourceFieldInfo(IntPtr fnGetResourceFieldInfo) => Reflection.Api.FieldBuilderSetFnGetResourceFieldInfo(Handle, fnGetResourceFieldInfo);

        public void SetFunctionPointerSignature(FieldProps returnProps, FieldProps* parameters, uint count)
        {
            Reflection.Api.FieldBuilderSetFunctionPointerSignature(Handle, &returnProps, parameters, count);
        }

        public void SetUserData(IntPtr userData) => Reflection.Api.FieldBuilderSetUserData(Handle, userData);

        public ReflectAttributeBuilder AddAttribute(TypeProps props)
        {
            return new ReflectAttributeBuilder(Reflection.Api.FieldBuilderAddAttribute(Handle, &props));
        }
    }
}
