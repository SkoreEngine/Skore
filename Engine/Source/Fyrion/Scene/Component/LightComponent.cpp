#include "LightComponent.hpp"

#include "Fyrion/Core/Attributes.hpp"
#include "Fyrion/Core/Registry.hpp"
#include "Fyrion/Scene/GameObject.hpp"
#include "TransformComponent.hpp"
#include "Fyrion/Scene/Scene.hpp"
#include "Fyrion/Scene/Service/RenderService.hpp"


namespace Fyrion
{
    void LightComponent::OnStart()
    {
        transformComponent = gameObject->GetComponent<TransformComponent>();
        renderService = gameObject->GetScene()->GetService<RenderService>();
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
        if (renderService && transformComponent)
        {
            switch (type)
            {
                case LightType::Directional:
                {
                    renderService->AddDirectionalLight(this, DirectionalLight{
                                                           .direction = Math::MakeVec4(transformComponent->GetRotation() * Vec3::AxisY()),
                                                           .color = this->color,
                                                           .intensity = this->intensity,
                                                           .indirectMultiplier = this->indirectMultiplier,
                                                           .castShadows = this->castShadows,
                                                       });
                    break;
                }
                case LightType::Point:
                    break;
                case LightType::Spot:
                    break;
                case LightType::Area:
                    break;
            }
        }
    }



    void LightComponent::OnDestroy()
    {
        renderService->RemoveDirectionalLight(this);
    }

    void LightComponent::RegisterType(NativeTypeHandler<LightComponent>& type)
    {
        type.Field<&LightComponent::type>("type").Attribute<UIProperty>();
        type.Field<&LightComponent::color>("color").Attribute<UIProperty>();
        type.Field<&LightComponent::intensity>("intensity").Attribute<UIProperty>();
        type.Field<&LightComponent::indirectMultiplier>("indirectMultiplier").Attribute<UIProperty>();
        type.Field<&LightComponent::castShadows>("castShadows").Attribute<UIProperty>();

        type.Attribute<ComponentDesc>(ComponentDesc{.dependencies = {GetTypeID<TransformComponent>()}});
    }
}
