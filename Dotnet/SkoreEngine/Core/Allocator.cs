using System.Runtime.InteropServices;

namespace Skore
{
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct Allocator
    {
        public void* Context;
        public delegate* unmanaged[Cdecl]<void*, ulong, void*> MemAlloc;
        public delegate* unmanaged[Cdecl]<void*, void*, void> MemFree;
        public delegate* unmanaged[Cdecl]<void*, void*, ulong, void*> MemRealloc;

        public static Allocator* Default => (Allocator*)Reflection.Api.GetDefaultAllocator();
    }
}
