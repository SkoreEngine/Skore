#include "BoxColliderComponent.hpp"

#include "RigidBodyComponent.hpp"
#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Registry.hpp"
#include "Skore/Scene/GameObject.hpp"
#include "Skore/Scene/Component/TransformComponent.hpp"


namespace Skore
{
    const Vec3& BoxColliderComponent::GetHalfSize() const
    {
        return halfSize;
    }

    void BoxColliderComponent::SetHalfSize(const Vec3& halfSize)
    {
        this->halfSize = halfSize;
    }

    f32 BoxColliderComponent::GetDensity() const
    {
        return density;
    }

    void BoxColliderComponent::SetDensity(f32 density)
    {
        this->density = density;
    }

    bool BoxColliderComponent::IsIsSensor() const
    {
        return isSensor;
    }

    void BoxColliderComponent::SetIsSensor(bool isSensor)
    {
        this->isSensor = isSensor;
    }

    void BoxColliderComponent::CollectShapes(Array<BodyShapeBuilder>& shapes)
    {
        shapes.EmplaceBack(BodyShapeBuilder{
            .bodyShape = BodyShapeType::Box,
            .size = halfSize,
            .density = density,
            .sensor = isSensor
        });
    }

    void BoxColliderComponent::RegisterType(NativeTypeHandler<BoxColliderComponent>& type)
    {
        type.Field<&BoxColliderComponent::halfSize>("halfSize").Attribute<UIProperty>();
        type.Field<&BoxColliderComponent::density>("density").Attribute<UIProperty>();
        type.Field<&BoxColliderComponent::isSensor>("isSensor").Attribute<UIProperty>();
        type.Attribute<ComponentDesc>(ComponentDesc{.dependencies = {GetTypeID<TransformComponent>()}});
    }
}
