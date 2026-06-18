using System;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Skore
{
    [StructLayout(LayoutKind.Sequential)]
    public struct Mat4 : IEquatable<Mat4>
    {
        public Vec4 M0;
        public Vec4 M1;
        public Vec4 M2;
        public Vec4 M3;

        public Mat4(Vec4 vec0, Vec4 vec1, Vec4 vec2, Vec4 vec3)
        {
            M0 = vec0;
            M1 = vec1;
            M2 = vec2;
            M3 = vec3;
        }

        public Mat4(float value)
        {
            M0 = new Vec4(value, 0, 0, 0);
            M1 = new Vec4(0, value, 0, 0);
            M2 = new Vec4(0, 0, value, 0);
            M3 = new Vec4(0, 0, 0, value);
        }

        [UnscopedRef]
        public ref Vec4 this[int axis] => ref Unsafe.Add(ref M0, axis);

        [UnscopedRef]
        public ref Vec4 Right => ref M0;

        [UnscopedRef]
        public ref Vec4 Up => ref M1;

        [UnscopedRef]
        public ref Vec4 Dir => ref M2;

        [UnscopedRef]
        public ref Vec4 Position => ref M3;

        public Mat4 Identity(float value)
        {
            this = new Mat4(value);
            return this;
        }

        public Mat4 Identity() => Identity(1.0f);

        [UnscopedRef]
        private ref float A(int index) => ref Unsafe.Add(ref Unsafe.As<Vec4, float>(ref M0), index);

        public static readonly Mat4 IdentityMatrix = new Mat4(1.0f);

        public static Mat4 operator *(Mat4 a, Mat4 b)
        {
            Mat4 mat = default;
            for (int c = 0; c < 4; ++c)
            {
                for (int r = 0; r < 4; ++r)
                {
                    mat[c][r] = 0.0f;
                    for (int k = 0; k < 4; ++k)
                    {
                        mat[c][r] += a[k][r] * b[c][k];
                    }
                }
            }
            return mat;
        }

        public static Mat4 operator *(Mat4 m, float a)
        {
            Mat4 mat = default;
            for (int c = 0; c < 4; ++c)
            {
                for (int r = 0; r < 4; ++r)
                {
                    mat[c][r] = m[c][r] * a;
                }
            }
            return mat;
        }

        public static Vec4 operator *(Mat4 m, Vec4 v)
        {
            Vec4 mov0 = new Vec4(v[0]);
            Vec4 mov1 = new Vec4(v[1]);
            Vec4 mul0 = m[0] * mov0;
            Vec4 mul1 = m[1] * mov1;
            Vec4 add0 = mul0 + mul1;
            Vec4 mov2 = new Vec4(v[2]);
            Vec4 mov3 = new Vec4(v[3]);
            Vec4 mul2 = m[2] * mov2;
            Vec4 mul3 = m[3] * mov3;
            Vec4 add1 = mul2 + mul3;
            Vec4 add2 = add0 + add1;
            return add2;
        }

        public static bool operator ==(Mat4 a, Mat4 b) => a.M0 == b.M0 && a.M1 == b.M1 && a.M2 == b.M2 && a.M3 == b.M3;
        public static bool operator !=(Mat4 a, Mat4 b) => !(a == b);

        public bool Equals(Mat4 other) => this == other;
        public override bool Equals(object? obj) => obj is Mat4 other && Equals(other);
        public override int GetHashCode() => HashCode.Combine(M0, M1, M2, M3);

        public static Mat4 Make(ReadOnlySpan<float> values)
        {
            Mat4 mat = default;
            for (int i = 0; i < 16; ++i)
            {
                mat.A(i) = values[i];
            }
            return mat;
        }

        public static Mat4 Scale(Vec3 scale)
        {
            Mat4 outMatrix = new Mat4(1.0f);
            outMatrix.A(0) = scale.X;
            outMatrix.A(5) = scale.Y;
            outMatrix.A(10) = scale.Z;
            return outMatrix;
        }

        public static Mat4 Scale(Mat4 m, Vec3 scale)
        {
            Mat4 ret = m;
            ret[0][0] *= scale.X;
            ret[1][1] *= scale.Y;
            ret[2][2] *= scale.Z;
            ret[3][3] = 1;
            return ret;
        }

        public static Mat4 RotateX(float angleRadians)
        {
            Mat4 outMatrix = new Mat4(1.0f);
            float c = MathF.Cos(angleRadians);
            float s = MathF.Sin(angleRadians);
            outMatrix.A(5) = c;
            outMatrix.A(6) = -s;
            outMatrix.A(9) = s;
            outMatrix.A(10) = c;
            return outMatrix;
        }

        public static Mat4 RotateY(float angleRadians)
        {
            Mat4 outMatrix = new Mat4(1.0f);
            float c = MathF.Cos(angleRadians);
            float s = MathF.Sin(angleRadians);
            outMatrix.A(0) = c;
            outMatrix.A(2) = -s;
            outMatrix.A(8) = s;
            outMatrix.A(10) = c;
            return outMatrix;
        }

        public static Mat4 RotateZ(float angleRadians)
        {
            Mat4 outMatrix = new Mat4(1.0f);
            float c = MathF.Cos(angleRadians);
            float s = MathF.Sin(angleRadians);
            outMatrix.A(0) = c;
            outMatrix.A(1) = -s;
            outMatrix.A(4) = s;
            outMatrix.A(5) = c;
            return outMatrix;
        }

        public static Mat4 Rotate(Mat4 m, float rx, float ry, float rz)
        {
            Mat4 r = m;
            float cosX = MathF.Cos(rx);
            float sinX = MathF.Sin(rx);
            float cosY = MathF.Cos(ry);
            float sinY = MathF.Sin(ry);
            float cosZ = MathF.Cos(rz);
            float sinZ = MathF.Sin(rz);
            r[0][0] = cosY * cosZ;
            r[0][1] = cosY * sinZ;
            r[0][2] = -sinY;
            r[1][0] = sinX * sinY * cosZ - cosX * sinZ;
            r[1][1] = sinX * sinY * sinZ + cosX * cosZ;
            r[1][2] = sinX * cosY;
            r[2][0] = cosX * sinY * cosZ + sinX * sinZ;
            r[2][1] = cosX * sinY * sinZ - sinX * cosZ;
            r[2][2] = cosX * cosY;
            return r;
        }

        public static Mat4 PerspectiveRH_ZO(float fovRadians, float aspectRatio, float zNear, float zFar)
        {
            Mat4 matrix = new Mat4(0.0f);
            float halfTanFov = MathF.Tan(fovRadians * 0.5f);
            matrix[0][0] = 1.0f / (aspectRatio * halfTanFov);
            matrix[1][1] = 1.0f / halfTanFov;
            matrix[2][2] = zFar / (zNear - zFar);
            matrix[2][3] = -1.0f;
            matrix[3][2] = -(zFar * zNear) / (zFar - zNear);
            return matrix;
        }

        public static Mat4 PerspectiveRH_NO(float fovRadians, float aspectRatio, float zNear, float zFar)
        {
            Mat4 matrix = new Mat4(0.0f);
            float halfTanFov = MathF.Tan(fovRadians * 0.5f);
            matrix[0][0] = 1.0f / (aspectRatio * halfTanFov);
            matrix[1][1] = 1.0f / halfTanFov;
            matrix[2][2] = -((zFar + zNear) / (zFar - zNear));
            matrix[2][3] = -1.0f;
            matrix[3][2] = -((2.0f * zFar * zNear) / (zFar - zNear));
            return matrix;
        }

        public static Mat4 Perspective(float fovRadians, float aspectRatio, float zNear, float zFar)
        {
            return PerspectiveRH_ZO(fovRadians, aspectRatio, zFar, zNear);
        }

        public static Mat4 LookAt(Vec3 eye, Vec3 center, Vec3 up)
        {
            Mat4 mat = new Mat4(1.0f);

            Vec3 z = eye - center;
            z = Vec3.Normalize(z);
            Vec3 y = up;
            Vec3 x = Vec3.Cross(y, z);
            y = Vec3.Cross(z, x);
            x = Vec3.Normalize(x);
            y = Vec3.Normalize(y);

            mat[0][0] = x.X;
            mat[1][0] = x.Y;
            mat[2][0] = x.Z;
            mat[3][0] = -Vec3.Dot(x, eye);
            mat[0][1] = y.X;
            mat[1][1] = y.Y;
            mat[2][1] = y.Z;
            mat[3][1] = -Vec3.Dot(y, eye);
            mat[0][2] = -z.X;
            mat[1][2] = -z.Y;
            mat[2][2] = -z.Z;
            mat[3][2] = Vec3.Dot(z, eye);
            mat[0][3] = 0;
            mat[1][3] = 0;
            mat[2][3] = 0;
            mat[3][3] = 1.0f;
            return mat;
        }

        public static Mat4 Translate(Mat4 m, float x, float y, float z)
        {
            Mat4 matrix = m;
            matrix[3][0] = x;
            matrix[3][1] = y;
            matrix[3][2] = z;
            return matrix;
        }

        public static Mat4 Translate(Mat4 m, Vec3 v) => Translate(m, v.X, v.Y, v.Z);

        public static Mat4 Translate(float x, float y, float z)
        {
            Mat4 matrix = new Mat4(1.0f);
            matrix[3][0] = x;
            matrix[3][1] = y;
            matrix[3][2] = z;
            return matrix;
        }

        public static Mat4 Translate(Vec3 v) => Translate(v.X, v.Y, v.Z);

        public static Mat4 Inverse(Mat4 mat)
        {
            Span<float> m = stackalloc float[16];
            for (int i = 0; i < 16; ++i)
            {
                m[i] = mat.A(i);
            }

            float t0 = m[10] * m[15];
            float t1 = m[14] * m[11];
            float t2 = m[6] * m[15];
            float t3 = m[14] * m[7];
            float t4 = m[6] * m[11];
            float t5 = m[10] * m[7];
            float t6 = m[2] * m[15];
            float t7 = m[14] * m[3];
            float t8 = m[2] * m[11];
            float t9 = m[10] * m[3];
            float t10 = m[2] * m[7];
            float t11 = m[6] * m[3];
            float t12 = m[8] * m[13];
            float t13 = m[12] * m[9];
            float t14 = m[4] * m[13];
            float t15 = m[12] * m[5];
            float t16 = m[4] * m[9];
            float t17 = m[8] * m[5];
            float t18 = m[0] * m[13];
            float t19 = m[12] * m[1];
            float t20 = m[0] * m[9];
            float t21 = m[8] * m[1];
            float t22 = m[0] * m[5];
            float t23 = m[4] * m[1];

            Span<float> o = stackalloc float[16];

            o[0] = (t0 * m[5] + t3 * m[9] + t4 * m[13]) - (t1 * m[5] + t2 * m[9] + t5 * m[13]);
            o[1] = (t1 * m[1] + t6 * m[9] + t9 * m[13]) - (t0 * m[1] + t7 * m[9] + t8 * m[13]);
            o[2] = (t2 * m[1] + t7 * m[5] + t10 * m[13]) - (t3 * m[1] + t6 * m[5] + t11 * m[13]);
            o[3] = (t5 * m[1] + t8 * m[5] + t11 * m[9]) - (t4 * m[1] + t9 * m[5] + t10 * m[9]);

            float d = 1.0f / (m[0] * o[0] + m[4] * o[1] + m[8] * o[2] + m[12] * o[3]);

            o[0] = d * o[0];
            o[1] = d * o[1];
            o[2] = d * o[2];
            o[3] = d * o[3];
            o[4] = d * ((t1 * m[4] + t2 * m[8] + t5 * m[12]) - (t0 * m[4] + t3 * m[8] + t4 * m[12]));
            o[5] = d * ((t0 * m[0] + t7 * m[8] + t8 * m[12]) - (t1 * m[0] + t6 * m[8] + t9 * m[12]));
            o[6] = d * ((t3 * m[0] + t6 * m[4] + t11 * m[12]) - (t2 * m[0] + t7 * m[4] + t10 * m[12]));
            o[7] = d * ((t4 * m[0] + t9 * m[4] + t10 * m[8]) - (t5 * m[0] + t8 * m[4] + t11 * m[8]));
            o[8] = d * ((t12 * m[7] + t15 * m[11] + t16 * m[15]) - (t13 * m[7] + t14 * m[11] + t17 * m[15]));
            o[9] = d * ((t13 * m[3] + t18 * m[11] + t21 * m[15]) - (t12 * m[3] + t19 * m[11] + t20 * m[15]));
            o[10] = d * ((t14 * m[3] + t19 * m[7] + t22 * m[15]) - (t15 * m[3] + t18 * m[7] + t23 * m[15]));
            o[11] = d * ((t17 * m[3] + t20 * m[7] + t23 * m[11]) - (t16 * m[3] + t21 * m[7] + t22 * m[11]));
            o[12] = d * ((t14 * m[10] + t17 * m[14] + t13 * m[6]) - (t16 * m[14] + t12 * m[6] + t15 * m[10]));
            o[13] = d * ((t20 * m[14] + t12 * m[2] + t19 * m[10]) - (t18 * m[10] + t21 * m[14] + t13 * m[2]));
            o[14] = d * ((t18 * m[6] + t23 * m[14] + t15 * m[2]) - (t22 * m[14] + t14 * m[2] + t19 * m[6]));
            o[15] = d * ((t22 * m[10] + t16 * m[2] + t21 * m[6]) - (t20 * m[6] + t23 * m[10] + t17 * m[2]));

            Mat4 outMatrix = default;
            for (int i = 0; i < 16; ++i)
            {
                outMatrix.A(i) = o[i];
            }
            return outMatrix;
        }

        public static Mat4 Ortho_RH_NO(float left, float right, float bottom, float top, float zNear, float zFar)
        {
            Mat4 mat = new Mat4(1.0f);
            mat[0][0] = 2 / (right - left);
            mat[1][1] = 2 / (top - bottom);
            mat[2][2] = -2 / (zFar - zNear);
            mat[3][0] = -(right + left) / (right - left);
            mat[3][1] = -(top + bottom) / (top - bottom);
            mat[3][2] = -(zFar + zNear) / (zFar - zNear);
            return mat;
        }

        public static Mat4 Ortho_RH_ZO(float left, float right, float bottom, float top, float zNear, float zFar)
        {
            Mat4 mat = new Mat4(1.0f);
            mat[0][0] = 2.0f / (right - left);
            mat[1][1] = 2.0f / (top - bottom);
            mat[2][2] = 1.0f / (zFar - zNear);
            mat[3][0] = -(right + left) / (right - left);
            mat[3][1] = -(top + bottom) / (top - bottom);
            mat[3][2] = -zNear / (zFar - zNear);
            return mat;
        }

        public static Mat4 Ortho(float left, float right, float bottom, float top, float zNear, float zFar)
        {
            return Ortho_RH_ZO(left, right, bottom, top, zNear, zFar);
        }

        public static Mat4 Transpose(Mat4 matrix)
        {
            Mat4 outMatrix = default;
            outMatrix.A(0) = matrix.A(0);
            outMatrix.A(1) = matrix.A(4);
            outMatrix.A(2) = matrix.A(8);
            outMatrix.A(3) = matrix.A(12);
            outMatrix.A(4) = matrix.A(1);
            outMatrix.A(5) = matrix.A(5);
            outMatrix.A(6) = matrix.A(9);
            outMatrix.A(7) = matrix.A(13);
            outMatrix.A(8) = matrix.A(2);
            outMatrix.A(9) = matrix.A(6);
            outMatrix.A(10) = matrix.A(10);
            outMatrix.A(11) = matrix.A(14);
            outMatrix.A(12) = matrix.A(3);
            outMatrix.A(13) = matrix.A(7);
            outMatrix.A(14) = matrix.A(11);
            outMatrix.A(15) = matrix.A(15);
            return outMatrix;
        }

        public static Mat4 OrthoNormalize(Mat4 mat)
        {
            Mat4 outMatrix = mat;
            outMatrix.M1 = Vec4.Normalize(outMatrix.M1);
            outMatrix.M2 = Vec4.Normalize(outMatrix.M2);
            outMatrix.M0 = Vec4.Normalize(outMatrix.M0);
            return outMatrix;
        }

        public static void Decompose(Mat4 mat, out Vec3 translation, out Vec3 rotation, out Vec3 scale)
        {
            scale = new Vec3(
                Vec3.Length(new Vec3(mat.M0)),
                Vec3.Length(new Vec3(mat.M1)),
                Vec3.Length(new Vec3(mat.M2)));

            Mat4 mathNorm = OrthoNormalize(mat);

            rotation = new Vec3(
                MathF.Atan2(mathNorm[1][2], mathNorm[2][2]),
                MathF.Atan2(-mathNorm[0][2], MathF.Sqrt(mathNorm[1][2] * mathNorm[1][2] + mathNorm[2][2] * mathNorm[2][2])),
                MathF.Atan2(mathNorm[0][1], mathNorm[0][0]));

            translation = new Vec3(mathNorm.M3.X, mathNorm.M3.Y, mathNorm.M3.Z);
        }

        public static void Decompose(Mat4 mat, out Vec3 translation)
        {
            Mat4 mathNorm = OrthoNormalize(mat);
            translation = new Vec3(mathNorm.M3.X, mathNorm.M3.Y, mathNorm.M3.Z);
        }

        public static Vec3 GetScale(Mat4 mat) => new Vec3(
            Vec3.Length(new Vec3(mat.M0)),
            Vec3.Length(new Vec3(mat.M1)),
            Vec3.Length(new Vec3(mat.M2)));

        public static Vec3 GetTranslation(Mat4 mat)
        {
            Mat4 mathNorm = OrthoNormalize(mat);
            return new Vec3(mathNorm.M3.X, mathNorm.M3.Y, mathNorm.M3.Z);
        }

        public static Quat GetQuaternion(Mat4 pMat)
        {
            Mat4 mat = OrthoNormalize(pMat);

            float tr = mat.M0.X + mat.M1.Y + mat.M2.Z;

            if (tr >= 0.0f)
            {
                float s = MathF.Sqrt(tr + 1.0f);
                float ise = 0.5f / s;
                return new Quat(
                    (mat.M1.Z - mat.M2.Y) * ise,
                    (mat.M2.X - mat.M0.Z) * ise,
                    (mat.M0.Y - mat.M1.X) * ise,
                    0.5f * s);
            }
            else
            {
                int i = 0;
                if (mat.M1.Y > mat.M0.X) i = 1;
                if (mat.M2.Z > mat[i][i]) i = 2;

                if (i == 0)
                {
                    float s = MathF.Sqrt(mat.M0.X - (mat.M1.Y + mat.M2.Z) + 1);
                    float ise = 0.5f / s;
                    return new Quat(
                        0.5f * s,
                        (mat.M1.X + mat.M0.Y) * ise,
                        (mat.M0.Z + mat.M2.X) * ise,
                        (mat.M1.Z - mat.M2.Y) * ise);
                }
                else if (i == 1)
                {
                    float s = MathF.Sqrt(mat.M1.Y - (mat.M2.Z + mat.M0.X) + 1);
                    float ise = 0.5f / s;
                    return new Quat(
                        (mat.M1.X + mat.M0.Y) * ise,
                        0.5f * s,
                        (mat.M2.Y + mat.M1.Z) * ise,
                        (mat.M2.X - mat.M0.Z) * ise);
                }
                else
                {
                    float s = MathF.Sqrt(mat.M2.Z - (mat.M0.X + mat.M1.Y) + 1);
                    float ise = 0.5f / s;
                    return new Quat(
                        (mat.M0.Z + mat.M2.X) * ise,
                        (mat.M2.Y + mat.M1.Z) * ise,
                        0.5f * s,
                        (mat.M0.Y - mat.M1.X) * ise);
                }
            }
        }

        public static Vec3 GetUpVector(Mat4 matrix) => new Vec3(matrix[1][0], matrix[1][1], matrix[1][2]);

        public static Vec3 GetForwardVector(Mat4 matrix) => new Vec3(-matrix[2][0], -matrix[2][1], -matrix[2][2]);

        public static Mat4 EulerXYZ(Vec3 angles)
        {
            float sx = MathF.Sin(angles[0]);
            float cx = MathF.Cos(angles[0]);
            float sy = MathF.Sin(angles[1]);
            float cy = MathF.Cos(angles[1]);
            float sz = MathF.Sin(angles[2]);
            float cz = MathF.Cos(angles[2]);

            float czsx = cz * sx;
            float cxcz = cx * cz;
            float sysz = sy * sz;

            Mat4 dest = default;
            dest[0][0] = cy * cz;
            dest[0][1] = czsx * sy + cx * sz;
            dest[0][2] = -cxcz * sy + sx * sz;
            dest[1][0] = -cy * sz;
            dest[1][1] = cxcz - sx * sysz;
            dest[1][2] = czsx + cx * sysz;
            dest[2][0] = sy;
            dest[2][1] = -cy * sx;
            dest[2][2] = cx * cy;
            dest[0][3] = 0.0f;
            dest[1][3] = 0.0f;
            dest[2][3] = 0.0f;
            dest[3][0] = 0.0f;
            dest[3][1] = 0.0f;
            dest[3][2] = 0.0f;
            dest[3][3] = 1.0f;
            return dest;
        }
    }
}
