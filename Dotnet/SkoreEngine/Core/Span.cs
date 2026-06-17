using System.Runtime.InteropServices;

namespace Skore
{
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct Span<T> where T : unmanaged
    {
        private T* first;
        private T* last;

        public Span(T* first, T* last)
        {
            this.first = first;
            this.last = last;
        }

        public Span(T* data, ulong size)
        {
            first = data;
            last = data + size;
        }

        public readonly T* Data => first;

        public readonly ulong Size => (ulong)(last - first);

        public readonly bool Empty => first == last;

        public readonly ref T this[ulong index] => ref first[index];

        public readonly ref T Back() => ref first[Size - 1];
    }
}
