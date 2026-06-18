using System;

namespace Skore
{
    public readonly unsafe struct ReflectType
    {
        public readonly IntPtr Handle;

        public ReflectType(IntPtr handle)
        {
            Handle = handle;
        }

        public bool IsValid => Handle != IntPtr.Zero;

        public string Name => Reflection.Api.TypeGetName(Handle).ToString();

        public string SimpleName => Reflection.Api.TypeGetSimpleName(Handle).ToString();

        public string Scope => Reflection.Api.TypeGetScope(Handle).ToString();

        public ref readonly TypeProps Props => ref *Reflection.Api.TypeGetProps(Handle);

        public TypeId TypeId => Props.TypeId;

        public uint Version => Reflection.Api.TypeGetVersion(Handle);

        public bool IsDerivedOf(TypeId typeId) => Reflection.Api.TypeIsDerivedOf(Handle, typeId) != 0;

        public TypeId[] GetBaseTypes()
        {
            UnmanagedSpan<TypeId> span = Reflection.Api.TypeGetBaseTypes(Handle);
            int count = (int)span.Size;
            var result = new TypeId[count];
            for (int i = 0; i < count; i++)
            {
                result[i] = span[(ulong)i];
            }
            return result;
        }

        public ReflectField[] GetFields()
        {
            UnmanagedSpan<IntPtr> span = Reflection.Api.TypeGetFields(Handle);
            int count = (int)span.Size;
            var result = new ReflectField[count];
            for (int i = 0; i < count; i++)
            {
                result[i] = new ReflectField(span[(ulong)i]);
            }
            return result;
        }

        public ReflectField? FindField(string name)
        {
            IntPtr handle = Reflection.InvokeNamed(Handle, Reflection.Api.TypeFindField, name);
            return handle == IntPtr.Zero ? null : new ReflectField(handle);
        }

        public ReflectFunction[] GetFunctions()
        {
            UnmanagedSpan<IntPtr> span = Reflection.Api.TypeGetFunctions(Handle);
            int count = (int)span.Size;
            var result = new ReflectFunction[count];
            for (int i = 0; i < count; i++)
            {
                result[i] = new ReflectFunction(span[(ulong)i]);
            }
            return result;
        }

        public ReflectFunction? FindFunction(string name)
        {
            IntPtr handle = Reflection.InvokeNamed(Handle, Reflection.Api.TypeFindFunction, name);
            return handle == IntPtr.Zero ? null : new ReflectFunction(handle);
        }

        public ReflectConstructor[] GetConstructors()
        {
            UnmanagedSpan<IntPtr> span = Reflection.Api.TypeGetConstructors(Handle);
            int count = (int)span.Size;
            var result = new ReflectConstructor[count];
            for (int i = 0; i < count; i++)
            {
                result[i] = new ReflectConstructor(span[(ulong)i]);
            }
            return result;
        }

        public ReflectConstructor? GetDefaultConstructor()
        {
            IntPtr handle = Reflection.Api.TypeGetDefaultConstructor(Handle);
            return handle == IntPtr.Zero ? null : new ReflectConstructor(handle);
        }

        public ReflectValue[] GetValues()
        {
            UnmanagedSpan<IntPtr> span = Reflection.Api.TypeGetValues(Handle);
            int count = (int)span.Size;
            var result = new ReflectValue[count];
            for (int i = 0; i < count; i++)
            {
                result[i] = new ReflectValue(span[(ulong)i]);
            }
            return result;
        }

        public ReflectValue? FindValueByName(string name)
        {
            IntPtr handle = Reflection.InvokeNamed(Handle, Reflection.Api.TypeFindValueByName, name);
            return handle == IntPtr.Zero ? null : new ReflectValue(handle);
        }

        public ReflectValue? FindValueByCode(long code)
        {
            IntPtr handle = Reflection.Api.TypeFindValueByCode(Handle, code);
            return handle == IntPtr.Zero ? null : new ReflectValue(handle);
        }

        public IntPtr NewObject() => Reflection.Api.TypeNewObject(Handle);

        public void Destroy(IntPtr instance) => Reflection.Api.TypeDestroy(Handle, instance);

        public void Destructor(IntPtr instance) => Reflection.Api.TypeDestructor(Handle, instance);

        public void Copy(IntPtr source, IntPtr dest) => Reflection.Api.TypeCopy(Handle, source, dest);

        public IntPtr GetAttribute(TypeId attributeId) => Reflection.Api.TypeGetAttribute(Handle, attributeId);
    }
}
