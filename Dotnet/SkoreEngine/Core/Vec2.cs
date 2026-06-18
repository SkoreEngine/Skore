using System;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Skore
{
    [StructLayout(LayoutKind.Sequential)]
    public struct Vec2 : IEquatable<Vec2>
    {
        public float X;
        public float Y;

        public Vec2(float value)
        {
            X = value;
            Y = value;
        }

        public Vec2(float x, float y)
        {
            X = x;
            Y = y;
        }

        public Vec2(Vec3 v)
        {
            X = v.X;
            Y = v.Y;
        }

        [UnscopedRef]
        public ref float this[int axis] => ref Unsafe.Add(ref X, axis);

        public static Vec2 operator /(Vec2 a, Vec2 b) => new Vec2(a.X / b.X, a.Y / b.Y);
        public static Vec2 operator /(Vec2 a, float b) => new Vec2(a.X / b, a.Y / b);
        public static Vec2 operator *(Vec2 a, float b) => new Vec2(a.X * b, a.Y * b);
        public static Vec2 operator *(Vec2 a, Vec2 b) => new Vec2(a.X * b.X, a.Y * b.Y);
        public static Vec2 operator +(Vec2 a, Vec2 b) => new Vec2(a.X + b.X, a.Y + b.Y);
        public static Vec2 operator -(Vec2 a, Vec2 b) => new Vec2(a.X - b.X, a.Y - b.Y);

        public static bool operator ==(Vec2 a, Vec2 b) => a.X == b.X && a.Y == b.Y;
        public static bool operator !=(Vec2 a, Vec2 b) => !(a == b);

        public bool Equals(Vec2 other) => X == other.X && Y == other.Y;
        public override bool Equals(object? obj) => obj is Vec2 other && Equals(other);
        public override int GetHashCode() => HashCode.Combine(X, Y);
        public override string ToString() => $"({X}, {Y})";

        public static Vec2 Abs(Vec2 value) => new Vec2(MathF.Abs(value.X), MathF.Abs(value.Y));

        public static float Dot(Vec2 a, Vec2 b) => a.X * b.X + a.Y * b.Y;

        public static float Length(Vec2 a) => MathF.Sqrt(Dot(a, a));

        public static float Distance(Vec2 a, Vec2 b) => Length(a - b);

        public static Vec2 Lerp(Vec2 a, Vec2 b, float alpha) => new Vec2(
            Clamp(a.X * (1 - alpha) + b.X * alpha, a.X, b.X),
            Clamp(a.Y * (1 - alpha) + b.Y * alpha, a.Y, b.Y));

        public static Vec2 Make(float x, float y) => new Vec2(x, y);

        private static float Clamp(float x, float min, float max) => x < min ? min : (x > max ? max : x);
    }
}
