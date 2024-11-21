#include "CameraComponent.hpp"

#include "TransformComponent.hpp"
#include "Fyrion/Core/Attributes.hpp"
#include "Fyrion/Core/Registry.hpp"
#include "Fyrion/Scene/GameObject.hpp"
#include "Fyrion/Scene/Scene.hpp"
#include "Fyrion/Graphics/RenderProxy.hpp"

namespace Fyrion
{
    void CameraComponent::OnStart()
    {
        transformComponent = gameObject->GetComponent<TransformComponent>();
        renderProxy = gameObject->GetScene()->GetProxy<RenderProxy>();
        OnChange();
    }

    void CameraComponent::OnChange()
    {
        if (renderProxy)
        {
            if (transformComponent && current)
            {
                renderProxy->AddCamera(this, CameraData{
                                           .view = Math::Inverse(transformComponent->GetWorldTransform()),
                                           .viewPos = Math::GetTranslation(transformComponent->GetWorldTransform()),
                                           .fov = fov,
                                           .nearClip = near,
                                           .farClip = far
                                       });
            }
            else
            {
                renderProxy->RemoveCamera(this);
            }
        }
    }

    void CameraComponent::ProcessEvent(const SceneEventDesc& event)
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

    void CameraComponent::OnDestroy()
    {
        if (renderProxy)
        {
            renderProxy->RemoveCamera(this);
        }
    }

    void CameraComponent::RegisterType(NativeTypeHandler<CameraComponent>& type)
    {
        type.Field<&CameraComponent::projection>("projection").Attribute<UIProperty>();
        type.Field<&CameraComponent::fov>("fov").Attribute<UIProperty>();
        type.Field<&CameraComponent::near>("near").Attribute<UIProperty>();
        type.Field<&CameraComponent::far>("far").Attribute<UIProperty>();
        type.Field<&CameraComponent::current>("current").Attribute<UIProperty>();
        type.Attribute<ComponentDesc>(ComponentDesc{.dependencies = {GetTypeID<TransformComponent>()}});
    }
}
