#pragma once

// The Jolt headers don't include Jolt.h. Always include Jolt.h before including any other Jolt header.
// You can use Jolt.h in your precompiled header to speed up compilation.
#include <Jolt/Jolt.h>

// Jolt includes
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>

#include "Fyrion/Common.hpp"
#include "Fyrion/Core/UnorderedDense.hpp"
#include "Fyrion/Physics/PhysicsTypes.hpp"
#include "Jolt/Physics/Character/CharacterVirtual.h"


namespace Fyrion
{
    namespace PhysicsLayers
    {
        static constexpr JPH::ObjectLayer NON_MOVING = 0;
        static constexpr JPH::ObjectLayer MOVING = 1;
        static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
    }

    class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
    {
    public:
        bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
        {
            switch (inObject1)
            {
                case PhysicsLayers::NON_MOVING:
                    return inObject2 == PhysicsLayers::MOVING;
                case PhysicsLayers::MOVING:
                    return true;
                default:
                    FY_ASSERT(false, "Error on physics");
                    return false;
            }
        }
    };

    namespace BroadPhaseLayers
    {
        static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
        static constexpr JPH::BroadPhaseLayer MOVING(1);
        static constexpr u32                  NUM_LAYERS(2);
    }


    class BroadPhaseLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
    {
    public:
        BroadPhaseLayerInterfaceImpl()
        {
            objectToBroadPhase[PhysicsLayers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
            objectToBroadPhase[PhysicsLayers::MOVING] = BroadPhaseLayers::MOVING;
        }

        u32 GetNumBroadPhaseLayers() const override
        {
            return BroadPhaseLayers::NUM_LAYERS;
        }

        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
        {
            FY_ASSERT(inLayer < PhysicsLayers::NUM_LAYERS, "Error on Physics");
            return objectToBroadPhase[inLayer];
        }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
        {
            switch ((JPH::BroadPhaseLayer::Type)inLayer)
            {
                case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING:
                    return "NON_MOVING";
                case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:
                    return "MOVING";
                default:
                    FY_ASSERT(false, "Error on Physics");
                    return "INVALID";
            }
        }
#endif

    private:
        JPH::BroadPhaseLayer objectToBroadPhase[PhysicsLayers::NUM_LAYERS];
    };

    class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
    {
    public:
        virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
        {
            switch (inLayer1)
            {
                case PhysicsLayers::NON_MOVING:
                    return inLayer2 == BroadPhaseLayers::MOVING;
                case PhysicsLayers::MOVING:
                    return true;
                default:
                    FY_ASSERT(false, "Error on Physics");
                    return false;
            }
        }
    };

    FY_FINLINE JPH::Vec3 Cast(const Vec3& vec3)
    {
        return JPH::Vec3{vec3.x, vec3.y, vec3.z};
    }

    FY_FINLINE Vec3 Cast(const JPH::Vec3& vec3)
    {
        return Vec3{vec3.GetX(), vec3.GetY(), vec3.GetZ()};
    }

    FY_FINLINE JPH::Vec4 Cast(const Vec4& vec4)
    {
        return JPH::Vec4{vec4.x, vec4.y, vec4.z, vec4.w};
    }

    FY_FINLINE JPH::Quat Cast(const Quat& quat)
    {
        return JPH::Quat{quat.x, quat.y, quat.z, quat.w};
    }

    FY_FINLINE Quat Cast(const JPH::Quat& quat)
    {
        return Quat{quat.GetX(), quat.GetY(), quat.GetZ(), quat.GetW()};
    }

    FY_FINLINE JPH::EMotionQuality CastQuality(const CollisionDetectionType& collisionDetection)
    {
        switch (collisionDetection)
        {
            case CollisionDetectionType::Discrete: return JPH::EMotionQuality::Discrete;
            case CollisionDetectionType::LinearCast: return JPH::EMotionQuality::LinearCast;
        }
        return JPH::EMotionQuality::Discrete;
    }


    struct PhysicsContext
    {
        JPH::TempAllocatorImpl tempAllocator = JPH::TempAllocatorImpl(10 * 1024 * 1024);
        JPH::PhysicsSystem     physicsSystem{};
        f32                    stepSize{};

        BroadPhaseLayerInterfaceImpl      broadPhaseLayerInterfaceImpl = {};
        ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilterImpl = {};
        ObjectLayerPairFilterImpl         objectLayerPairFilterImpl = {};
        JPH::JobSystemThreadPool          jobSystem = JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1);

        ankerl::unordered_dense::set<JPH::CharacterVirtual*> virtualCharacters;
    };
}
