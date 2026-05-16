#pragma once
#include "FixedArray.hpp"
#include "Hash.hpp"
#include "Skore/Common.hpp"

#include <cmath>


namespace Skore
{
	constexpr float PI = 3.14159265358f;
	constexpr float HalfPI = PI / 2.0f;
	constexpr float QuarterPI = PI / 4.0f;
	constexpr float RadToDeg = 180.0f / PI;
	constexpr float DegToRad = PI / 180.0f;
	constexpr float OneDivPI = 1.0f / PI;
	constexpr float TwoPI = PI * 2.0f;
	constexpr float Sqrt2 = 1.414213562f;
	constexpr float Epsilon = 0.0001f;

	struct Vec3;
	struct Vec2;
	struct Mat34;
	struct Vec4;
	struct Vec4D;

	template <typename T>
	struct Compare
	{
		constexpr static bool IsEqual(T a, T b)
		{
			return a == b;
		}
	};

	template<typename T>
	struct CompareRound
	{
		constexpr static bool IsEqual(T a, T b)
		{
			T rounded_a = std::round(a * 100) / 100.0f;
			T rounded_b = std::round(b * 100) / 100.0f;
			return std::abs(rounded_a - rounded_b) < 0.001f;
		}
	};

	struct Mat4;

	constexpr Vec4 operator*(const Mat4& m, const Vec4& v);

	struct Extent
	{
		u32 width{};
		u32 height{};

		constexpr bool   operator<(const u32& b) const;
		constexpr bool   operator>(const u32& b) const;
		constexpr Extent operator*(const Extent& b) const;
		constexpr Extent operator*(const u32& b) const;
		constexpr Extent operator*(const Vec2& b) const;

		friend bool operator==(const Extent& lhs, const Extent& rhs)
		{
			return lhs.width == rhs.width
				&& lhs.height == rhs.height;
		}

		friend bool operator!=(const Extent& lhs, const Extent& rhs)
		{
			return !(lhs == rhs);
		}

		explicit operator bool() const noexcept
		{
			return width > 0 && height > 0;
		}
	};

	struct Extent3D
	{
		u32 width{};
		u32 height{};
		u32 depth{};

		constexpr Extent3D() = default;
		constexpr Extent3D(u32 width, u32 height, u32 depth) : width(width), height(height), depth(depth) {}
		constexpr Extent3D(const Extent& extent) : width(extent.width), height(extent.height), depth(1) {}

		constexpr bool     operator<(const u32& b) const;
		constexpr bool     operator>(const u32& b) const;
		constexpr Extent3D operator*(const Extent3D& b) const;
		constexpr Extent3D operator*(const u32& b) const;
		constexpr Extent3D operator*(const Vec3& b) const;

		static inline Extent3D Max(const Extent3D& lhs, const Extent3D& rhs);
	};

	struct Offset3D
	{
		i32 x = 0;
		i32 y = 0;
		i32 z = 0;
	};


	struct Rect
	{
		i32 x{};
		i32 y{};
		u32 width{};
		u32 height{};
	};

	struct Vec2
	{
		union
		{
			struct
			{
				union
				{
					Float x, r, width;
				};

				union
				{
					Float y, g, height;
				};
			};

			Float c[2] = {};
		};

		constexpr              Vec2();
		constexpr              Vec2(Float value);
		constexpr              Vec2(Float _x, Float _y);
		constexpr              Vec2(const Vec3& v);

		constexpr Vec2         operator/(const Vec2& b) const;
		constexpr Vec2         operator/(const Float& b) const;
		constexpr Vec2         operator*(const Float& b) const;
		constexpr Vec2         operator*(const Vec2& b) const;
		constexpr Vec2         operator+(const Vec2& b) const;
		constexpr Vec2         operator-(const Vec2& b) const;
		constexpr Vec2         operator>>(const int vl) const;
		constexpr Vec2         operator<<(const int vl) const;
		constexpr bool         operator==(const Float& b) const;
		constexpr bool         operator!=(const Float& b) const;
		constexpr const Float& operator[](usize axis) const;
		constexpr Float&       operator[](usize axis);
		constexpr Vec2&        operator*=(const Vec2& rhs);
		constexpr Vec2&        operator/=(const Vec2& rhs);
		constexpr Vec2&        operator-=(const Vec2& rhs);
		constexpr Vec2&        operator+=(const Vec2& rhs);
		constexpr bool         operator>(const Float& b) const;

		template <typename T>
		constexpr Vec2& operator*=(const T& rhs);

		static constexpr Vec2 Abs(const Vec2& value);
		static constexpr Float Dot(const Vec2& a, const Vec2& b);
		static inline Float Length(const Vec2& a);
		static inline Float Distance(const Vec2& a, const Vec2& b);
		template <typename Type>
		static constexpr Vec2 Lerp(const Vec2& a, const Vec2& b, Type alpha);
		static constexpr Vec2 Make(const Float* value);
		static constexpr Vec2 Make(const Float& x, const Float& y);
	};

	struct Vec3
	{
		union
		{
			struct
			{
				Float x;
				Float y;
				Float z;
			};

			Float coord[3] = {};
		};

		constexpr              Vec3();
		constexpr              Vec3(Float value);
		constexpr              Vec3(Float* value);
		constexpr              Vec3(Float _x, Float _y, Float _z);
		constexpr              Vec3(const Vec4& v);

		constexpr const Float& operator[](int axis) const;
		constexpr Float&       operator[](int axis);
		constexpr Vec3         operator/(const Vec3& b) const;
		constexpr Vec3         operator*(const Vec3& b) const;
		constexpr Vec3         operator*(const Float& b) const;
		constexpr Vec3         operator-(const Vec3& b) const;
		constexpr Vec3         operator/(const Float& b) const;
		constexpr Vec3         operator+(const Float& b) const;
		constexpr Vec3         operator-(const Float& b) const;
		constexpr Vec3         operator>>(const int vl) const;
		constexpr Vec3         operator<<(const int vl) const;
		constexpr Vec3&        operator*=(const Vec3& rhs);
		constexpr Vec3&        operator+=(const Vec3& rhs);
		constexpr Vec3&        operator-=(const Vec3& rhs);
		constexpr const Float* operator[](usize axis) const;
		constexpr Float*       operator[](usize axis);
		constexpr Vec3         operator-() const;
		constexpr bool         operator==(const Float& b) const;
		constexpr bool         operator==(const Vec3& b) const;
		constexpr bool         operator>(const Float& b) const;

		template <typename U>
		constexpr Vec3 operator/=(U u);

		constexpr static Vec3 Zero();
		constexpr static Vec3 AxisX();
		constexpr static Vec3 AxisY();
		constexpr static Vec3 AxisZ();

		static constexpr Vec3 Abs(const Vec3& value);
		static constexpr Float Dot(const Vec3& a, const Vec3& b);
		static inline Float Length(const Vec3& a);
		static inline Float Distance(const Vec3& a, const Vec3& b);
		static constexpr Vec3 Cross(const Vec3& a, const Vec3& b);
		static constexpr Vec3 Scale(const Vec3& a, Float s);
		static inline Vec3 Normalize(const Vec3& a);
		static inline Vec3 Min(const Vec3& lhs, const Vec3& rhs);
		static inline Vec3 Max(const Vec3& lhs, const Vec3& rhs);
		template <typename Type>
		static constexpr Vec3 Lerp(const Vec3& a, const Vec3& b, Type alpha);
		static constexpr Vec3 Mix(const Vec3& a, const Vec3& b, Float t);
		static constexpr Vec3 Mix(const Vec3& a, const Vec3& b, const Vec3& t);
		static constexpr Vec3 Radians(const Vec3& other);
		static inline Vec3 Degrees(Vec3 radians);
		static constexpr Vec3 Sin(const Vec3& other);
		static constexpr Vec3 Cos(const Vec3& other);
		static constexpr Vec3 Make(const Float* value);
		static constexpr Vec3 Make(const Vec4& value);
		static constexpr Vec3 Make(const f64* value);
		static constexpr Vec3 Round(const Vec3& v);
	};

	constexpr Vec3 operator*(const Float& b, const Vec3& a);
	constexpr Vec3 operator+(const Vec3& a, const Vec3& b);

	//TODO - use templates
	struct IVec2
	{
		union
		{
			struct
			{
				i32 x;
				i32 y;
			};

			i32 coord[2] = {};
		};
	};

	//TODO - use templates
	struct IVec4
	{
		union
		{
			struct
			{
				i32 x;
				i32 y;
				i32 z;
				i32 w;
			};

			i32 coord[4] = {};
		};
	};

	struct Vec4
	{
		union
		{
			struct
			{
				union
				{
					Float x, r, width;
				};

				union
				{
					Float y, g, height;
				};

				union
				{
					Float z, b;
				};

				union
				{
					Float w, a;
				};
			};

			Float c[4] = {};
		};

		Vec4() = default;
		constexpr              Vec4(Float value);
		constexpr              Vec4(Float* value);
		constexpr              Vec4(Float _x, Float _y, Float _z, Float _w);
		constexpr              Vec4(const Vec3& vec, Float _w);
		constexpr              Vec4(const Vec4D& vec);
		constexpr const Float& operator[](u32 axis) const;
		constexpr Float&       operator[](u32 axis);
		constexpr Vec4         operator/(const Vec4& b) const;
		constexpr Vec4         operator+(const Vec4& b) const;
		constexpr Vec4         operator*(const Vec4& b) const;
		constexpr Vec4         operator-(const Vec4& b) const;
		constexpr Vec4         operator/(const Float& b) const;
		constexpr Vec4         operator*(const Float& b) const;
		constexpr Vec4&        operator/=(const Float& b);
		constexpr Vec4         operator+=(const Vec4& b) const;
		constexpr Vec4         operator>>(i32 vl) const;
		constexpr Vec4         operator<<(i32 vl) const;
		constexpr bool         operator==(const Float& b) const;
		constexpr bool         operator==(const Vec4& b) const;
		constexpr bool         operator!=(const Float& b) const;
		constexpr bool         operator!=(const Vec4& b) const;

