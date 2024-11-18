#include "RigidBodyComponent.hpp"

#include "Fyrion/Core/Attributes.hpp"
#include "Fyrion/Core/Registry.hpp"
#include "Fyrion/Scene/Component/TransformComponent.hpp"

namespace Fyrion
{
    void RigidBodyComponent::OnStart()
    {

    }

    void RigidBodyComponent::RegisterType(NativeTypeHandler<RigidBodyComponent>& type)
    {
        type.Field<&RigidBodyComponent::mass>("mass").Attribute<UIProperty>();
        type.Field<&RigidBodyComponent::friction>("friction").Attribute<UIProperty>();
        type.Field<&RigidBodyComponent::restitution>("restitution").Attribute<UIProperty>();
        type.Field<&RigidBodyComponent::gravityFactor>("gravityFactor").Attribute<UIProperty>();
        type.Field<&RigidBodyComponent::isKinematic>("isKinematic").Attribute<UIProperty>();
        type.Field<&RigidBodyComponent::collisionDetectionType>("collisionDetectionType").Attribute<UIProperty>();

        type.Attribute<ComponentDesc>(ComponentDesc{.dependencies = {GetTypeID<TransformComponent>()}});
    }
}

