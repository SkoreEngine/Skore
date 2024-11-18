#include "LightComponent.hpp"

#include "Fyrion/Core/Attributes.hpp"
#include "Fyrion/Core/Registry.hpp"
#include "Fyrion/Scene/GameObject.hpp"
#include "TransformComponent.hpp"
#include "Fyrion/Scene/Scene.hpp"
#include "Fyrion/Graphics/RenderProxy.hpp"


namespace Fyrion
{
    void LightComponent::OnStart()
    {
        transformComponent = gameObject->GetComponent<TransformComponent>();
        renderProxy = gameObject->GetScene()->GetProxy<RenderProxy>();

        OnChange();
    }

    LightType LightComponent::GetType() const
    {
        return type;
    }

    void LightComponent::SetType(LightType type)
    {
        this->type = type;
        OnChange();
    }

    f32 LightComponent::GetIntensity() const
    {
        return intensity;
    }

    void LightComponent::SetIntensity(f32 intensity)
    {
        this->intensity = intensity;
        OnChange();
    }

    f32 LightComponent::GetIndirectMultiplier() const
    {
        return indirectMultiplier;
    }

    void LightComponent::SetIndirectMultiplier(f32 indirectMultiplier)
    {
        this->indirectMultiplier = indirectMultiplier;
        OnChange();
    }

    bool LightComponent::IsCastShadows() const
    {
        return castShadows;
    }

    void LightComponent::SetCastShadows(bool castShadows)
    {
        this->castShadows = castShadows;
        OnChange();
    }

    void LightComponent::ProcessEvent(const SceneEventDesc& event)
    {
        switch (event.type)
        {
            case SceneEventType::TransformChanged:
            {
                OnChange();
                break;
            }
        }
    }

    void LightComponent::OnChange()
    {
        if (renderProxy && transformComponent)
        {
            LightProperties properties;
            properties.type = type;
            properties.direction = transformComponent->GetRotation() * Vec3::AxisY();
            properties.position = transformComponent->GetPosition();
            properties.color = color;
            properties.range = range;
            properties.intensity = intensity;
            properties.indirectMultiplier = indirectMultiplier;
            properties.castShadows = castShadows;

            renderProxy->AddLight(this, properties);
        }
    }

    void LightComponent::OnDestroy()
    {
        if (renderProxy)
        {
            renderProxy->RemoveLight(this);
        }
    }

    void LightComponent::RegisterType(NativeTypeHandler<LightComponent>& type)
    {
        type.Field<&LightComponent::type>("type").Attribute<UIProperty>();
        type.Field<&LightComponent::color>("color").Attribute<UIProperty>();
        type.Field<&LightComponent::intensity>("intensity").Attribute<UIProperty>();
        type.Field<&LightComponent::indirectMultiplier>("indirectMultiplier").Attribute<UIProperty>();
        // type.Field<&LightComponent::range>("range").Attribute<UIProperty>();
        type.Field<&LightComponent::castShadows>("castShadows").Attribute<UIProperty>();

        type.Attribute<ComponentDesc>(ComponentDesc{.dependencies = {GetTypeID<TransformComponent>()}});
    }
}
