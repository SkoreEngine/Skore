using System;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Skore
{
    [StructLayout(LayoutKind.Sequential)]
    public struct Vec4 : IEquatable<Vec4>
    {
        public float X;
        public float Y;
        public float Z;
        public float W;

        public Vec4(float value)
        {
            X = value;
            Y = value;
            Z = value;
            W = value;
        }

        public Vec4(float x, float y, float z, float w)
        {
            X = x;
            Y = y;
            Z = z;
            W = w;
        }

        public Vec4(Vec3 vec, float w)
        {
            X = vec.X;
            Y = vec.Y;
            Z = vec.Z;
            W = w;
        }

        [UnscopedRef]
        public ref float this[int axis] => ref Unsafe.Add(ref X, axis);

        public static Vec4 operator /(Vec4 a, Vec4 b) => new Vec4(a.X / b.X, a.Y / b.Y, a.Z / b.Z, a.W / b.W);
        public static Vec4 operator +(Vec4 a, Vec4 b) => new Vec4(a.X + b.X, a.Y + b.Y, a.Z + b.Z, a.W + b.W);
        public static Vec4 operator *(Vec4 a, Vec4 b) => new Vec4(a.X * b.X, a.Y * b.Y, a.Z * b.Z, a.W * b.W);
        public static Vec4 operator -(Vec4 a, Vec4 b) => new Vec4(a.X - b.X, a.Y - b.Y, a.Z - b.Z, a.W - b.W);
        public static Vec4 operator /(Vec4 a, float b) => new Vec4(a.X / b, a.Y / b, a.Z / b, a.W / b);
        public static Vec4 operator *(Vec4 a, float b) => new Vec4(a.X * b, a.Y * b, a.Z * b, a.W * b);

        public static bool operator ==(Vec4 a, Vec4 b) => a.X == b.X && a.Y == b.Y && a.Z == b.Z && a.W == b.W;
        public static bool operator !=(Vec4 a, Vec4 b) => !(a == b);

        public bool Equals(Vec4 other) => X == other.X && Y == other.Y && Z == other.Z && W == other.W;
        public override bool Equals(object? obj) => obj is Vec4 other && Equals(other);
        public override int GetHashCode() => HashCode.Combine(X, Y, Z, W);
        public override string ToString() => $"({X}, {Y}, {Z}, {W})";

        public static float Dot(Vec4 a, Vec4 b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z;

        public static float Length(Vec4 a) => MathF.Sqrt(Dot(a, a));

        public static float Distance(Vec4 a, Vec4 b) => Length(a - b);

        public static Vec4 Scale(Vec4 a, float s) => new Vec4(a.X * s, a.Y * s, a.Z * s, a.W);

        public static Vec4 Normalize(Vec4 a)
        {
            float k = 1.0f / Length(a);
            return Scale(a, k);
        }

        public static Vec4 Make(Vec3 value, float w = 0.0f) => new Vec4(value.X, value.Y, value.Z, w);

        public static Vec4 Make(Vec2 value1, Vec2 value2) => new Vec4(value1.X, value1.Y, value2.X, value2.Y);

        public static Vec4 Lerp(Vec4 a, Vec4 b, float alpha) => new Vec4(
            Clamp(a.X * (1 - alpha) + b.X * alpha, a.X, b.X),
            Clamp(a.Y * (1 - alpha) + b.Y * alpha, a.Y, b.Y),
            Clamp(a.Z * (1 - alpha) + b.Z * alpha, a.Z, b.Z),
            Clamp(a.W * (1 - alpha) + b.W * alpha, a.W, b.W));

        public static Vec4 Round(Vec4 v) => new Vec4(
            MathF.Round(v.X, MidpointRounding.AwayFromZero),
            MathF.Round(v.Y, MidpointRounding.AwayFromZero),
            MathF.Round(v.Z, MidpointRounding.AwayFromZero),
            MathF.Round(v.W, MidpointRounding.AwayFromZero));

        private static float Clamp(float x, float min, float max) => x < min ? min : (x > max ? max : x);
    }
}