		static constexpr Float Dot(const Vec4& a, const Vec4& b);
		static inline Float Length(const Vec4& a);
		static inline Float Distance(const Vec4& a, const Vec4& b);
		static constexpr Vec4 Scale(const Vec4& a, Float s);
		static inline Vec4 Normalize(const Vec4& a);
		static constexpr Vec4 Make(const Vec3& value, Float w = 0.0);
		static constexpr Vec4 Make(const Float* value);
		static constexpr Vec4 Make(const Vec2& value1, const Vec2& value2);
		template <typename Type>
		static constexpr Vec4 Lerp(const Vec4& a, const Vec4& b, Type alpha);
		static constexpr Vec4 Round(const Vec4& v);
	};

	struct Vec4D
	{
		union
		{
			struct
			{
				union
				{
					f64 x, r, width;
				};

				union
				{
					f64 y, g, height;
				};

				union
				{
					f64 z, b;
				};

				union
				{
					f64 w, a;
				};
			};

			f64 c[4] = {};
		};
	};

	struct Quat
	{
		union
		{
			struct
			{
				Float x;
				Float y;
				Float z;
				Float w;
			};

			Float c[4] = {};
		};

		constexpr              Quat();
		constexpr              Quat(Float x, Float y, Float z, Float w);
		constexpr              Quat(const Vec3& eulerAngle);
		constexpr              Quat(Float s, const Vec3& v);
		constexpr const Float& operator[](int axis) const;
		constexpr Float&       operator[](int axis);
		constexpr Quat&        operator=(const Mat4& m);
		constexpr bool         operator==(const Quat&) const;
		constexpr Float        Normal() const;
		constexpr Quat&        Normalize();
		constexpr Float        Dot(const Quat& other) const;

		static constexpr Quat Normalized(const Quat& value);
		static constexpr Float DotProduct(const Quat& value, const Quat& other);
		static constexpr Quat Slerp(const Quat& q0, const Quat& q1, f32 percentage);
		static inline Float Roll(const Quat& q);
		static inline Float Yaw(const Quat& q);
		static inline Float Pitch(const Quat& q);
		static inline Quat AngleAxis(const Float& angle, const Vec3& v);
		static inline Vec3 EulerAngles(const Quat& quat);
		static constexpr Mat4 ToMatrix4(const Quat& q);
		template <typename Type>
		static inline Quat Make(const f64* t);
	};

	struct Mat4
	{
		union
		{
			Vec4  m[4] = {0.f};
			Float a[16];

			struct
			{
				Vec4 right{}, up{}, dir{}, position{};
			} v;
		};

		constexpr             Mat4();
		constexpr             Mat4(const Vec4& vec0, const Vec4& vec1, const Vec4& vec2, const Vec4& vec3);
		constexpr             Mat4(const Float value);
		constexpr             Mat4(const Mat34& mat34);
		constexpr Mat4&       Identity();
		constexpr Mat4&       Identity(Float value);
		constexpr const Vec4& operator[](usize axis) const;
		constexpr Vec4&       operator[](usize axis);

		static inline Mat4 Scale(const Vec3& scale);
		static inline Mat4 Scale(const Mat4& m, const Vec3& scale);
		static inline Mat4 RotateX(f32 angleRadians);
		static inline Mat4 RotateY(f32 angleRadians);
		static inline Mat4 RotateZ(f32 angleRadians);
		static inline Mat4 Rotate(const Mat4& m, f32 rx, f32 ry, f32 rz);
		static inline Mat4 PerspectiveRH_ZO(f32 fovRadians, f32 aspectRatio, f32 zNear, f32 zFar);
		static inline Mat4 PerspectiveRH_NO(f32 fovRadians, f32 aspectRatio, f32 zNear, f32 zFar);
		static inline Mat4 Perspective(f32 fovRadians, f32 aspectRatio, f32 zNear, f32 zFar);
		static inline Mat4 LookAt(const Vec3& eye, const Vec3& center, const Vec3& up);
		static inline Mat4 Translate(const Mat4& m, f32 x, f32 y, f32 z);
		static inline Mat4 Translate(const Mat4& m, const Vec3& v);
		static inline Mat4 Translate(f32 x, f32 y, f32 z);
		static inline Mat4 Translate(const Vec3& v);
		static inline Mat4 Inverse(const Mat4& mat);
		static inline Mat4 Ortho_RH_NO(f32 left, f32 right, f32 bottom, f32 top, f32 zNear, f32 zFar);
		static inline Mat4 Ortho_RH_ZO(f32 left, f32 right, f32 bottom, f32 top, f32 zNear, f32 zFar);
		static inline Mat4 Ortho(f32 left, f32 right, f32 bottom, f32 top, f32 zNear, f32 zFar);
		static inline Mat4 Transpose(const Mat4& matrix);
		static inline Mat4 OrthoNormalize(const Mat4& mat);
		static inline void Decompose(const Mat4& mat, Vec3& translation, Vec3& rotation, Vec3& scale);
		static inline void Decompose(const Mat4& mat, Vec3& translation);
		static inline Vec3 GetScale(const Mat4& mat);
		static inline Vec3 GetTranslation(const Mat4& mat);
		static inline Quat GetQuaternion(const Mat4 pMat);
		static constexpr Vec3 GetUpVector(const Mat4& matrix);
		static constexpr Vec3 GetForwardVector(const Mat4& matrix);
		static inline Mat4 EulerXYZ(Vec3 angles);
	};

	struct Mat34
	{
		f32 matrix[3][4];

		constexpr Mat34() = default;

		constexpr Mat34(const Mat4& m)
		{
			matrix[0][0] = m[0][0];
			matrix[0][1] = m[0][1];
			matrix[0][2] = m[0][2];
			matrix[0][3] = m[0][3];
			matrix[1][0] = m[1][0];
			matrix[1][1] = m[1][1];
			matrix[1][2] = m[1][2];
			matrix[1][3] = m[1][3];
			matrix[2][0] = m[2][0];
			matrix[2][1] = m[2][1];
			matrix[2][2] = m[2][2];
			matrix[2][3] = m[2][3];
		}
	};

	struct Plane
	{
		Vec3 normal;
		f32  distance;

		Plane() = default;
		Plane(const Vec3& p1, const Vec3& norm);
		Plane(f32 a, f32 b, f32 c, f32 d);

		f32 GetDistanceToPoint(const Vec3& point) const;

		void Normalize();
	};

	enum FrustumSide { FRUSTUM_SIDE_LEFT = 0, FRUSTUM_SIDE_RIGHT, FRUSTUM_SIDE_BOTTOM, FRUSTUM_SIDE_TOP, FRUSTUM_SIDE_NEAR, FRUSTUM_SIDE_FAR };

	struct Frustum
	{
		union
		{
			struct
			{
				Plane topFace;
				Plane bottomFace;

				Plane rightFace;
				Plane leftFace;

				Plane farFace;
				Plane nearFace;
			};
			Plane planes[6] = {};
		};

	};

	struct AABB
	{
		Vec3 min = {F32_MAX, F32_MAX, F32_MAX};
		Vec3 max = {F32_LOW, F32_LOW, F32_LOW};

		void Expand(const AABB& other);
		Vec3 GetCenter() const;
		f32 GetRadius() const;
		FixedArray<Vec3, 8> GetCorners() const;

		bool IsOnOrForwardPlane(const Plane& plane) const;
		bool IsOnFrustum(const Frustum& camFrustum) const;

		explicit operator bool() const noexcept
		{
			return min != F32_MAX && max != F32_LOW;
		}
	};

	struct Ray
	{
		Vec3 origin;
		Vec3 dir;

		bool TestRayOBBIntersection(const AABB& aabb, const Mat4& matrix, float& dist) const;
	};

	namespace Math
	{
		template <typename T>
		constexpr auto Min(T a, T b) -> T
		{
			return a < b ? a : b;
		}

		template <typename T>
		constexpr auto Max(T a, T b) -> T
		{
			return a > b ? a : b;
		}

		template <typename T>
		constexpr auto Clamp(T x, T min, T max) -> T
		{
			if (x < min)
			{
				return min;
			}
			if (x > max)
			{
				return max;
			}
			return x;
		}

		constexpr f32 SmoothStep(f32 edge0, f32 edge1, f32 x, f32 min = 0.0f, f32 max = 1.0f)
		{
			x = Clamp((x - edge0) / (edge1 - edge0), min, max);
			return x * x * (3.0f - 2.0f * x);
		}

		template <typename Type>
		constexpr Type Atan(Type a, Type b)
		{
			return std::atan2(a, b);
		}

		template <typename Type>
		constexpr Type Pow(Type v)
		{
			return v * v;
		}

		template <typename Type>
		Type Pow(Type x, Type y)
		{
			return std::pow(x, y);
		}

		template <typename Type>
		constexpr Type Sqrt(Type value)
		{
			return std::sqrt(value);
		}

		template <typename Type>
		constexpr Type Acos(Type vl)
		{
			return std::acos(vl);
		}

		template <typename T>
		constexpr auto Cos(T radians)
		{
			return static_cast<T>(cosf(static_cast<T>(radians)));
		}

		template <typename T>
		constexpr auto Sin(T radians)
		{
			return static_cast<T>(sinf(static_cast<T>(radians)));
		}

		template <typename T>
		constexpr auto Radians(T degrees)
		{
			return degrees * static_cast<T>(0.01745329251994329576923690768489);
		}

		template <typename T>
		constexpr auto Degrees(T radians)
		{
			return radians * static_cast<T>(57.295779513082320876798154814105);
		}

		template <typename T>
		constexpr T Abs(T x)
		{
			return x < 0 ? -x : x;
		}

		template <typename T>
		T Ceil(T x)
		{
			return std::ceil(x);
		}

		inline Float LinearToGamma(Float value)
		{
			return std::pow(value, 1.0 / 2.2);
		}

		inline bool ScreenToWorld(const Vec3& position, const Extent extent, const Mat4& projView, Vec2& out)
		{
			Vec4 clipPos = projView * Vec4(position, 1.0f);
			if (clipPos.w > 0.0f)
			{
				if (clipPos.w != 0.0f) {
					clipPos.x /= clipPos.w;
					clipPos.y /= clipPos.w;
					clipPos.z /= clipPos.w;
				}

				f32 screenX = (clipPos.x + 1.0f) * extent.width * 0.5f;
				f32 screenY = (1.0f - clipPos.y) * extent.height * 0.5f;

				if(screenX < 0 || screenX > extent.width)
				{
					return false;
				}

				if(screenY < 0 || screenY > extent.height)
				{
					return false;
				}

				out.x = screenX;
				out.y = screenY;

				return true;
			}

			return false;
		}

