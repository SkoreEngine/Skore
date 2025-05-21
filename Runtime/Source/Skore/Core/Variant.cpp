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

#include "Variant.hpp"
#include "Array.hpp"


namespace Skore
{
	Variant::Type Variant::GetType() const
	{
		return m_type;
	}

	Variant::Variant(bool value) : m_type(Type::Bool)
	{
		m_data.boolValue = value;
	}

	Variant::Variant(u8 value) : m_type(Type::UInt)
	{
		m_data.u64Value = value;
	}

	Variant::Variant(u16 value) : m_type(Type::UInt)
	{
		m_data.u64Value = value;
	}

	Variant::Variant(u32 value) : m_type(Type::UInt)
	{
		m_data.u64Value = value;
	}

	Variant::Variant(u64 value) : m_type(Type::UInt)
	{
		m_data.u64Value = value;
	}

	Variant::Variant(i8 value) : m_type(Type::Int)
	{
		m_data.i64Value = value;
	}

	Variant::Variant(i16 value) : m_type(Type::Int)
	{
		m_data.i64Value = value;
	}

	Variant::Variant(i32 value) : m_type(Type::Int)
	{
		m_data.i64Value = value;
	}

	Variant::Variant(i64 value) : m_type(Type::Int)
	{
		m_data.i64Value = value;
	}

	Variant::Variant(f32 value) : m_type(Type::Float)
	{
		m_data.f64Value = value;
	}

	Variant::Variant(f64 value) : m_type(Type::Float)
	{
		m_data.f64Value = value;
	}

	Variant::Variant(StringView value) : m_type(Type::String)
	{
		new(m_data.strBuffer) String(value);
	}

	Variant::Variant(const String& value) : m_type(Type::String)
	{
		new(m_data.strBuffer) String(value);
	}

	Variant::Variant(const UUID& uuid) : m_type(Type::UUID)
	{
		m_data.uuidValue = uuid;
	}

	Variant::Variant(const char* value) : m_type(Type::String)
	{
		new(m_data.strBuffer) String(value);
	}

	Variant::Variant(const Vec2& value) : m_type(Type::Vec2)
	{
		m_data.vec2Value = value;
	}

	Variant::Variant(const Vec3& value) : m_type(Type::Vec3)
	{
		m_data.vec3Value = value;
	}

	Variant::Variant(const Vec4& value) : m_type(Type::Vec4)
	{
		m_data.vec4Value = value;
	}

	Variant::Variant(const Quat& value) : m_type(Type::Quat)
	{
		m_data.quatValue = value;
	}

	Variant::Variant(const Mat4& value) : m_type(Type::Mat4)
	{
		m_data.mat4Value = value;
	}

	Variant::Variant(const Span<Variant>& value) : m_type(Type::VariantArray)
	{
		new(m_data.arrBuffer) Array(value);
	}

	Variant::Variant(const Dictionary& value) : m_type(Type::Dictionary)
	{
		new(m_data.dictBuffer) Dictionary(value);
	}

	Variant::~Variant()
	{
		switch (m_type)
		{
			case Type::String:
				reinterpret_cast<String*>(m_data.strBuffer)->~String();
				break;
			case Type::VariantArray:
				reinterpret_cast<Array<Variant>*>(m_data.strBuffer)->~Array<Variant>();
				break;
			default:
				break;
		}
	}

	Variant::operator bool() const
	{
		return m_data.boolValue;
	}

	Variant::operator u8() const
	{
		return m_data.u64Value;
	}

	Variant::operator u16() const
	{
		return m_data.u64Value;
	}

	Variant::operator u32() const
	{
		return m_data.u64Value;
	}

	Variant::operator u64() const
	{
		return m_data.u64Value;
	}

	Variant::operator i8() const
	{
		return m_data.i64Value;
	}

	Variant::operator i16() const
	{
		return m_data.i64Value;
	}

	Variant::operator i32() const
	{
		return m_data.i64Value;
	}

	Variant::operator i64() const
	{
		return m_data.i64Value;
	}

	Variant::operator float() const
	{
		return m_data.f64Value;
	}

	Variant::operator f64() const
	{
		return m_data.f64Value;
	}

	Variant::operator StringView() const
	{
		const String* str = reinterpret_cast<const String*>(m_data.strBuffer);
		return *str;
	}

	Variant::operator UUID() const
	{
		return m_data.uuidValue;
	}

	Variant::operator Vec2() const
	{
		return m_data.vec2Value;
	}

	Variant::operator Vec3() const
	{
		return m_data.vec3Value;
	}

	Variant::operator Vec4() const
	{
		return m_data.vec4Value;
	}

	Variant::operator Quat() const
	{
		return m_data.quatValue;
	}

	Variant::operator Mat4() const
	{
		return m_data.mat4Value;
	}

	Variant::operator Span<Variant>() const
	{
		return *reinterpret_cast<const Array<Variant>*>(m_data.arrBuffer);
	}

	Variant::operator Dictionary() const
	{
		return *reinterpret_cast<const Dictionary*>(m_data.dictBuffer);
	}

	bool Variant::operator==(const Variant& variant) const
	{
		if (m_type != variant.m_type) return false;

		switch (m_type)
		{
			case Type::None:
				return true;
			case Type::Bool:
				return m_data.boolValue == variant.m_data.boolValue;
			case Type::UInt:
				return m_data.u64Value == variant.m_data.u64Value;
			case Type::Int:
				return m_data.i64Value == variant.m_data.i64Value;
			case Type::Float:
				return m_data.f64Value == variant.m_data.f64Value;
			case Type::String:
				return *reinterpret_cast<const String*>(m_data.strBuffer) == *reinterpret_cast<const String*>(variant.m_data.strBuffer);
			case Type::UUID:
				return m_data.uuidValue == variant.m_data.uuidValue;
			case Type::Vec2:
				return m_data.vec2Value == variant.m_data.vec2Value;
			case Type::Vec3:
				return m_data.vec3Value == variant.m_data.vec3Value;
			case Type::Vec4:
				return m_data.vec4Value == variant.m_data.vec4Value;
			case Type::Quat:
				return m_data.quatValue == variant.m_data.quatValue;
			case Type::Mat4:
				return m_data.mat4Value == variant.m_data.mat4Value;
			default:
				break;
		}
		return false;
	}

	bool Variant::operator!=(const Variant& variant) const
	{
		return !(*this == variant);
	}

	constexpr usize Variant::Hash() const
	{
		switch (m_type)
		{
			case Type::None:
				return 0;
			case Type::Bool:
				return HashValue(m_data.boolValue);
			case Type::UInt:
				return HashValue(m_data.u64Value);
			case Type::Int:
				return HashValue(m_data.i64Value);
			case Type::Float:
				return HashValue(m_data.f64Value);
			case Type::String:
				return HashValue(*reinterpret_cast<const String*>(m_data.strBuffer));
			case Type::UUID:
				return HashValue(m_data.uuidValue);
			case Type::Vec2:
				return HashValue(m_data.vec2Value);
			case Type::Vec3:
				return HashValue(m_data.vec3Value);
			case Type::Vec4:
				return HashValue(m_data.vec4Value);
			case Type::Quat:
				return HashValue(m_data.quatValue);
			case Type::Mat4:
				return HashValue(m_data.mat4Value);
			default:
				break;
		}

		return 0;
	}
}
