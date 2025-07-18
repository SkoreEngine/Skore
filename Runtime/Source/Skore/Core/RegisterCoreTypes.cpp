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


#include "Color.hpp"
#include "Logger.hpp"
#include "Object.hpp"
#include "Reflection.hpp"

namespace Skore
{

	void RegisterMathTypes()
	{
		auto extent = Reflection::Type<Extent>();
		extent.Field<&Extent::width>("width");
		extent.Field<&Extent::height>("height");

		auto extent3D = Reflection::Type<Extent3D>();
		extent3D.Field<&Extent3D::width>("width");
		extent3D.Field<&Extent3D::height>("height");
		extent3D.Field<&Extent3D::depth>("depth");

		auto vec2 = Reflection::Type<Vec2>();
		vec2.Field<&Vec2::x>("x");
		vec2.Field<&Vec2::y>("y");

		auto vec3 = Reflection::Type<Vec3>();
		vec3.Field<&Vec3::x>("x");
		vec3.Field<&Vec3::y>("y");
		vec3.Field<&Vec3::z>("z");

		auto vec4 = Reflection::Type<Vec4>();
		vec4.Field<&Vec4::x>("x");
		vec4.Field<&Vec4::y>("y");
		vec4.Field<&Vec4::z>("z");
		vec4.Field<&Vec4::w>("w");

		auto quat = Reflection::Type<Quat>();
		quat.Field<&Quat::x>("x");
		quat.Field<&Quat::y>("y");
		quat.Field<&Quat::z>("z");
		quat.Field<&Quat::w>("w");

		auto aabb = Reflection::Type<AABB>();
		aabb.Field<&AABB::min>("min");
		aabb.Field<&AABB::max>("max");


		Reflection::Type<Color>();
		Reflection::Type<Transform>();
	}

	void RegisterCoreTypes()
	{
		Reflection::Type<bool>("bool");
		Reflection::Type<u8>("u8");
		Reflection::Type<u16>("u16");
		Reflection::Type<u32>("u32");
		Reflection::Type<u64>("u64");
		Reflection::Type<ul32>("ul32");
		Reflection::Type<i8>("i8");
		Reflection::Type<i16>("i16");
		Reflection::Type<i32>("i32");
		Reflection::Type<i64>("i64");
		Reflection::Type<f32>("f32");
		Reflection::Type<f64>("f64");
		Reflection::Type<Array<u8>>("Skore::ByteArray");
		Reflection::Type<String>("Skore::String");
		Reflection::Type<StringView>("Skore::StringView");

		Reflection::Type<Object>();
		Reflection::Type<Logger>();


		RegisterMathTypes();

	}
}
