using System;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Skore
{
    [StructLayout(LayoutKind.Sequential)]
    public struct Quat : IEquatable<Quat>
    {
        public float X;
        public float Y;
        public float Z;
        public float W;

        public Quat(float x, float y, float z, float w)
        {
            X = x;
            Y = y;
            Z = z;
            W = w;
        }

        public Quat(Vec3 eulerAngle)
        {
            Vec3 c = Vec3.Cos(eulerAngle * 0.5f);
            Vec3 s = Vec3.Sin(eulerAngle * 0.5f);
            X = s.X * c.Y * c.Z - c.X * s.Y * s.Z;
            Y = c.X * s.Y * c.Z + s.X * c.Y * s.Z;
            Z = c.X * c.Y * s.Z - s.X * s.Y * c.Z;
            W = c.X * c.Y * c.Z + s.X * s.Y * s.Z;
        }

        public Quat(float s, Vec3 v)
        {
            X = v.X;
            Y = v.Y;
            Z = v.Z;
            W = s;
        }

        [UnscopedRef]
        public ref float this[int axis] => ref Unsafe.Add(ref X, axis);

        public float Normal()
        {
            float n = X * X + Y * Y + Z * Z + W * W;
            if (n == 1)
            {
                return 1;
            }
            return MathF.Sqrt(n);
        }

        public Quat Normalize()
        {
            float normal = Normal();
            X /= normal;
            Y /= normal;
            Z /= normal;
            W /= normal;
            return this;
        }

        public float Dot(Quat other) => X * other.X + Y * other.Y + Z * other.Z + W * other.W;

        public static Quat operator +(Quat q, Quat p) => new Quat(q.W + p.W, q.X + p.X, q.Y + p.Y, q.Z + p.Z);

        public static Quat operator *(Quat p, Quat q)
        {
            Quat dest = default;
            dest.W = p.W * q.W - p.X * q.X - p.Y * q.Y - p.Z * q.Z;
            dest.X = p.W * q.X + p.X * q.W + p.Y * q.Z - p.Z * q.Y;
            dest.Y = p.W * q.Y + p.Y * q.W + p.Z * q.X - p.X * q.Z;
            dest.Z = p.W * q.Z + p.Z * q.W + p.X * q.Y - p.Y * q.X;
            return dest;
        }

        public static Vec3 operator *(Quat q, Vec3 v)
        {
            Vec3 qv = new Vec3(q.X, q.Y, q.Z);
            Vec3 uv = Vec3.Cross(qv, v);
            Vec3 uuv = Vec3.Cross(qv, uv);
            return v + (uv * q.W + uuv) * 2.0f;
        }

        public static bool operator ==(Quat a, Quat b) => a.X == b.X && a.Y == b.Y && a.Z == b.Z && a.W == b.W;
        public static bool operator !=(Quat a, Quat b) => !(a == b);

        public bool Equals(Quat other) => X == other.X && Y == other.Y && Z == other.Z && W == other.W;
        public override bool Equals(object? obj) => obj is Quat other && Equals(other);
        public override int GetHashCode() => HashCode.Combine(X, Y, Z, W);
        public override string ToString() => $"({X}, {Y}, {Z}, {W})";

        public static Quat Normalized(Quat value)
        {
            Quat retValue = value;
            return retValue.Normalize();
        }

        public static float DotProduct(Quat value, Quat other)
        {
            Quat retValue = value;
            return retValue.Dot(other);
        }

        public static Quat Slerp(Quat q0, Quat q1, float percentage)
        {
            Quat v0 = Normalized(q0);
            Quat v1 = Normalized(q1);

            float dot = DotProduct(v0, v1);

            if (dot < 0.0f)
            {
                v1.X = -v1.X;
                v1.Y = -v1.Y;
                v1.Z = -v1.Z;
                v1.W = -v1.W;
                dot = -dot;
            }

            const float dotThreshold = 0.9995f;
            if (dot > dotThreshold)
            {
                Quat outQuaternion = new Quat(
                    v0.X + ((v1.X - v0.X) * percentage),
                    v0.Y + ((v1.Y - v0.Y) * percentage),
                    v0.Z + ((v1.Z - v0.Z) * percentage),
                    v0.W + ((v1.W - v0.W) * percentage));

                return outQuaternion.Normalize();
            }

            float theta0 = MathF.Acos(dot);
            float theta = theta0 * percentage;
            float sinTheta = MathF.Sin(theta);
            float sinTheta0 = MathF.Sin(theta0);

            float s0 = MathF.Cos(theta) - dot * sinTheta / sinTheta0;
            float s1 = sinTheta / sinTheta0;

            return new Quat(
                v0.X * s0 + v1.X * s1,
                v0.Y * s0 + v1.Y * s1,
                v0.Z * s0 + v1.Z * s1,
                v0.W * s0 + v1.W * s1);
        }

        public static float Roll(Quat q) => MathF.Atan2(
            2.0f * (q.X * q.Y + q.W * q.Z),
            q.W * q.W + q.X * q.X - q.Y * q.Y - q.Z * q.Z);

        public static float Yaw(Quat q) => MathF.Asin(Clamp(-2.0f * (q.X * q.Z - q.W * q.Y), -1.0f, 1.0f));

        public static float Pitch(Quat q)
        {
            float y = 2.0f * (q.Y * q.Z + q.W * q.X);
            float x = q.W * q.W - q.X * q.X - q.Y * q.Y + q.Z * q.Z;
            return MathF.Atan2(y, x);
        }

        public static Quat AngleAxis(float angle, Vec3 v)
        {
            float s = MathF.Sin(angle * 0.5f);
            return new Quat(MathF.Cos(angle * 0.5f), v * s);
        }

        public static Vec3 EulerAngles(Quat quat) => new Vec3(Pitch(quat), Yaw(quat), Roll(quat));

        public static Mat4 ToMatrix4(Quat q)
        {
            Mat4 result = new Mat4(1.0f);
            float qxx = q.X * q.X;
            float qyy = q.Y * q.Y;
            float qzz = q.Z * q.Z;
            float qxz = q.X * q.Z;
            float qxy = q.X * q.Y;
            float qyz = q.Y * q.Z;
            float qwx = q.W * q.X;
            float qwy = q.W * q.Y;
            float qwz = q.W * q.Z;

            result[0][0] = 1.0f - 2.0f * (qyy + qzz);
            result[0][1] = 2.0f * (qxy + qwz);
            result[0][2] = 2.0f * (qxz - qwy);
            result[0][3] = 0.0f;

            result[1][0] = 2.0f * (qxy - qwz);
            result[1][1] = 1.0f - 2.0f * (qxx + qzz);
            result[1][2] = 2.0f * (qyz + qwx);
            result[1][3] = 0.0f;

            result[2][0] = 2.0f * (qxz + qwy);
            result[2][1] = 2.0f * (qyz - qwx);
            result[2][2] = 1.0f - 2.0f * (qxx + qyy);
            result[2][3] = 0.0f;

            result[3][0] = 0.0f;
            result[3][1] = 0.0f;
            result[3][2] = 0.0f;
            result[3][3] = 1.0f;

            return result;
        }

        private static float Clamp(float x, float min, float max) => x < min ? min : (x > max ? max : x);
    }
}
