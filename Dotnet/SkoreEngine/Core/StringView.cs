using System.Runtime.InteropServices;

namespace Skore
{
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct StringView
    {
        private byte* str;
        private ulong size;

        public StringView(byte* str, ulong size)
        {
            this.str = str;
            this.size = size;
        }

        public readonly byte* Data => str;

        public readonly ulong Size => size;

        public readonly bool Empty => size == 0;

        public readonly byte this[ulong index] => str[index];

        public override readonly string ToString()
        {
            return str == null ? string.Empty : Marshal.PtrToStringUTF8((nint)str, (int)size) ?? string.Empty;
        }
    }
}