		inline AABB TransformAABB(const AABB& aabb, const Mat4& transform)
		{
			auto corners = aabb.GetCorners();

			FixedArray<Vec3, 8> transformedCorners;

			for (u8 i = 0; i < 8; ++i)
			{
				Vec4 transformedPoint = transform * Vec4(corners[i], 1.0f);
				transformedCorners[i] = Vec3(transformedPoint.x, transformedPoint.y, transformedPoint.z);
			}

			Vec3 newMin = transformedCorners[0];
			Vec3 newMax = transformedCorners[0];

			for (u8 i = 1; i < 8; ++i)
			{
				const auto& point = transformedCorners[i];
				newMin = Vec3::Min(newMin, point);
				newMax = Vec3::Max(newMax, point);
			}
			return AABB{newMin, newMax};
		}

		inline Vec2 GerNearFarFromAABB(const AABB& aabb, const Mat4& viewMatrix)
		{
			Vec3 corners[8] = {
				{aabb.min.x, aabb.min.y, aabb.min.z},
				{aabb.max.x, aabb.min.y, aabb.min.z},
				{aabb.min.x, aabb.max.y, aabb.min.z},
				{aabb.max.x, aabb.max.y, aabb.min.z},
				{aabb.min.x, aabb.min.y, aabb.max.z},
				{aabb.max.x, aabb.min.y, aabb.max.z},
				{aabb.min.x, aabb.max.y, aabb.max.z},
				{aabb.max.x, aabb.max.y, aabb.max.z}
			};

			float minZ = F32_MAX;
			float maxZ = -F32_MAX;

			for (int i = 0; i < 8; i++) {
				Vec3 viewPos = viewMatrix * Vec4(corners[i], 1.0);
				float depth = -viewPos.z;
				minZ = Min(minZ, depth);
				maxZ = Max(maxZ, depth);
			}

			return {Max(0.01f, minZ * 0.9f), maxZ * 1.1f};
		}

		inline Frustum CreateFrustumFromCamera(const Vec3& pos, const Vec3& front, const Vec3& right, const Vec3& up, f32 aspect, f32 fovY, f32 zNear, f32 zFar)
		{
			Frustum    frustum;
			const f32  halfVSide = zFar * tanf(fovY * .5f);
			const f32  halfHSide = halfVSide * aspect;
			const Vec3 frontMultFar = zFar * front;

			frustum.nearFace = {pos + zNear * front, front};
			frustum.farFace = {pos + frontMultFar, -front};
			frustum.rightFace = {pos, Vec3::Cross(frontMultFar - right * halfHSide, up)};
			frustum.leftFace = {pos, Vec3::Cross(up, frontMultFar + right * halfHSide)};
			frustum.topFace = {pos, Vec3::Cross(right, frontMultFar - up * halfVSide)};
			frustum.bottomFace = {pos, Vec3::Cross(frontMultFar + up * halfVSide, right)};
			return frustum;
		}

		inline Frustum CreateFrustumFromCamera(const Mat4& viewProjection)
		{
			Frustum frustum;

			frustum.planes[FRUSTUM_SIDE_LEFT].normal = Vec3(
				viewProjection[0][3] + viewProjection[0][0],
				viewProjection[1][3] + viewProjection[1][0],
				viewProjection[2][3] + viewProjection[2][0]
			);

			frustum.planes[FRUSTUM_SIDE_LEFT].distance = viewProjection[3][3] + viewProjection[3][0];

			frustum.planes[FRUSTUM_SIDE_RIGHT].normal = Vec3(
				viewProjection[0][3] - viewProjection[0][0],
				viewProjection[1][3] - viewProjection[1][0],
				viewProjection[2][3] - viewProjection[2][0]
			);
			frustum.planes[FRUSTUM_SIDE_RIGHT].distance = viewProjection[3][3] - viewProjection[3][0];

			frustum.planes[FRUSTUM_SIDE_BOTTOM].normal = Vec3(
				viewProjection[0][3] + viewProjection[0][1],
				viewProjection[1][3] + viewProjection[1][1],
				viewProjection[2][3] + viewProjection[2][1]
			);
			frustum.planes[FRUSTUM_SIDE_BOTTOM].distance = viewProjection[3][3] + viewProjection[3][1];

			frustum.planes[FRUSTUM_SIDE_TOP].normal = Vec3(
				viewProjection[0][3] - viewProjection[0][1],
				viewProjection[1][3] - viewProjection[1][1],
				viewProjection[2][3] - viewProjection[2][1]
			);
			frustum.planes[FRUSTUM_SIDE_TOP].distance = viewProjection[3][3] - viewProjection[3][1];

			frustum.planes[FRUSTUM_SIDE_NEAR].normal = Vec3(
				viewProjection[0][2],
				viewProjection[1][2],
				viewProjection[2][2]
			);
			frustum.planes[FRUSTUM_SIDE_NEAR].distance = viewProjection[3][2];

			frustum.planes[FRUSTUM_SIDE_FAR].normal = Vec3(
				viewProjection[0][3] - viewProjection[0][2],
				viewProjection[1][3] - viewProjection[1][2],
				viewProjection[2][3] - viewProjection[2][2]
			);
			frustum.planes[FRUSTUM_SIDE_FAR].distance = viewProjection[3][3] - viewProjection[3][2];

			for (auto& plane : frustum.planes)
			{
				plane.Normalize();
			}

			return frustum;
		}
	}
	//end Math


	constexpr bool Extent::operator<(const u32& b) const
	{
		return this->width < b && this->height < b;
	}

	constexpr bool Extent::operator>(const u32& b) const
	{
		return this->width > b && this->height > b;
	}

	constexpr Extent Extent::operator*(const Extent& b) const
	{
		return {this->width * b.width, this->height * b.height};
	}

	constexpr Extent Extent::operator*(const u32& b) const
	{
		return {this->width * b, this->height * b};
	}

	constexpr Extent Extent::operator*(const Vec2& b) const
	{
		return {static_cast<u32>(static_cast<f32>(width) * b.width), static_cast<u32>(static_cast<f32>(height) * b.height)};
	}

	constexpr bool Extent3D::operator<(const u32& b) const
	{
		return this->width < b && this->height < b && this->depth < b;
	}

	constexpr bool Extent3D::operator>(const u32& b) const
	{
		return this->width > b && this->height > b && this->depth > b;
	}

	constexpr Extent3D Extent3D::operator*(const Extent3D& b) const
	{
		return Extent3D{this->width * b.width, this->height * b.height, this->depth * b.depth};
	}

	constexpr Extent3D Extent3D::operator*(const u32& b) const
	{
		return {this->width * b, this->height * b, this->depth * b};
	}

	constexpr Extent3D Extent3D::operator*(const Vec3& b) const
	{
		return {u32((f32)this->width * b.x), u32((f32)this->height * b.y), u32((f32)this->depth * b.z)};
	}

	constexpr Vec2::Vec2() : x(0), y(0) {}
	constexpr Vec2::Vec2(Float value) : x(value), y(value) {}
	constexpr Vec2::Vec2(Float _x, Float _y) : x(_x), y(_y) {}
	constexpr Vec2::Vec2(const Vec3& v) : x(v.x), y(v.y) {}

	constexpr Vec2 Vec2::operator/(const Vec2& b) const
	{
		return Vec2{this->x / b.x, this->y / b.y};
	}

	constexpr Vec2 Vec2::operator/(const Float& b) const
	{
		return {this->x / b, this->y / b};
	}

	constexpr Vec2 Vec2::operator*(const Float& b) const
	{
		return {this->x * b, this->y * b};
	}

	constexpr Vec2 Vec2::operator*(const Vec2& b) const
	{
		return {this->x * b.x, this->y * b.y};
	}

	constexpr Vec2 Vec2::operator+(const Vec2& b) const
	{
		return {this->x + b.x, this->y + b.y};
	}

	constexpr Vec2 Vec2::operator-(const Vec2& b) const
	{
		return {this->x - b.x, this->y - b.y};
	}

	constexpr Vec2 Vec2::operator>>(const int vl) const
	{
		//return TVec2{this->x >> vl, this->y >> vl};
		//SK_ASSERT(false, "Not Implemnted");
		return {};
	}

	constexpr Vec2 Vec2::operator<<(const int vl) const
	{
		//return TVec2{this->x << vl, this->y << vl};
		//SK_ASSERT(false, "Not Implemnted");
		return {};
	}

	constexpr bool Vec2::operator==(const Float& b) const
	{
		return this->x == b && this->y == b;
	}

	constexpr bool Vec2::operator!=(const Float& b) const
	{
		return !(*this == b);
	}

	constexpr const Float& Vec2::operator[](usize axis) const
	{
		return c[axis];
	}

	constexpr Float& Vec2::operator[](usize axis)
	{
		return c[axis];
	}

	constexpr Vec2& Vec2::operator*=(const Vec2& rhs)
	{
		this->x *= rhs.x;
		this->y *= rhs.y;
		return *this;
	}

	constexpr Vec2& Vec2::operator/=(const Vec2& rhs)
	{
		this->x /= rhs.x;
		this->y /= rhs.y;
		return *this;
	}

	constexpr Vec2& Vec2::operator-=(const Vec2& rhs)
	{
		this->x -= rhs.x;
		this->y -= rhs.y;
		return *this;
	}

	constexpr Vec2& Vec2::operator+=(const Vec2& rhs)
	{
		this->x += rhs.x;
		this->y += rhs.y;
		return *this;
	}

	constexpr bool Vec2::operator>(const Float& b) const
	{
		return x > b && y > b;
	}

	template <typename T>
	constexpr Vec2& Vec2::operator*=(const T& rhs)
	{
		this->x *= rhs;
		this->y *= rhs;
		return *this;
	}

	constexpr bool operator==(const Vec2& a, const Vec2& b)
	{
		return a.x == b.x && a.y == b.y;
	}

	constexpr bool operator!=(const Vec2& a, const Vec2& b)
	{
		return !(a == b);
	}

	constexpr Vec2 operator*(const Vec2& a, const Vec2& b)
	{
		return {a.x * b.x, a.y * b.y};
	}

	// Vec2 static methods
	constexpr Vec2 Vec2::Abs(const Vec2& value)
	{
		return Vec2{Math::Abs(value.x), Math::Abs(value.y)};
	}

