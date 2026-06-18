using System;
using System.Runtime.InteropServices;

namespace Skore
{
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct NativeString
    {
        private const ulong LongFlag = 1UL << 63;

        private ulong _size;
        private fixed byte _union[24];
        private IntPtr _allocator;

        public override string ToString()
        {
            ulong length = _size & ~LongFlag;
            if (length == 0)
            {
                return string.Empty;
            }

            fixed (byte* union = _union)
            {
                byte* data = (_size & LongFlag) != 0 ? *(byte**)union : union;
                return data == null ? string.Empty : Marshal.PtrToStringUTF8((nint)data, (int)length) ?? string.Empty;
            }
        }

        private static IntPtr _type;
        private static IntPtr _ctor;
        private static bool _initialized;

        private static void Init()
        {
            if (_initialized)
            {
                return;
            }
            ReflectType type = Reflection.FindTypeByName("Skore::String")!.Value;
            _type = type.Handle;
            foreach (ReflectConstructor ctor in type.GetConstructors())
            {
                if (ctor.GetParams().Length == 1)
                {
                    _ctor = ctor.Handle;
                    break;
                }
            }
            _initialized = true;
        }

        public static void Construct(IntPtr dest, string value)
        {
            Init();
            int byteCount = System.Text.Encoding.UTF8.GetByteCount(value);
            byte* bytes = stackalloc byte[byteCount];
            System.Text.Encoding.UTF8.GetBytes(value, new Span<byte>(bytes, byteCount));
            StringView view = new StringView(bytes, (ulong)byteCount);
            new ReflectConstructor(_ctor).Construct(dest, new IntPtr[] { (IntPtr)(&view) });
        }

        public static void Destruct(IntPtr dest)
        {
            Init();
            new ReflectType(_type).Destructor(dest);
        }
    }
}
