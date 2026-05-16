
#include "Skore/Core/Color.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Object.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Settings.hpp"
#include "Skore/Scene/Components/Transform.hpp"

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
		extent3D.Function<&Extent3D::Max>("Max", "lhs", "rhs");

		auto vec2 = Reflection::Type<Vec2>();
		vec2.Field<&Vec2::x>("x");
		vec2.Field<&Vec2::y>("y");
		vec2.Constructor<f32, f32>("x", "y");
		vec2.Function<&Vec2::Abs>("Abs", "value");
		vec2.Function<&Vec2::Dot>("Dot", "a", "b");
		vec2.Function<&Vec2::Length>("Length", "a");
		vec2.Function<&Vec2::Distance>("Distance", "a", "b");
		vec2.Function<&Vec2::Lerp<Float>>("Lerp", "a", "b", "alpha");
		vec2.Function<static_cast<Vec2(*)(const Float&, const Float&)>(&Vec2::Make)>("Make", "x", "y");

		auto vec3 = Reflection::Type<Vec3>();
		vec3.Field<&Vec3::x>("x");
		vec3.Field<&Vec3::y>("y");
		vec3.Field<&Vec3::z>("z");
		vec3.Constructor<f32, f32, f32>("x", "y", "z");
		vec3.Function<&Vec3::Abs>("Abs", "value");
		vec3.Function<&Vec3::Dot>("Dot", "a", "b");
		vec3.Function<&Vec3::Length>("Length", "a");
		vec3.Function<&Vec3::Distance>("Distance", "a", "b");
		vec3.Function<&Vec3::Cross>("Cross", "a", "b");
		vec3.Function<&Vec3::Scale>("Scale", "a", "s");
		vec3.Function<&Vec3::Normalize>("Normalize", "a");
		vec3.Function<&Vec3::Min>("Min", "lhs", "rhs");
		vec3.Function<&Vec3::Max>("Max", "lhs", "rhs");
		vec3.Function<&Vec3::Lerp<Float>>("Lerp", "a", "b", "alpha");
		vec3.Function<static_cast<Vec3(*)(const Vec3&, const Vec3&, Float)>(&Vec3::Mix)>("Mix", "a", "b", "t");
		vec3.Function<&Vec3::Radians>("Radians", "other");
		vec3.Function<&Vec3::Degrees>("Degrees", "radians");
		vec3.Function<&Vec3::Sin>("Sin", "other");
		vec3.Function<&Vec3::Cos>("Cos", "other");
		vec3.Function<static_cast<Vec3(*)(const Vec4&)>(&Vec3::Make)>("Make", "value");
		vec3.Function<&Vec3::Round>("Round", "v");

		auto vec4 = Reflection::Type<Vec4>();
		vec4.Field<&Vec4::x>("x");
		vec4.Field<&Vec4::y>("y");
		vec4.Field<&Vec4::z>("z");
		vec4.Field<&Vec4::w>("w");
		vec4.Constructor<f32, f32, f32, f32>("x", "y", "z", "w");
		vec4.Function<&Vec4::Dot>("Dot", "a", "b");
		vec4.Function<&Vec4::Length>("Length", "a");
		vec4.Function<&Vec4::Distance>("Distance", "a", "b");
		vec4.Function<&Vec4::Scale>("Scale", "a", "s");
		vec4.Function<&Vec4::Normalize>("Normalize", "a");
		vec4.Function<static_cast<Vec4(*)(const Vec3&, Float)>(&Vec4::Make)>("Make", "value", "w");
		vec4.Function<&Vec4::Lerp<Float>>("Lerp", "a", "b", "alpha");
		vec4.Function<&Vec4::Round>("Round", "v");

		auto quat = Reflection::Type<Quat>();
		quat.Field<&Quat::x>("x");
		quat.Field<&Quat::y>("y");
		quat.Field<&Quat::z>("z");
		quat.Field<&Quat::w>("w");
		quat.Constructor<f32, f32, f32, f32>("x", "y", "z", "w");
		quat.Function<&Quat::Normalized>("Normalized", "value");
		quat.Function<&Quat::DotProduct>("DotProduct", "value", "other");
		quat.Function<&Quat::Slerp>("Slerp", "q0", "q1", "percentage");
		quat.Function<&Quat::Roll>("Roll", "q");
		quat.Function<&Quat::Yaw>("Yaw", "q");
		quat.Function<&Quat::Pitch>("Pitch", "q");
		quat.Function<&Quat::AngleAxis>("AngleAxis", "angle", "v");
		quat.Function<&Quat::EulerAngles>("EulerAngles", "quat");
		quat.Function<&Quat::ToMatrix4>("ToMatrix4", "q");

		auto mat4 = Reflection::Type<Mat4>();
		mat4.Function<static_cast<Mat4(*)(const Vec3&)>(&Mat4::Scale)>("Scale", "scale");
		mat4.Function<&Mat4::RotateX>("RotateX", "angleRadians");
		mat4.Function<&Mat4::RotateY>("RotateY", "angleRadians");
		mat4.Function<&Mat4::RotateZ>("RotateZ", "angleRadians");
		mat4.Function<&Mat4::Rotate>("Rotate", "m", "rx", "ry", "rz");
		mat4.Function<&Mat4::PerspectiveRH_ZO>("PerspectiveRH_ZO", "fovRadians", "aspectRatio", "zNear", "zFar");
		mat4.Function<&Mat4::PerspectiveRH_NO>("PerspectiveRH_NO", "fovRadians", "aspectRatio", "zNear", "zFar");
		mat4.Function<&Mat4::Perspective>("Perspective", "fovRadians", "aspectRatio", "zNear", "zFar");
		mat4.Function<&Mat4::LookAt>("LookAt", "eye", "center", "up");
		mat4.Function<static_cast<Mat4(*)(const Vec3&)>(&Mat4::Translate)>("Translate", "v");
		mat4.Function<&Mat4::Inverse>("Inverse", "mat");
		mat4.Function<&Mat4::Ortho_RH_NO>("Ortho_RH_NO", "left", "right", "bottom", "top", "zNear", "zFar");
		mat4.Function<&Mat4::Ortho_RH_ZO>("Ortho_RH_ZO", "left", "right", "bottom", "top", "zNear", "zFar");
		mat4.Function<&Mat4::Ortho>("Ortho", "left", "right", "bottom", "top", "zNear", "zFar");
		mat4.Function<&Mat4::Transpose>("Transpose", "matrix");
		mat4.Function<&Mat4::OrthoNormalize>("OrthoNormalize", "mat");
		mat4.Function<static_cast<void(*)(const Mat4&, Vec3&, Vec3&, Vec3&)>(&Mat4::Decompose)>("Decompose", "mat", "translation", "rotation", "scale");
		mat4.Function<&Mat4::GetScale>("GetScale", "mat");
		mat4.Function<&Mat4::GetTranslation>("GetTranslation", "mat");
		mat4.Function<&Mat4::GetQuaternion>("GetQuaternion", "pMat");
		mat4.Function<&Mat4::GetUpVector>("GetUpVector", "matrix");
		mat4.Function<&Mat4::GetForwardVector>("GetForwardVector", "matrix");
		mat4.Function<&Mat4::EulerXYZ>("EulerXYZ", "angles");

		auto aabb = Reflection::Type<AABB>();
		aabb.Field<&AABB::min>("min");
		aabb.Field<&AABB::max>("max");

		Reflection::Type<Random>();
		Reflection::Type<Color>();
	}

	void RegisterSettingsType();

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
		Reflection::Type<ProjectSettings>();

		Reflection::Type<Object>();

		auto logLevel = Reflection::Type<LogLevel>();
		logLevel.Value<LogLevel::Trace>("Trace");
		logLevel.Value<LogLevel::Debug>("Debug");
		logLevel.Value<LogLevel::Info>("Info");
		logLevel.Value<LogLevel::Warn>("Warn");
		logLevel.Value<LogLevel::Error>("Error");
		logLevel.Value<LogLevel::Critical>("Critical");

		Reflection::Type<Logger>();


		RegisterMathTypes();
		RegisterSettingsType();

	}
}