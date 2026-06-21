using System.Runtime.InteropServices;

namespace Skore
{
    [StructLayout(LayoutKind.Sequential)]
    public readonly struct TypeId
    {
        public readonly ulong Id;

        public TypeId(ulong id)
        {
            Id = id;
        }

        public bool Valid => Id > 0;

        public static TypeId FromName(string name)
        {
            ulong hash = 0;
            foreach (byte b in System.Text.Encoding.UTF8.GetBytes(name))
            {
                unchecked
                {
                    hash = b + (hash << 6) + (hash << 16) - hash;
                }
            }
            return new TypeId(hash);
        }

        public bool Equals(TypeId other) => Id == other.Id;

        public override bool Equals(object? obj) => obj is TypeId other && Equals(other);

        public override int GetHashCode() => Id.GetHashCode();

        public static bool operator ==(TypeId a, TypeId b) => a.Id == b.Id;

        public static bool operator !=(TypeId a, TypeId b) => a.Id != b.Id;

        public static implicit operator ulong(TypeId t) => t.Id;
    }
}
