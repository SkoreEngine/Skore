// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "Color.hpp"
#include "Hash.hpp"
#include "UUID.hpp"
#include "Span.hpp"
#include "HashMap.hpp"
#include "Skore/Common.hpp"

namespace Skore
{
	class Variant;
	using Dictionary = HashMap<Variant, Variant>;

	class SK_API Variant
	{
	public:
		enum class Type
		{
			None,
			Bool,
			UInt,
			Int,
			Float,
			String,
			UUID,
			Color,
			Vec2,
			Vec3,
			Vec4,
			Quat,
			Mat4,
			VariantArray,
			Dictionary
		};

		Type GetType() const;

		Variant(const Variant& variant);
		Variant(Variant&& variant) noexcept;

		Variant(bool value);
		Variant(u8 value);
		Variant(u16 value);
		Variant(u32 value);
		Variant(u64 value);
		Variant(i8 value);
		Variant(i16 value);
		Variant(i32 value);
		Variant(i64 value);
		Variant(f32 value);
		Variant(f64 value);
		Variant(StringView value);
		Variant(const char* value);
		Variant(const String& value);
		Variant(const UUID& value);
		Variant(const Color& value);
		Variant(const Vec2& value);
		Variant(const Vec3& value);
		Variant(const Vec4& value);
		Variant(const Quat& value);
		Variant(const Mat4& value);
		Variant(const Span<Variant>& value);
		Variant(const Dictionary& value);

		~Variant();

		operator bool() const;
		operator u8() const;
		operator u16() const;
		operator u32() const;
		operator u64() const;
		operator i8() const;
		operator i16() const;
		operator i32() const;
		operator i64() const;
		operator f32() const;
		operator f64() const;
		operator StringView() const;
		operator String() const;
		operator UUID() const;
		operator Color() const;
		operator Vec2() const;
		operator Vec3() const;
		operator Vec4() const;
		operator Quat() const;
		operator Mat4() const;
		operator Span<Variant>() const;
		operator Dictionary() const;

		bool operator==(const Variant& variant) const;
		bool operator!=(const Variant& variant) const;

		constexpr usize Hash() const;

	private:
		Type m_type = Type::None;

		union
		{
			bool  boolValue;
			u64   u64Value;
			i64   i64Value;
			f64   f64Value;
			UUID  uuidValue;
			Color colorValue;
			Vec2  vec2Value;
			Vec3  vec3Value;
			Vec4  vec4Value;
			Quat  quatValue;
			Mat4  mat4Value;
			char  strBuffer[sizeof(String)];
			char  arrBuffer[sizeof(Array<Variant>)];
			char  dictBuffer[sizeof(Dictionary)];
		} m_data alignas(8){};
	};


	template <>
	struct Hash<Variant>
	{
		constexpr static bool hasHash = true;
		constexpr static usize Value(const Variant& variant)
		{
			return variant.Hash();
		}
	};

	template<typename T>
	struct VariantCast
	{
		constexpr static bool hasSpecialization = false;
	};

#define DEFINE_VARIANT_CAST(TYPE)	\
	template <>						\
	struct VariantCast<TYPE>		\
	{	\
		constexpr static bool hasSpecialization = true;	\
		static Variant ToVariant(const TYPE& value)	\
		{							\
			return {value};			\
		}							\
		\
		static TYPE FromVariant(const Variant& variant)	\
		{												\
			return variant;								\
		}												\
	}

	DEFINE_VARIANT_CAST(bool);
	DEFINE_VARIANT_CAST(u8);
	DEFINE_VARIANT_CAST(u16);
	DEFINE_VARIANT_CAST(u32);
	DEFINE_VARIANT_CAST(u64);
	DEFINE_VARIANT_CAST(i8);
	DEFINE_VARIANT_CAST(i16);
	DEFINE_VARIANT_CAST(i32);
	DEFINE_VARIANT_CAST(i64);
	DEFINE_VARIANT_CAST(f32);
	DEFINE_VARIANT_CAST(f64);
	DEFINE_VARIANT_CAST(String);
	DEFINE_VARIANT_CAST(UUID);
	DEFINE_VARIANT_CAST(Color);
	DEFINE_VARIANT_CAST(Vec2);
	DEFINE_VARIANT_CAST(Vec3);
	DEFINE_VARIANT_CAST(Vec4);
	DEFINE_VARIANT_CAST(Quat);
	DEFINE_VARIANT_CAST(Mat4);
}
