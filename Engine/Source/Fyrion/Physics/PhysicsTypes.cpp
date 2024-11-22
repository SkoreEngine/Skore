#include "PhysicsTypes.hpp"

#include "Fyrion/Core/Attributes.hpp"
#include "Fyrion/Core/Registry.hpp"

namespace Fyrion
{
    void RegisterPhysicsTypes()
    {
        Registry::Type<PhysicsSettings>();

        auto collisionDetectionType = Registry::Type<CollisionDetectionType>();
        collisionDetectionType.Value<CollisionDetectionType::Discrete>("Discrete");
        collisionDetectionType.Value<CollisionDetectionType::LinearCast>("LinearCast");
    }

    void PhysicsSettings::RegisterType(NativeTypeHandler<PhysicsSettings>& type)
    {
        type.Field<&PhysicsSettings::maxBodies>("maxBodies").Attribute<UIProperty>();
        type.Field<&PhysicsSettings::maxBodyPairs>("maxBodyPairs").Attribute<UIProperty>();
        type.Field<&PhysicsSettings::maxContactConstraints>("maxContactConstraints").Attribute<UIProperty>();
        type.Field<&PhysicsSettings::physicsTicksPerSeconds>("physicsTicksPerSeconds").Attribute<UIProperty>();
        type.Attribute<Settings>(Settings{
            .path = "Physics/3D",
            .type = GetTypeID<ProjectSettings>(
            )
        });
    }
}
