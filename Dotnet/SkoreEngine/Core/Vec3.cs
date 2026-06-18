using System;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Skore
{
    [StructLayout(LayoutKind.Sequential)]
    public struct Vec3 : IEquatable<Vec3>
    {
        public float X;
        public float Y;
        public float Z;

        public Vec3(float value)
        {
            X = value;
            Y = value;
            Z = value;
        }

        public Vec3(float x, float y, float z)
        {
            X = x;
            Y = y;
            Z = z;
        }

        public Vec3(Vec4 v)
        {
            X = v.X;
            Y = v.Y;
            Z = v.Z;
        }

        [UnscopedRef]
        public ref float this[int axis] => ref Unsafe.Add(ref X, axis);

        public static Vec3 operator /(Vec3 a, Vec3 b) => new Vec3(a.X / b.X, a.Y / b.Y, a.Z / b.Z);
        public static Vec3 operator *(Vec3 a, Vec3 b) => new Vec3(a.X * b.X, a.Y * b.Y, a.Z * b.Z);
        public static Vec3 operator -(Vec3 a, Vec3 b) => new Vec3(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
        public static Vec3 operator +(Vec3 a, Vec3 b) => new Vec3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
        public static Vec3 operator /(Vec3 a, float b) => new Vec3(a.X / b, a.Y / b, a.Z / b);
        public static Vec3 operator +(Vec3 a, float b) => new Vec3(a.X + b, a.Y + b, a.Z + b);
        public static Vec3 operator -(Vec3 a, float b) => new Vec3(a.X - b, a.Y - b, a.Z - b);
        public static Vec3 operator *(Vec3 a, float b) => new Vec3(a.X * b, a.Y * b, a.Z * b);
        public static Vec3 operator *(float a, Vec3 b) => new Vec3(a * b.X, a * b.Y, a * b.Z);
        public static Vec3 operator -(Vec3 v) => new Vec3(-v.X, -v.Y, -v.Z);

        public static bool operator ==(Vec3 a, Vec3 b) => a.X == b.X && a.Y == b.Y && a.Z == b.Z;
        public static bool operator !=(Vec3 a, Vec3 b) => !(a == b);

        public bool Equals(Vec3 other) => X == other.X && Y == other.Y && Z == other.Z;
        public override bool Equals(object? obj) => obj is Vec3 other && Equals(other);
        public override int GetHashCode() => HashCode.Combine(X, Y, Z);
        public override string ToString() => $"({X}, {Y}, {Z})";

        public static Vec3 Zero() => new Vec3(0, 0, 0);
        public static Vec3 AxisX() => new Vec3(1, 0, 0);
        public static Vec3 AxisY() => new Vec3(0, 1, 0);
        public static Vec3 AxisZ() => new Vec3(0, 0, 1);

        public static Vec3 Abs(Vec3 value) => new Vec3(MathF.Abs(value.X), MathF.Abs(value.Y), MathF.Abs(value.Z));

        public static float Dot(Vec3 a, Vec3 b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z;

        public static float Length(Vec3 a) => MathF.Sqrt(Dot(a, a));

        public static float Distance(Vec3 a, Vec3 b) => Length(a - b);

        public static Vec3 Cross(Vec3 a, Vec3 b) => new Vec3(
            a.Y * b.Z - a.Z * b.Y,
            a.Z * b.X - a.X * b.Z,
            a.X * b.Y - a.Y * b.X);

        public static Vec3 Scale(Vec3 a, float s) => new Vec3(a.X * s, a.Y * s, a.Z * s);

        public static Vec3 Normalize(Vec3 a)
        {
            float k = 1.0f / Length(a);
            return Scale(a, k);
        }

        public static Vec3 Min(Vec3 lhs, Vec3 rhs) => new Vec3(
            MathF.Min(lhs.X, rhs.X), MathF.Min(lhs.Y, rhs.Y), MathF.Min(lhs.Z, rhs.Z));

        public static Vec3 Max(Vec3 lhs, Vec3 rhs) => new Vec3(
            MathF.Max(lhs.X, rhs.X), MathF.Max(lhs.Y, rhs.Y), MathF.Max(lhs.Z, rhs.Z));

        public static Vec3 Lerp(Vec3 a, Vec3 b, float alpha) => new Vec3(
            Clamp(a.X * (1 - alpha) + b.X * alpha, a.X, b.X),
            Clamp(a.Y * (1 - alpha) + b.Y * alpha, a.Y, b.Y),
            Clamp(a.Z * (1 - alpha) + b.Z * alpha, a.Z, b.Z));

        public static Vec3 Mix(Vec3 a, Vec3 b, float t) => new Vec3(
            a.X + (b.X - a.X) * t,
            a.Y + (b.Y - a.Y) * t,
            a.Z + (b.Z - a.Z) * t);

        public static Vec3 Mix(Vec3 a, Vec3 b, Vec3 t) => new Vec3(
            a.X + (b.X - a.X) * t.X,
            a.Y + (b.Y - a.Y) * t.Y,
            a.Z + (b.Z - a.Z) * t.Z);

        public static Vec3 Radians(Vec3 other) => new Vec3(
            other.X * DegToRad, other.Y * DegToRad, other.Z * DegToRad);

        public static Vec3 Degrees(Vec3 radians) => new Vec3(
            radians.X * RadToDeg, radians.Y * RadToDeg, radians.Z * RadToDeg);

        public static Vec3 Sin(Vec3 other) => new Vec3(MathF.Sin(other.X), MathF.Sin(other.Y), MathF.Sin(other.Z));

        public static Vec3 Cos(Vec3 other) => new Vec3(MathF.Cos(other.X), MathF.Cos(other.Y), MathF.Cos(other.Z));

        public static Vec3 Make(Vec4 value) => new Vec3(value.X, value.Y, value.Z);

        public static Vec3 Round(Vec3 v) => new Vec3(
            MathF.Round(v.X, MidpointRounding.AwayFromZero),
            MathF.Round(v.Y, MidpointRounding.AwayFromZero),
            MathF.Round(v.Z, MidpointRounding.AwayFromZero));

        private const float DegToRad = 0.01745329251994329576923690768489f;
        private const float RadToDeg = 57.295779513082320876798154814105f;

        private static float Clamp(float x, float min, float max) => x < min ? min : (x > max ? max : x);
    }
}
