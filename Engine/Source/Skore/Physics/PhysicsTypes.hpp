#pragma once
#include "Skore/Common.hpp"
#include "Skore/Core/Math.hpp"

namespace Skore
{
    enum class CollisionDetectionType
    {
        Discrete,
        LinearCast
    };

    struct PhysicsSettings
    {
        u32 maxBodies = 65536;
        u32 maxBodyPairs = 65536;
        u32 maxContactConstraints = 10240;
        u32 physicsTicksPerSeconds = 75;


        static void RegisterType(NativeTypeHandler<PhysicsSettings>& type);
    };

    enum class BodyShapeType
    {
        None     = 0,
        Plane    = 1,
        Box      = 2,
        Sphere   = 3,
        Capsule  = 4,
        Cylinder = 5,
        Mesh     = 6,
        Convex   = 7,
        Terrain  = 8
    };

    struct BodyShapeBuilder
    {
        BodyShapeType bodyShape = BodyShapeType::None;
        Vec3          size{1.0f, 1.0f, 1.0f};
        f32           height{1.0};
        f32           radius{0.5};
        f32           density = 1000;
        bool          sensor = false;
    };
}
