using System;
using System.Text;

namespace Skore
{
    public static unsafe class Reflection
    {
        private static ReflectionApi _api;
        private static bool _initialized;

        internal static ref readonly ReflectionApi Api => ref _api;

        public static bool Initialized => _initialized;

        internal static void Initialize(IntPtr apiPtr)
        {
            if (apiPtr == IntPtr.Zero)
            {
                throw new ArgumentNullException(nameof(apiPtr), "native reflection api pointer is null");
            }

            _api = *(ReflectionApi*)apiPtr;
            _initialized = true;
        }

        public static ReflectType? FindTypeByName(string name)
        {
            IntPtr handle = InvokeNamed(_api.FindTypeByName, name);
            return handle == IntPtr.Zero ? null : new ReflectType(handle);
        }

        public static ReflectType? FindTypeById(TypeId typeId)
        {
            IntPtr handle = _api.FindTypeById(typeId);
            return handle == IntPtr.Zero ? null : new ReflectType(handle);
        }

        public static ReflectType[] GetAllTypes()
        {
            uint count = 0;
            IntPtr* data = (IntPtr*)_api.GetAllTypes(&count);
            var result = new ReflectType[count];
            for (uint i = 0; i < count; i++)
            {
                result[i] = new ReflectType(data[i]);
            }
            return result;
        }

        public static TypeId[] GetDerivedTypes(TypeId baseType)
        {
            uint count = 0;
            TypeId* data = (TypeId*)_api.GetDerivedTypes(baseType, &count);
            var result = new TypeId[count];
            for (uint i = 0; i < count; i++)
            {
                result[i] = data[i];
            }
            return result;
        }

        public static uint Version => _api.GetReflectionVersion();

        internal static IntPtr InvokeNamed(delegate* unmanaged[Cdecl]<StringView, IntPtr> fn, string name)
        {
            fixed (char* chars = name)
            {
                int byteCount = Encoding.UTF8.GetByteCount(chars, name.Length);
                byte* buffer = stackalloc byte[byteCount];
                Encoding.UTF8.GetBytes(chars, name.Length, buffer, byteCount);
                return fn(new StringView(buffer, (ulong)byteCount));
            }
        }

        internal static IntPtr InvokeNamed(IntPtr handle, delegate* unmanaged[Cdecl]<IntPtr, StringView, IntPtr> fn, string name)
        {
            fixed (char* chars = name)
            {
                int byteCount = Encoding.UTF8.GetByteCount(chars, name.Length);
                byte* buffer = stackalloc byte[byteCount];
                Encoding.UTF8.GetBytes(chars, name.Length, buffer, byteCount);
                return fn(handle, new StringView(buffer, (ulong)byteCount));
            }
        }

        public static ReflectTypeBuilder RegisterType(string name, string simpleName, TypeProps props)
        {
            fixed (char* nameChars = name)
            fixed (char* simpleChars = simpleName)
            {
                int nameBytes = Encoding.UTF8.GetByteCount(nameChars, name.Length);
                byte* nameBuffer = stackalloc byte[nameBytes];
                Encoding.UTF8.GetBytes(nameChars, name.Length, nameBuffer, nameBytes);

                int simpleBytes = Encoding.UTF8.GetByteCount(simpleChars, simpleName.Length);
                byte* simpleBuffer = stackalloc byte[simpleBytes];
                Encoding.UTF8.GetBytes(simpleChars, simpleName.Length, simpleBuffer, simpleBytes);

                IntPtr handle = _api.RegisterType(
                    new StringView(nameBuffer, (ulong)nameBytes),
                    new StringView(simpleBuffer, (ulong)simpleBytes),
                    &props);
                return new ReflectTypeBuilder(handle);
            }
        }
    }
}