	constexpr Float Vec2::Dot(const Vec2& a, const Vec2& b)
	{
		return a.x * b.x + a.y * b.y;
	}

	inline Float Vec2::Length(const Vec2& a)
	{
		return static_cast<Float>(sqrt(Dot(a, a)));
	}

	inline Float Vec2::Distance(const Vec2& a, const Vec2& b)
	{
		return Length(a - b);
	}

	template <typename Type>
	constexpr Vec2 Vec2::Lerp(const Vec2& a, const Vec2& b, Type alpha)
	{
		return Vec2{
			Math::Clamp(a.x * (1 - alpha) + b.x * alpha, a.x, b.x),
			Math::Clamp(a.y * (1 - alpha) + b.y * alpha, a.y, b.y)
		};
	}

	constexpr Vec2 Vec2::Make(const Float* value)
	{
		return {value[0], value[1]};
	}

	constexpr Vec2 Vec2::Make(const Float& x, const Float& y)
	{
		return {x, y};
	}

	constexpr Vec3::Vec3()
	{

	}
	constexpr Vec3::Vec3(Float value)
	{
		this->x = value;
		this->y = value;
		this->z = value;
	}

	constexpr Vec3::Vec3(Float* value)
	{
		this->x = value[0];
		this->y = value[1];
		this->z = value[2];
	}

	constexpr Vec3::Vec3(Float _x, Float _y, Float _z)
	{
		this->x = _x;
		this->y = _y;
		this->z = _z;
	}

	constexpr Vec3::Vec3(const Vec4& v)
	{
		this->x = v.x;
		this->y = v.y;
		this->z = v.z;
	}

	constexpr const Float& Vec3::operator[](int axis) const
	{
		return coord[axis];
	}

	constexpr Float& Vec3::operator[](int axis)
	{
		return coord[axis];
	}

	constexpr Vec3 Vec3::operator/(const Vec3& b) const
	{
		return {this->x / b.x, this->y / b.y, this->z / b.z};
	}

	constexpr Vec3 Vec3::operator*(const Vec3& b) const
	{
		return {this->x * b.x, this->y * b.y, this->z * b.z};
	}

	constexpr Vec3 Vec3::operator-(const Vec3& b) const
	{
		return {this->x - b.x, this->y - b.y, this->z - b.z};
	}

	constexpr Vec3 Vec3::operator/(const Float& b) const
	{
		return {this->x / b, this->y / b, this->z / b};
	}

	constexpr Vec3 Vec3::operator+(const Float& b) const
	{
		return {this->x + b, this->y + b, this->z + b};
	}

	constexpr Vec3 Vec3::operator-(const Float& b) const
	{
		return {this->x - b, this->y - b, this->z - b};
	}

	constexpr Vec3 Vec3::operator*(const Float& b) const
	{
		return {this->x * b, this->y * b, this->z * b};
	}

	constexpr Vec3 Vec3::operator>>(const int vl) const
	{
		//return {this->x >> vl, this->y >> vl, this->z >> vl};
		return {};
	}

	constexpr Vec3 Vec3::operator<<(const int vl) const
	{
		//return {this->x << vl, this->y << vl, this->z << vl};
		return {};
	}

	constexpr Vec3& Vec3::operator*=(const Vec3& rhs)
	{
		this->x *= rhs.x;
		this->y *= rhs.y;
		this->z *= rhs.z;
		return *this;
	}

	constexpr Vec3& Vec3::operator+=(const Vec3& rhs)
	{
		this->x += rhs.x;
		this->y += rhs.y;
		this->z += rhs.z;
		return *this;
	}

	constexpr Vec3& Vec3::operator-=(const Vec3& rhs)
	{
		this->x -= rhs.x;
		this->y -= rhs.y;
		this->z -= rhs.z;
		return *this;
	}

	constexpr const Float* Vec3::operator[](usize axis) const
	{
		return &coord[axis];
	}

	constexpr Float* Vec3::operator[](usize axis)
	{
		return &coord[axis];
	}

	constexpr Vec3 Vec3::operator-() const
	{
		Vec3 ret{};
		ret.x = this->x * -1;
		ret.y = this->y * -1;
		ret.z = this->z * -1;
		return ret;
	}

	constexpr bool Vec3::operator==(const Float& b) const
	{
		return Compare<Float>::IsEqual(x, b) && Compare<Float>::IsEqual(y, b) && Compare<Float>::IsEqual(z, b);
	}

	constexpr bool Vec3::operator==(const Vec3& b) const
	{
		return Compare<Float>::IsEqual(x, b.x) && Compare<Float>::IsEqual(y, b.y) && Compare<Float>::IsEqual(z, b.z);
	}

	constexpr bool Vec3::operator>(const Float& b) const
	{
		return x > b && y > b && z > b;
	}

	template <typename U>
	constexpr Vec3 Vec3::operator/=(U u)
	{
		this->x /= static_cast<Float>(u);
		this->y /= static_cast<Float>(u);
		this->z /= static_cast<Float>(u);
		return *this;
	}

	constexpr Vec3 Vec3::Zero()
	{
		return {0, 0, 0};
	}

	constexpr Vec3 Vec3::AxisX()
	{
		return {static_cast<Float>(1), static_cast<Float>(0), static_cast<Float>(0)};
	}

	constexpr Vec3 Vec3::AxisY()
	{
		return {static_cast<Float>(0), static_cast<Float>(1), static_cast<Float>(0)};
	}

	constexpr Vec3 Vec3::AxisZ()
	{
		return {static_cast<Float>(0), static_cast<Float>(0), static_cast<Float>(1)};
	}

	constexpr Vec3 operator*(const Float& a, const Vec3& b)
	{
		return {a * b.x, a * b.y, a * b.z};
	}

	constexpr Vec3 operator+(const Vec3& a, const Vec3& b)
	{
		return {a.x + b.x, a.y + b.y, a.z + b.z};
	}

	// Vec3 static methods
	constexpr Vec3 Vec3::Abs(const Vec3& value)
	{
		return Vec3{Math::Abs(value.x), Math::Abs(value.y), Math::Abs(value.z)};
	}

	constexpr Float Vec3::Dot(const Vec3& a, const Vec3& b)
	{
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}

	inline Float Vec3::Length(const Vec3& a)
	{
		return static_cast<Float>(sqrt(Dot(a, a)));
	}

	inline Float Vec3::Distance(const Vec3& a, const Vec3& b)
	{
		return Length(a - b);
	}

	constexpr Vec3 Vec3::Cross(const Vec3& a, const Vec3& b)
	{
		return Vec3{
			a[1] * b[2] - a[2] * b[1],
			a[2] * b[0] - a[0] * b[2],
			a[0] * b[1] - a[1] * b[0]
		};
	}

	constexpr Vec3 Vec3::Scale(const Vec3& a, Float s)
	{
		return Vec3{a.x * s, a.y * s, a.z * s};
	}

	inline Vec3 Vec3::Normalize(const Vec3& a)
	{
		Float k = 1.f / Length(a);
		return Scale(a, k);
	}

	inline Vec3 Vec3::Min(const Vec3& lhs, const Vec3& rhs)
	{
		return Vec3{Math::Min(lhs.x, rhs.x), Math::Min(lhs.y, rhs.y), Math::Min(lhs.z, rhs.z)};
	}

	inline Vec3 Vec3::Max(const Vec3& lhs, const Vec3& rhs)
	{
		return Vec3{Math::Max(lhs.x, rhs.x), Math::Max(lhs.y, rhs.y), Math::Max(lhs.z, rhs.z)};
	}

	template <typename Type>
	constexpr Vec3 Vec3::Lerp(const Vec3& a, const Vec3& b, Type alpha)
	{
		return Vec3{
			Math::Clamp(a.x * (1 - alpha) + b.x * alpha, a.x, b.x),
			Math::Clamp(a.y * (1 - alpha) + b.y * alpha, a.y, b.y),
			Math::Clamp(a.z * (1 - alpha) + b.z * alpha, a.z, b.z)
		};
	}

	constexpr Vec3 Vec3::Mix(const Vec3& a, const Vec3& b, Float t)
	{
		return Vec3{
			a.x + (b.x - a.x) * t,
			a.y + (b.y - a.y) * t,
			a.z + (b.z - a.z) * t
		};
	}

	constexpr Vec3 Vec3::Mix(const Vec3& a, const Vec3& b, const Vec3& t)
	{
		return Vec3{
			a.x + (b.x - a.x) * t.x,
			a.y + (b.y - a.y) * t.y,
			a.z + (b.z - a.z) * t.z
		};
	}

	constexpr Vec3 Vec3::Radians(const Vec3& other)
	{
		return Vec3{Math::Radians(other.x), Math::Radians(other.y), Math::Radians(other.z)};
	}

	inline Vec3 Vec3::Degrees(Vec3 radians)
	{
		return Vec3{Math::Degrees(radians.x), Math::Degrees(radians.y), Math::Degrees(radians.z)};
	}

	constexpr Vec3 Vec3::Sin(const Vec3& other)
	{
		return Vec3{Math::Sin(other.x), Math::Sin(other.y), Math::Sin(other.z)};
	}

	constexpr Vec3 Vec3::Cos(const Vec3& other)
	{
		return Vec3{Math::Cos(other.x), Math::Cos(other.y), Math::Cos(other.z)};
	}

	constexpr Vec3 Vec3::Make(const Float* value)
	{
		return Vec3{value[0], value[1], value[2]};
	}

	constexpr Vec3 Vec3::Make(const Vec4& value)
	{
		return Vec3{value.x, value.y, value.z};
	}

	constexpr Vec3 Vec3::Make(const f64* value)
	{
		return Vec3{
			static_cast<f32>(value[0]),
			static_cast<f32>(value[1]),
			static_cast<f32>(value[2])
		};
	}

	constexpr Vec3 Vec3::Round(const Vec3& v)
	{
		return Vec3(
			std::round(v.x),
			std::round(v.y),
			std::round(v.z)
		);
	}

	constexpr Vec4::Vec4(Float value)
	{
		c[0] = value;
		c[1] = value;
		c[2] = value;
		c[3] = value;
	}

	constexpr Vec4::Vec4(Float* value)
	{
		c[0] = value[0];
		c[1] = value[1];
		c[2] = value[2];
		c[3] = value[3];
	}

