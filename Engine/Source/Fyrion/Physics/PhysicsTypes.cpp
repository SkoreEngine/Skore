#include "PhysicsTypes.hpp"

#include "Fyrion/Core/Registry.hpp"

namespace Fyrion
{
    void RegisterPhysicsTypes()
    {
        auto collisionDetectionType = Registry::Type<CollisionDetectionType>();
        collisionDetectionType.Value<CollisionDetectionType::Discrete>("Discrete");
        collisionDetectionType.Value<CollisionDetectionType::LinearCast>("LinearCast");
    }
}
