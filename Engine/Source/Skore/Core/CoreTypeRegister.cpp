#include "Attributes.hpp"
#include "Color.hpp"
#include "Math.hpp"
#include "Registry.hpp"
#include "UUID.hpp"

namespace Skore
{
    struct UUID;

    void RegisterBaseTypes()
    {
        Registry::Type<bool>("bool");
        Registry::Type<u8>("u8");
        Registry::Type<u16>("u16");
        Registry::Type<u32>("u32");
        Registry::Type<u64>("u64");
        Registry::Type<ul32>("ul32");
        Registry::Type<i8>("i8");
        Registry::Type<i16>("i16");
        Registry::Type<i32>("i32");
        Registry::Type<i64>("i64");
        Registry::Type<f32>("f32");
        Registry::Type<f64>("f64");
        Registry::Type<Array<u8>>("Skore::ByteArray");
        Registry::Type<String>("Skore::String");
        Registry::Type<StringView>("Skore::StringView");
        Registry::Type<Color>();

        auto uuid = Registry::Type<UUID>();
        uuid.Field<&UUID::firstValue>("firstValue");
        uuid.Field<&UUID::secondValue>("secondValue");


        auto allocator = Registry::Type<Allocator>();
        allocator.Function<&Allocator::MemAlloc>("MemAlloc");
        allocator.Function<&Allocator::MemFree>("MemFree");

        Registry::Type<UIProperty>();
        Registry::Type<UIFloatProperty>();
        Registry::Type<ProjectSettings>();

        auto settings = Registry::Type<Settings>();
        settings.Field<&Settings::path>("path");
        settings.Field<&Settings::type>("type");
    }

    void RegisterMathTypes()
    {
        auto extent = Registry::Type<Extent>();
        extent.Field<&Extent::width>("width");
        extent.Field<&Extent::height>("height");

        auto extent3D = Registry::Type<Extent3D>();
        extent3D.Field<&Extent3D::width>("width");
        extent3D.Field<&Extent3D::height>("height");
        extent3D.Field<&Extent3D::depth>("depth");

        auto vec2 = Registry::Type<Vec2>();
        vec2.Field<&Vec2::x>("x");
        vec2.Field<&Vec2::y>("y");

        auto vec3 = Registry::Type<Vec3>();
        vec3.Field<&Vec3::x>("x");
        vec3.Field<&Vec3::y>("y");
        vec3.Field<&Vec3::z>("z");

        auto vec4 = Registry::Type<Vec4>();
        vec4.Field<&Vec4::x>("x");
        vec4.Field<&Vec4::y>("y");
        vec4.Field<&Vec4::z>("z");
        vec4.Field<&Vec4::w>("w");

        auto quat = Registry::Type<Quat>();
        quat.Field<&Quat::x>("x");
        quat.Field<&Quat::y>("y");
        quat.Field<&Quat::z>("z");
        quat.Field<&Quat::w>("w");

        auto aabb = Registry::Type<AABB>();
        aabb.Field<&AABB::min>("min");
        aabb.Field<&AABB::max>("max");
    }


    void RegisterCoreTypes()
    {
        RegisterBaseTypes();
        RegisterMathTypes();
    }
}