	constexpr Vec4::Vec4(Float _x, Float _y, Float _z, Float _w)
	{
		c[0] = _x;
		c[1] = _y;
		c[2] = _z;
		c[3] = _w;
	}

	constexpr Vec4::Vec4(const Vec3& vec, Float _w)
	{
		c[0] = vec.x;
		c[1] = vec.y;
		c[2] = vec.z;
		c[3] = _w;
	}

	constexpr Vec4::Vec4(const Vec4D& vec) : x(vec.x), y(vec.y), z(vec.z), w(vec.w) {}

	constexpr const Float& Vec4::operator[](u32 axis) const
	{
		return c[axis];
	}

	constexpr Float& Vec4::operator[](u32 axis)
	{
		return c[axis];
	}

	constexpr Vec4 Vec4::operator/(const Vec4& b) const
	{
		return {this->x / b.x, this->y / b.y, this->z / b.z, this->w / b.w};
	}

	constexpr Vec4 Vec4::operator+(const Vec4& b) const
	{
		return {this->x + b.x, this->y + b.y, this->z + b.z, this->w + b.w};
	}

	constexpr Vec4 Vec4::operator*(const Vec4& b) const
	{
		return {this->x * b.x, this->y * b.y, this->z * b.z, this->w * b.w};
	}

	constexpr Vec4 Vec4::operator-(const Vec4& b) const
	{
		return {this->x - b.x, this->y - b.y, this->z - b.z, this->w - b.w};
	}

	constexpr Vec4 Vec4::operator/(const Float& b) const
	{
		return {this->x / b, this->y / b, this->z / b, this->w / b};
	}

	constexpr Vec4 Vec4::operator*(const Float& b) const
	{
		return {this->x * b, this->y * b, this->z * b, this->w * b};
	}

	constexpr Vec4& Vec4::operator/=(const Float& b)
	{
		this->x /= b;
		this->y /= b;
		this->z /= b;
		this->w /= b;
		return *this;
	}

	constexpr Vec4 Vec4::operator+=(const Vec4& b) const
	{
		return {this->x + b.x, this->y + b.y, this->z + b.z, this->w + b.w};
	}

	constexpr Vec4 Vec4::operator>>(i32 vl) const
	{
		//return {this->x >> vl, this->y >> vl, this->z >> vl, this->w >> vl};
		//SK_ASSERT(false, "TODO");
		return {};
	}

	constexpr Vec4 Vec4::operator<<(i32 vl) const
	{
		//return {this->x << vl, this->y << vl, this->z << vl, this->w << vl};
		//SK_ASSERT(false, "TODO");
		return {};
	}

	constexpr bool Vec4::operator==(const Float& b) const
	{
		return Compare<Float>::IsEqual(x, b) && Compare<Float>::IsEqual(y, b) && Compare<Float>::IsEqual(z, b) && Compare<Float>::IsEqual(w, b);
	}

	constexpr bool Vec4::operator==(const Vec4& b) const
	{
		return Compare<Float>::IsEqual(x, b.x) && Compare<Float>::IsEqual(y, b.y) && Compare<Float>::IsEqual(z, b.z) && Compare<Float>::IsEqual(w, b.w);
	}

	constexpr bool Vec4::operator!=(const Float& b) const
	{
		return !(*this == b);
	}

	constexpr bool Vec4::operator!=(const Vec4& b) const
	{
		return !(*this == b);
	}

	// Vec4 static methods
	constexpr Float Vec4::Dot(const Vec4& a, const Vec4& b)
	{
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}

	inline Float Vec4::Length(const Vec4& a)
	{
		return Math::Sqrt(Dot(a, a));
	}

	inline Float Vec4::Distance(const Vec4& a, const Vec4& b)
	{
		return Length(a - b);
	}

	constexpr Vec4 Vec4::Scale(const Vec4& a, Float s)
	{
		return Vec4{a.x * s, a.y * s, a.z * s, a.w};
	}

	inline Vec4 Vec4::Normalize(const Vec4& a)
	{
		f32 k = 1.f / Length(a);
		return Scale(a, k);
	}

	constexpr Vec4 Vec4::Make(const Vec3& value, Float w)
	{
		return Vec4{value[0], value[1], value[2], w};
	}

	constexpr Vec4 Vec4::Make(const Float* value)
	{
		return Vec4{value[0], value[1], value[2], value[4]};
	}

	constexpr Vec4 Vec4::Make(const Vec2& value1, const Vec2& value2)
	{
		return Vec4{value1[0], value1[1], value2[0], value2[1]};
	}

	template <typename Type>
	constexpr Vec4 Vec4::Lerp(const Vec4& a, const Vec4& b, Type alpha)
	{
		return Vec4{
			Math::Clamp(a.x * (1 - alpha) + b.x * alpha, a.x, b.x),
			Math::Clamp(a.y * (1 - alpha) + b.y * alpha, a.y, b.y),
			Math::Clamp(a.z * (1 - alpha) + b.z * alpha, a.z, b.z),
			Math::Clamp(a.w * (1 - alpha) + b.w * alpha, a.w, b.w)
		};
	}

	constexpr Vec4 Vec4::Round(const Vec4& v)
	{
		return Vec4(
			std::round(v.x),
			std::round(v.y),
			std::round(v.z),
			std::round(v.w)
		);
	}

	constexpr Quat::Quat() : x(0), y(0), z(0), w(0) {}

	constexpr Quat::Quat(Float x, Float y, Float z, Float w) : x(x), y(y), z(z), w(w) {}

	constexpr Quat::Quat(const Vec3& eulerAngle)
	{
		auto c = Vec3::Cos(eulerAngle * 0.5f);
		auto s = Vec3::Sin(eulerAngle * 0.5f);
		this->x = s.x * c.y * c.z - c.x * s.y * s.z;
		this->y = c.x * s.y * c.z + s.x * c.y * s.z;
		this->z = c.x * c.y * s.z - s.x * s.y * c.z;
		this->w = c.x * c.y * c.z + s.x * s.y * s.z;
	}

	constexpr Quat::Quat(Float s, const Vec3& v) : x(v.x), y(v.y), z(v.z), w(s) {}

	constexpr const Float& Quat::operator[](int axis) const
	{
		return c[axis];
	}

	constexpr Float& Quat::operator[](int axis)
	{
		return c[axis];
	}

	constexpr Quat& Quat::operator=(const Mat4& m)
	{
		//SK_ASSERT(false, "not implemeneted");
		return *this;
	}

	constexpr bool Quat::operator==(const Quat& other) const
	{
		return Compare<Float>::IsEqual(x, other.x) &&
			Compare<Float>::IsEqual(y, other.y) &&
			Compare<Float>::IsEqual(z, other.z) &&
			Compare<Float>::IsEqual(w, other.w);
	}

	constexpr Float Quat::Normal() const
	{
		const auto n = x * x + y * y + z * z + w * w;
		if (n == 1)
		{
			return 1;
		}
		return std::sqrt(n);
	}

	constexpr Quat& Quat::Normalize()
	{
		Float normal = Normal();
		x = x / normal;
		y = y / normal;
		z = z / normal;
		w = w / normal;
		return *this;
	}

	constexpr Float Quat::Dot(const Quat& other) const
	{
		return (x * other.x) + (y * other.y) + (z * other.z) + (w * other.w);
	}

	constexpr Quat operator+(const Quat& q, const Quat& p)
	{
		return {q.w + p.w, q.x + p.x, q.y + p.y, q.z + p.z};
	}

	constexpr Quat operator*(const Quat& p, const Quat& q)
	{
		Quat dest{};
		dest.w = p.w * q.w - p.x * q.x - p.y * q.y - p.z * q.z;
		dest.x = p.w * q.x + p.x * q.w + p.y * q.z - p.z * q.y;
		dest.y = p.w * q.y + p.y * q.w + p.z * q.x - p.x * q.z;
		dest.z = p.w * q.z + p.z * q.w + p.x * q.y - p.y * q.x;
		return dest;
	}

	constexpr Vec3 operator*(const Quat& q, const Vec3& v)
	{
		Vec3 qv = {q.x, q.y, q.z};
		Vec3 uv = Vec3::Cross(qv, v);
		Vec3 uuv = Vec3::Cross(qv, uv);
		return v + (uv * q.w + uuv) * 2.0f;
	}

	// Quat static methods
	constexpr Quat Quat::Normalized(const Quat& value)
	{
		auto retValue = value;
		return retValue.Normalize();
	}

	constexpr Float Quat::DotProduct(const Quat& value, const Quat& other)
	{
		auto retValue = value;
		return retValue.Dot(other);
	}

	//got from https://github.com/travisvroman/kohi/blob/main/engine/src/math/kmath.h
	constexpr Quat Quat::Slerp(const Quat& q0, const Quat& q1, f32 percentage)
	{
		Quat outQuaternion{};

		Quat v0 = Normalized(q0);
		Quat v1 = Normalized(q1);

		f32 dot = DotProduct(v0, v1);

		if (dot < 0.0f)
		{
			v1.x = -v1.x;
			v1.y = -v1.y;
			v1.z = -v1.z;
			v1.w = -v1.w;
			dot = -dot;
		}

		const f32 DOT_THRESHOLD = 0.9995f;
		if (dot > DOT_THRESHOLD)
		{
			outQuaternion = Quat{
				v0.x + ((v1.x - v0.x) * percentage),
				v0.y + ((v1.y - v0.y) * percentage),
				v0.z + ((v1.z - v0.z) * percentage),
				v0.w + ((v1.w - v0.w) * percentage)
			};

			return outQuaternion.Normalize();
		}

		f32 theta_0 = Math::Acos(dot);
		f32 theta = theta_0 * percentage;
		f32 sin_theta = Math::Sin(theta);
		f32 sin_theta_0 = Math::Sin(theta_0);

		f32 s0 = Math::Cos(theta) - dot * sin_theta / sin_theta_0;
		f32 s1 = sin_theta / sin_theta_0;

		return {
			(v0.x * s0) + (v1.x * s1),
			(v0.y * s0) + (v1.y * s1),
			(v0.z * s0) + (v1.z * s1),
			(v0.w * s0) + (v1.w * s1)
		};
	}

	inline Float Quat::Roll(const Quat& q)
	{
		return Math::Atan(2.f * (q.x * q.y + q.w * q.z), q.w * q.w + q.x * q.x - q.y * q.y - q.z * q.z);
	}

