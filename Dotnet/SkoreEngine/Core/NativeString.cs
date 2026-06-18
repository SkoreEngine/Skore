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
    }
}
