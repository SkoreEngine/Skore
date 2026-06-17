using System;
using System.Runtime.InteropServices;

namespace Skore
{
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct TypeProps
    {
        public TypeId TypeId;
        public TypeId TypeApi;
        public StringView Name;
        public IntPtr GetTypeApi;
        public IntPtr FnCopy;
        public IntPtr FnDestroy;
        public uint Size;
        public uint Alignment;
        public byte IsTriviallyCopyableRaw;
        public byte IsEnumRaw;
        private fixed byte _pad[6];

        public readonly bool IsTriviallyCopyable => IsTriviallyCopyableRaw != 0;

        public readonly bool IsEnum => IsEnumRaw != 0;
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct FieldProps
    {
        public TypeProps Type;
        public TypeId OwnerId;
        public byte IsConstRaw;
        public byte IsPointerRaw;
        public byte IsReferenceRaw;
        private fixed byte _pad[5];

        public readonly bool IsConst => IsConstRaw != 0;

        public readonly bool IsPointer => IsPointerRaw != 0;

        public readonly bool IsReference => IsReferenceRaw != 0;
    }
}