	inline Float Quat::Yaw(const Quat& q)
	{
		return asin(Math::Clamp(-2.f * (q.x * q.z - q.w * q.y), -1.f, 1.f));
	}

	inline Float Quat::Pitch(const Quat& q)
	{
		Float const y = 2.f * (q.y * q.z + q.w * q.x);
		Float const x = q.w * q.w - q.x * q.x - q.y * q.y + q.z * q.z;
		return Math::Atan(y, x);
	}

	inline Quat Quat::AngleAxis(const Float& angle, const Vec3& v)
	{
		const Float a(angle);
		Float const s = Math::Sin(a * 0.5f);
		return Quat(Math::Cos(a * 0.5f), v * s);
	}

	inline Vec3 Quat::EulerAngles(const Quat& quat)
	{
		return {Pitch(quat), Yaw(quat), Roll(quat)};
	}

	constexpr Mat4 Quat::ToMatrix4(const Quat& q)
	{
		Mat4  result{1.0f};
		Float qxx(q.x * q.x);
		Float qyy(q.y * q.y);
		Float qzz(q.z * q.z);
		Float qxz(q.x * q.z);
		Float qxy(q.x * q.y);
		Float qyz(q.y * q.z);
		Float qwx(q.w * q.x);
		Float qwy(q.w * q.y);
		Float qwz(q.w * q.z);

		result[0][0] = Float(1) - Float(2) * (qyy + qzz);
		result[0][1] = Float(2) * (qxy + qwz);
		result[0][2] = Float(2) * (qxz - qwy);
		result[0][3] = Float(0);

		result[1][0] = Float(2) * (qxy - qwz);
		result[1][1] = Float(1) - Float(2) * (qxx + qzz);
		result[1][2] = Float(2) * (qyz + qwx);
		result[1][3] = Float(0);

		result[2][0] = Float(2) * (qxz + qwy);
		result[2][1] = Float(2) * (qyz - qwx);
		result[2][2] = Float(1) - Float(2) * (qxx + qyy);
		result[2][3] = Float(0);

		result[3][0] = Float(0);
		result[3][1] = Float(0);
		result[3][2] = Float(0);
		result[3][3] = Float(1);

		return result;
	}

	template <typename Type>
	inline Quat Quat::Make(const f64* t)
	{
		return Quat{
			static_cast<Type>(t[0]),
			static_cast<Type>(t[1]),
			static_cast<Type>(t[2]),
			static_cast<Type>(t[3])
		};
	}

	// Extent3D static methods
	inline Extent3D Extent3D::Max(const Extent3D& lhs, const Extent3D& rhs)
	{
		return Extent3D{Math::Max(lhs.width, rhs.width), Math::Max(lhs.height, rhs.height), Math::Max(lhs.depth, rhs.depth)};
	}

	constexpr Mat4::Mat4() {}

	constexpr Mat4::Mat4(const Vec4& vec0, const Vec4& vec1, const Vec4& vec2, const Vec4& vec3)
	{
		Identity(1.f);
		m[0] = vec0;
		m[1] = vec1;
		m[2] = vec2;
		m[3] = vec3;
	}

	constexpr Mat4::Mat4(const Float value)
	{
		Identity(value);
	}

	constexpr Mat4::Mat4(const Mat34& mat34)
	{
		m[0][0] = mat34.matrix[0][0];
		m[0][1] = mat34.matrix[0][1];
		m[0][2] = mat34.matrix[0][2];
		m[0][3] = mat34.matrix[0][3];
		m[1][0] = mat34.matrix[1][0];
		m[1][1] = mat34.matrix[1][1];
		m[1][2] = mat34.matrix[1][2];
		m[1][3] = mat34.matrix[1][3];
		m[2][0] = mat34.matrix[2][0];
		m[2][1] = mat34.matrix[2][1];
		m[2][2] = mat34.matrix[2][2];
		m[2][3] = mat34.matrix[2][3];
		m[3][0] = 0.0f;
		m[3][1] = 0.0f;
		m[3][2] = 0.0f;
		m[3][3] = 1.0f;
	}

	constexpr Mat4& Mat4::Identity()
	{
		Identity(1.0);
		return *this;
	}

	constexpr Mat4& Mat4::Identity(Float value)
	{
		u32 i, j;
		for (i = 0; i < 4; ++i)
		{
			for (j = 0; j < 4; ++j)
			{
				m[i][j] = i == j ? value : 0.f;
			}
		}
		return *this;
	}

	constexpr const Vec4& Mat4::operator[](usize axis) const
	{
		return m[axis];
	}

	constexpr Vec4& Mat4::operator[](usize axis)
	{
		return m[axis];
	}

	constexpr bool operator==(const Mat4& a, const Mat4& b)
	{
		for (int i = 0; i < 16; ++i)
		{
			if (a.a[i] != b.a[i]) return false;
		}
		return true;
	}

	constexpr bool operator!=(const Mat4& a, const Mat4& b)
	{
		return !(a == b);
	}

	constexpr Mat4 operator*(const Mat4& a, const Mat4& b)
	{
		Mat4 mat;
		int  k, r, c;
		for (c = 0; c < 4; ++c)
			for (r = 0; r < 4; ++r)
			{
				mat.m[c][r] = 0.f;
				for (k = 0; k < 4; ++k)
				{
					mat.m[c][r] += a.m[k][r] * b.m[c][k];
				}
			}
		return mat;
	}

	constexpr Mat4 operator*(const Mat4& m, const f32& a)
	{
		Mat4 mat{};
		for (int c = 0; c < 4; ++c)
		{
			for (int r = 0; r < 4; ++r)
			{
				mat.m[c][r] = m.m[c][r] * a;
			}
		}
		return mat;
	}

	constexpr Vec4 operator*(const Mat4& m, const Vec4& v)
	{
		Vec4 mov0{v[0]};
		Vec4 mov1{v[1]};
		Vec4 mul0 = m[0] * mov0;
		Vec4 mul1 = m[1] * mov1;
		Vec4 add0 = mul0 + mul1;
		Vec4 mov2{v[2]};
		Vec4 mov3{v[3]};
		Vec4 mul2 = m[2] * mov2;
		Vec4 mul3 = m[3] * mov3;
		Vec4 add1 = mul2 + mul3;
		Vec4 add2 = add0 + add1;
		return add2;
	}

	constexpr Mat4 MakeMat4(const f32* values)
	{
		Mat4 mat{};
		for (int i = 0; i < 16; ++i)
		{
			mat.a[i] = values[i];
		}
		return mat;
	}

	constexpr static inline Mat4 IdentityMatrix = Mat4().Identity();

	// Mat4 static methods
	inline Mat4 Mat4::Scale(const Vec3& scale)
	{
		Mat4 outMatrix(1.0);
		outMatrix.a[0] = scale.x;
		outMatrix.a[5] = scale.y;
		outMatrix.a[10] = scale.z;
		return outMatrix;
	}

	inline Mat4 Mat4::Scale(const Mat4& m, const Vec3& scale)
	{
		auto ret = m;
		ret[0][0] *= scale.x;
		ret[1][1] *= scale.y;
		ret[2][2] *= scale.z;
		ret[3][3] = 1;
		return ret;
	}

	inline Mat4 Mat4::RotateX(f32 angleRadians)
	{
		Mat4 outMatrix(1.0);
		f32  c = cos(angleRadians);
		f32  s = sin(angleRadians);
		outMatrix.a[5] = c;
		outMatrix.a[6] = -s;
		outMatrix.a[9] = s;
		outMatrix.a[10] = c;
		return outMatrix;
	}

	inline Mat4 Mat4::RotateY(f32 angleRadians)
	{
		Mat4 outMatrix(1.0);
		f32  c = cos(angleRadians);
		f32  s = sin(angleRadians);
		outMatrix.a[0] = c;
		outMatrix.a[2] = -s;
		outMatrix.a[8] = s;
		outMatrix.a[10] = c;
		return outMatrix;
	}

	inline Mat4 Mat4::RotateZ(f32 angleRadians)
	{
		Mat4 outMatrix(1.0);
		f32  c = cos(angleRadians);
		f32  s = sin(angleRadians);
		outMatrix.a[0] = c;
		outMatrix.a[1] = -s;
		outMatrix.a[4] = s;
		outMatrix.a[5] = c;
		return outMatrix;
	}

	inline Mat4 Mat4::Rotate(const Mat4& m, f32 rx, f32 ry, f32 rz)
	{
		Mat4 r = m;
		auto cosX = cos(rx);
		auto sin_x = sin(rx);
		auto cosY = cos(ry);
		auto sin_y = sin(ry);
		auto cosZ = cos(rz);
		auto sin_z = sin(rz);
		r[0][0] = cosY * cosZ;
		r[0][1] = cosY * sin_z;
		r[0][2] = -sin_y;
		r[1][0] = sin_x * sin_y * cosZ - cosX * sin_z;
		r[1][1] = sin_x * sin_y * sin_z + cosX * cosZ;
		r[1][2] = sin_x * cosY;
		r[2][0] = cosX * sin_y * cosZ + sin_x * sin_z;
		r[2][1] = cosX * sin_y * sin_z - sin_x * cosZ;
		r[2][2] = cosX * cosY;
		return r;
	}

	inline Mat4 Mat4::PerspectiveRH_ZO(f32 fovRadians, f32 aspectRatio, f32 zNear, f32 zFar)
	{
		Mat4       matrix{0};
		const auto halfTanFov = tanf(fovRadians * 0.5f);
		matrix[0][0] = 1.0f / (aspectRatio * halfTanFov);
		matrix[1][1] = 1.0f / halfTanFov;
		matrix[2][2] = zFar / (zNear - zFar);
		matrix[2][3] = -1.f;
		matrix[3][2] = -(zFar * zNear) / (zFar - zNear);
		return matrix;
	}

	inline Mat4 Mat4::PerspectiveRH_NO(f32 fovRadians, f32 aspectRatio, f32 zNear, f32 zFar)
	{
		Mat4       matrix{0};
		const auto halfTanFov = tanf(fovRadians * 0.5f);
		matrix[0][0] = 1.0f / (aspectRatio * halfTanFov);
		matrix[1][1] = 1.0f / halfTanFov;
		matrix[2][2] = -((zFar + zNear) / (zFar - zNear));
		matrix[2][3] = -1.f;
		matrix[3][2] = -((2.f * zFar * zNear) / (zFar - zNear));
		return matrix;
	}

	inline Mat4 Mat4::Perspective(f32 fovRadians, f32 aspectRatio, f32 zNear, f32 zFar)
	{
		return PerspectiveRH_ZO(fovRadians, aspectRatio, zNear, zFar);
	}

	inline Mat4 Mat4::LookAt(const Vec3& eye, const Vec3& center, const Vec3& up)
	{
		Mat4 mat(1.0);

		auto z = eye - center;
		z = Vec3::Normalize(z);
		auto y = up;
		auto x = Vec3::Cross(y, z);
		y = Vec3::Cross(z, x);
		x = Vec3::Normalize(x);
		y = Vec3::Normalize(y);

		mat[0][0] = x.x;
		mat[1][0] = x.y;
		mat[2][0] = x.z;
		mat[3][0] = -Vec3::Dot(x, eye);
		mat[0][1] = y.x;
		mat[1][1] = y.y;
		mat[2][1] = y.z;
		mat[3][1] = -Vec3::Dot(y, eye);
		mat[0][2] = -z.x;
		mat[1][2] = -z.y;
		mat[2][2] = -z.z;
		mat[3][2] = Vec3::Dot(z, eye);
		mat[0][3] = 0;
		mat[1][3] = 0;
		mat[2][3] = 0;
		mat[3][3] = 1.0f;
		return mat;
	}

	inline Mat4 Mat4::Translate(const Mat4& m, f32 x, f32 y, f32 z)
	{
		Mat4 matrix = m;
		matrix[3][0] = x;
		matrix[3][1] = y;
		matrix[3][2] = z;
		return matrix;
	}

	inline Mat4 Mat4::Translate(const Mat4& m, const Vec3& v)
	{
		return Translate(m, v.x, v.y, v.z);
	}

	inline Mat4 Mat4::Translate(f32 x, f32 y, f32 z)
	{
		Mat4 matrix(1.0);
		matrix[3][0] = x;
		matrix[3][1] = y;
		matrix[3][2] = z;
		return matrix;
	}

	inline Mat4 Mat4::Translate(const Vec3& v)
	{
		return Translate(v.x, v.y, v.z);
	}

	//credits: from https://github.com/travisvroman/kohi/blob/main/engine/src/math/kmath.h
	inline Mat4 Mat4::Inverse(const Mat4& mat)
	{
		const f32* m = mat.a;

		f32 t0 = m[10] * m[15];
		f32 t1 = m[14] * m[11];
		f32 t2 = m[6] * m[15];
		f32 t3 = m[14] * m[7];
		f32 t4 = m[6] * m[11];
		f32 t5 = m[10] * m[7];
		f32 t6 = m[2] * m[15];
		f32 t7 = m[14] * m[3];
		f32 t8 = m[2] * m[11];
		f32 t9 = m[10] * m[3];
		f32 t10 = m[2] * m[7];
		f32 t11 = m[6] * m[3];
		f32 t12 = m[8] * m[13];
		f32 t13 = m[12] * m[9];
		f32 t14 = m[4] * m[13];
		f32 t15 = m[12] * m[5];
		f32 t16 = m[4] * m[9];
		f32 t17 = m[8] * m[5];
		f32 t18 = m[0] * m[13];
		f32 t19 = m[12] * m[1];
		f32 t20 = m[0] * m[9];
		f32 t21 = m[8] * m[1];
		f32 t22 = m[0] * m[5];
		f32 t23 = m[4] * m[1];

		Mat4 outMatrix{};
		f32* o = outMatrix.a;

		o[0] = (t0 * m[5] + t3 * m[9] + t4 * m[13]) - (t1 * m[5] + t2 * m[9] + t5 * m[13]);
		o[1] = (t1 * m[1] + t6 * m[9] + t9 * m[13]) - (t0 * m[1] + t7 * m[9] + t8 * m[13]);
		o[2] = (t2 * m[1] + t7 * m[5] + t10 * m[13]) - (t3 * m[1] + t6 * m[5] + t11 * m[13]);
		o[3] = (t5 * m[1] + t8 * m[5] + t11 * m[9]) - (t4 * m[1] + t9 * m[5] + t10 * m[9]);

		f32 d = 1.0f / (m[0] * o[0] + m[4] * o[1] + m[8] * o[2] + m[12] * o[3]);

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

		return outMatrix;
	}

	inline Mat4 Mat4::Ortho_RH_NO(f32 left, f32 right, f32 bottom, f32 top, f32 zNear, f32 zFar)
	{
		Mat4 mat(1.0);
		mat[0][0] = 2 / (right - left);
		mat[1][1] = 2 / (top - bottom);
		mat[2][2] = -2 / (zFar - zNear);
		mat[3][0] = -(right + left) / (right - left);
		mat[3][1] = -(top + bottom) / (top - bottom);
		mat[3][2] = -(zFar + zNear) / (zFar - zNear);
		return mat;
	}

	inline Mat4 Mat4::Ortho_RH_ZO(f32 left, f32 right, f32 bottom, f32 top, f32 zNear, f32 zFar)
	{
		Mat4 mat{1.0};
		mat[0][0] = 2.f / (right - left);
		mat[1][1] = 2.f / (top - bottom);
		mat[2][2] = 1.f / (zFar - zNear);
		mat[3][0] = -(right + left) / (right - left);
		mat[3][1] = -(top + bottom) / (top - bottom);
		mat[3][2] = -zNear / (zFar - zNear);
		return mat;
	}

	inline Mat4 Mat4::Ortho(f32 left, f32 right, f32 bottom, f32 top, f32 zNear, f32 zFar)
	{
		return Ortho_RH_ZO(left, right, bottom, top, zNear, zFar);
	}

	inline Mat4 Mat4::Transpose(const Mat4& matrix)
	{
		Mat4 outMatrix{1.0};
		outMatrix.a[0] = matrix.a[0];
		outMatrix.a[1] = matrix.a[4];
		outMatrix.a[2] = matrix.a[8];
		outMatrix.a[3] = matrix.a[12];
		outMatrix.a[4] = matrix.a[1];
		outMatrix.a[5] = matrix.a[5];
		outMatrix.a[6] = matrix.a[9];
		outMatrix.a[7] = matrix.a[13];
		outMatrix.a[8] = matrix.a[2];
		outMatrix.a[9] = matrix.a[6];
		outMatrix.a[10] = matrix.a[10];
		outMatrix.a[11] = matrix.a[14];
		outMatrix.a[12] = matrix.a[3];
		outMatrix.a[13] = matrix.a[7];
		outMatrix.a[14] = matrix.a[11];
		outMatrix.a[15] = matrix.a[15];
		return outMatrix;
	}

	inline Mat4 Mat4::OrthoNormalize(const Mat4& mat)
	{
		Mat4 outMatrix = mat;
		outMatrix.v.up = Vec4::Normalize(outMatrix.v.up);
		outMatrix.v.dir = Vec4::Normalize(outMatrix.v.dir);
		outMatrix.v.right = Vec4::Normalize(outMatrix.v.right);
		return outMatrix;
	}

	inline void Mat4::Decompose(const Mat4& mat, Vec3& translation, Vec3& rotation, Vec3& scale)
	{
		scale[0] = Vec3::Length(mat.v.right);
		scale[1] = Vec3::Length(mat.v.up);
		scale[2] = Vec3::Length(mat.v.dir);

		auto mathNorm = OrthoNormalize(mat);

		rotation[0] = atan2f(mathNorm.m[1][2], mathNorm.m[2][2]);
		rotation[1] = atan2f(-mathNorm.m[0][2], sqrtf(mathNorm.m[1][2] * mathNorm.m[1][2] + mathNorm.m[2][2] * mathNorm.m[2][2]));
		rotation[2] = atan2f(mathNorm.m[0][1], mathNorm.m[0][0]);

		translation[0] = mathNorm.v.position.x;
		translation[1] = mathNorm.v.position.y;
		translation[2] = mathNorm.v.position.z;
	}

	inline void Mat4::Decompose(const Mat4& mat, Vec3& translation)
	{
		auto mathNorm = OrthoNormalize(mat);
		translation[0] = mathNorm.v.position.x;
		translation[1] = mathNorm.v.position.y;
		translation[2] = mathNorm.v.position.z;
	}

	inline Vec3 Mat4::GetScale(const Mat4& mat)
	{
		return {Vec3::Length(mat.v.right), Vec3::Length(mat.v.up), Vec3::Length(mat.v.dir)};
	}

	inline Vec3 Mat4::GetTranslation(const Mat4& mat)
	{
		auto mathNorm = OrthoNormalize(mat);
		return {mathNorm.v.position.x, mathNorm.v.position.y, mathNorm.v.position.z};
	}

	inline Quat Mat4::GetQuaternion(const Mat4 pMat)
	{
		auto mat = OrthoNormalize(pMat);

		float tr = mat.m[0].c[0] + mat.m[1].c[1] + mat.m[2].c[2];

		if (tr >= 0.0f)
		{
			float s = sqrt(tr + 1.0f);
			float is = 0.5f / s;
			return Quat(
				(mat.m[1].c[2] - mat.m[2].c[1]) * is,
				(mat.m[2].c[0] - mat.m[0].c[2]) * is,
				(mat.m[0].c[1] - mat.m[1].c[0]) * is,
				0.5f * s);
		}
		else
		{
			int i = 0;
			if (mat.m[1].c[1] > mat.m[0].c[0]) i = 1;
			if (mat.m[2].c[2] > mat.m[i].c[i]) i = 2;

			if (i == 0)
			{
				float s = sqrt(mat.m[0].c[0] - (mat.m[1].c[1] + mat.m[2].c[2]) + 1);
				float is = 0.5f / s;
				return Quat(
					0.5f * s,
					(mat.m[1].c[0] + mat.m[0].c[1]) * is,
					(mat.m[0].c[2] + mat.m[2].c[0]) * is,
					(mat.m[1].c[2] - mat.m[2].c[1]) * is);
			}
			else if (i == 1)
			{
				float s = sqrt(mat.m[1].c[1] - (mat.m[2].c[2] + mat.m[0].c[0]) + 1);
				float is = 0.5f / s;
				return Quat(
					(mat.m[1].c[0] + mat.m[0].c[1]) * is,
					0.5f * s,
					(mat.m[2].c[1] + mat.m[1].c[2]) * is,
					(mat.m[2].c[0] - mat.m[0].c[2]) * is);
			}
			else
			{
				float s = sqrt(mat.m[2].c[2] - (mat.m[0].c[0] + mat.m[1].c[1]) + 1);
				float is = 0.5f / s;
				return Quat(
					(mat.m[0].c[2] + mat.m[2].c[0]) * is,
					(mat.m[2].c[1] + mat.m[1].c[2]) * is,
					0.5f * s,
					(mat.m[0].c[1] - mat.m[1].c[0]) * is);
			}
		}
	}

	constexpr Vec3 Mat4::GetUpVector(const Mat4& matrix)
	{
		return {matrix[1][0], matrix[1][1], matrix[1][2]};
	}

	constexpr Vec3 Mat4::GetForwardVector(const Mat4& matrix)
	{
		return {-matrix[2][0], -matrix[2][1], -matrix[2][2]};
	}

	inline Mat4 Mat4::EulerXYZ(Vec3 angles)
	{
		f32 cx, cy, cz, sx, sy, sz, czsx, cxcz, sysz;

		sx = sinf(angles[0]);
		cx = cosf(angles[0]);
		sy = sinf(angles[1]);
		cy = cosf(angles[1]);
		sz = sinf(angles[2]);
		cz = cosf(angles[2]);

		czsx = cz * sx;
		cxcz = cx * cz;
		sysz = sy * sz;

		Mat4 dest;
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

	inline Plane::Plane(const Vec3& p1, const Vec3& norm) : normal(Vec3::Normalize(norm)),
	                                                        distance(Vec3::Dot(normal, p1)) {}

	inline Plane::Plane(f32 a, f32 b, f32 c, f32 d)
	{
		f32 length = std::sqrt(a * a + b * b + c * c);
		normal = Vec3(a, b, c) / length;
		distance = d / length;
	}

	inline f32 Plane::GetDistanceToPoint(const Vec3& point) const
	{
		return Vec3::Dot(normal, point) + distance;
	}

	inline void Plane::Normalize()
	{
		f32 length = Vec3::Length(normal);
		normal /= length;
		distance /= length;
	}

	inline void AABB::Expand(const AABB& other)
	{
		min = Vec3::Min(min, other.min);
		max = Vec3::Max(max, other.max);
	}

	inline Vec3 AABB::GetCenter() const
	{
		return (min + max) * 0.5f;
	}

	inline f32 AABB::GetRadius() const
	{
		return Vec3::Length(max - min);
	}

	inline FixedArray<Vec3, 8> AABB::GetCorners() const
	{
		return {
			Vec3(min.x, min.y, min.z), // Bottom-left-back
			Vec3(max.x, min.y, min.z), // Bottom-right-back
			Vec3(max.x, min.y, max.z), // Bottom-right-front
			Vec3(min.x, min.y, max.z), // Bottom-left-front

			// Top face (max.y)
			Vec3(min.x, max.y, min.z), // Top-left-back
			Vec3(max.x, max.y, min.z), // Top-right-back
			Vec3(max.x, max.y, max.z), // Top-right-front
			Vec3(min.x, max.y, max.z)  // Top-left-front
		};
	}

	inline bool AABB::IsOnOrForwardPlane(const Plane& plane) const
	{
		Vec3 negativeVertex = Vec3(
			plane.normal.x < 0 ? max.x : min.x,
			plane.normal.y < 0 ? max.y : min.y,
			plane.normal.z < 0 ? max.z : min.z
		);
		f32 d = plane.GetDistanceToPoint(negativeVertex);
		return d >= 0.f;
	}

	inline bool AABB::IsOnFrustum(const Frustum& camFrustum) const
	{
		for (u32 i = 0; i < 6; ++i)
		{
			const Plane& plane = camFrustum.planes[i];

			Vec3 positiveVertex = min;
			if (plane.normal.x >= 0) positiveVertex.x = max.x;
			if (plane.normal.y >= 0) positiveVertex.y = max.y;
			if (plane.normal.z >= 0) positiveVertex.z = max.z;

			if (plane.GetDistanceToPoint(positiveVertex) < 0)
			{
				return false;
			}
		}
		return true;
	}

	//https://github.com/opengl-tutorials/ogl/blob/master/misc05_picking/misc05_picking_custom.cpp
	inline bool Ray::TestRayOBBIntersection(const AABB& aabb, const Mat4& matrix, float& dist) const
	{
		float tMin = 0.0f;
		float tMax = 100000.0f;

		Vec3 obbPositionWorldSpace{matrix[3].x, matrix[3].y, matrix[3].z};
		Vec3 delta = obbPositionWorldSpace - origin;

		{
			Vec3 xaxis{matrix[0].x, matrix[0].y, matrix[0].z};

			float e = Vec3::Dot(xaxis, delta);
			float f = Vec3::Dot(dir, xaxis);

			if (fabs(f) > 0.001f)
			{
				float t1 = (e + aabb.min.x) / f;
				float t2 = (e + aabb.max.x) / f;

				if (t1 > t2)
				{
					float w = t1;
					t1 = t2;
					t2 = w;
				}

				if (t2 < tMax)
				{
					tMax = t2;
				}


				if (t1 > tMin)
				{
					tMin = t1;
				}

				if (tMax < tMin)
				{
					return false;
				}
			}
			else
			{
				if (-e + aabb.min.x > 0.0f || -e + aabb.max.x < 0.0f)
				{
					return false;
				}
			}
		}


		{
			Vec3  yaxis{matrix[1].x, matrix[1].y, matrix[1].z};
			float e = Vec3::Dot(yaxis, delta);
			float f = Vec3::Dot(dir, yaxis);

			if (fabs(f) > 0.001f)
			{
				float t1 = (e + aabb.min.y) / f;
				float t2 = (e + aabb.max.y) / f;

				if (t1 > t2)
				{
					float w = t1;
					t1 = t2;
					t2 = w;
				}

				if (t2 < tMax)
					tMax = t2;
				if (t1 > tMin)
					tMin = t1;
				if (tMin > tMax)
					return false;
			}
			else
			{
				if (-e + aabb.min.y > 0.0f || -e + aabb.max.y < 0.0f)
					return false;
			}
		}

		{
			Vec3  zaxis{matrix[2].x, matrix[2].y, matrix[2].z};
			float e = Vec3::Dot(zaxis, delta);
			float f = Vec3::Dot(dir, zaxis);

			if (fabs(f) > 0.001f)
			{
				float t1 = (e + aabb.min.z) / f;
				float t2 = (e + aabb.max.z) / f;

				if (t1 > t2)
				{
					float w = t1;
					t1 = t2;
					t2 = w;
				}

				if (t2 < tMax)
					tMax = t2;
				if (t1 > tMin)
					tMin = t1;
				if (tMin > tMax)
					return false;
			}
			else
			{
				if (-e + aabb.min.z > 0.0f || -e + aabb.max.z < 0.0f)
					return false;
			}
		}

		dist = tMin;
		return true;
	}


	inline f32 Halton(i32 i, i32 b)
	{
		// Creates a halton sequence of values between 0 and 1.
		// https://en.wikipedia.org/wiki/Halton_sequence
		// Used for jittering based on a constant set of 2D points.
		f32 f = 1.0f;
		f32 r = 0.0f;
		while (i > 0)
		{
			f = f / f32(b);
			r = r + f * f32(i % b);
			i = i / b;
		}
		return r;
	}

	inline Vec2 Halton23Sequence(i32 index)
	{
		return Vec2{Halton(index, 2), Halton(index, 3)};
	}

	inline f64 Determinant(Mat34 mat)
	{
		// Use only the first 3 columns
		f32 a = mat.matrix[0][0], b = mat.matrix[0][1], c = mat.matrix[0][2];
		f32 d = mat.matrix[1][0], e = mat.matrix[1][1], f = mat.matrix[1][2];
		f32 g = mat.matrix[2][0], h = mat.matrix[2][1], i = mat.matrix[2][2];

		return a * (e * i - f * h)
			- b * (d * i - f * g)
			+ c * (d * h - e * g);
	}


	//hash impl

	template <>
	struct Hash<Vec2>
	{
		constexpr static bool hasHash = true;

		constexpr static usize Value(const Vec2& value)
		{
			usize seed{};
			HashCombine(seed, Hash<Float>::Value(value.x), Hash<Float>::Value(value.y));
			return seed;
		}
	};

	template <>
	struct Hash<Vec3>
	{
		constexpr static bool hasHash = true;

		constexpr static usize Value(const Vec3& value)
		{
			usize seed{};
			HashCombine(seed, Hash<Float>::Value(value.x), Hash<Float>::Value(value.y), Hash<Float>::Value(value.z));
			return seed;
		}
	};

	template <>
	struct Hash<Vec4>
	{
		constexpr static bool hasHash = true;

		constexpr static usize Value(const Vec4& value)
		{
			usize seed{};
			HashCombine(seed, Hash<Float>::Value(value.x), Hash<Float>::Value(value.y), Hash<Float>::Value(value.z), Hash<Float>::Value(value.w));
			return seed;
		}
	};

	template <>
	struct Hash<Quat>
	{
		constexpr static bool hasHash = true;

		constexpr static usize Value(const Quat& value)
		{
			usize seed{};
			HashCombine(seed, Hash<Float>::Value(value.x), Hash<Float>::Value(value.y), Hash<Float>::Value(value.z), Hash<Float>::Value(value.w));
			return seed;
		}
	};

	template <>
	struct Hash<Mat4>
	{
		constexpr static bool hasHash = true;

		constexpr static usize Value(const Mat4& value)
		{
			return AppendValue(value);
		}
	};

	template<>
	struct CompareRound<Quat>
	{
		constexpr static bool IsEqual(Quat a, Quat b)
		{
			return CompareRound<Float>::IsEqual(a.x, b.x) &&
				CompareRound<Float>::IsEqual(a.y, b.y) &&
				CompareRound<Float>::IsEqual(a.z, b.z) &&
				CompareRound<Float>::IsEqual(a.w, b.w);
		}
	};

	struct SK_API Random
	{
		static u64 Xorshift64star();
		static i64 NextInt(i64 max);
		static f32 NextFloat32(f32 min, f32 max);
		static u64 NextUInt(u64 max);

		static void RegisterType(NativeReflectType<Random>& type);
	};
}