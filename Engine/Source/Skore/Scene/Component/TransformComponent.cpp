#include "TransformComponent.hpp"

#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Registry.hpp"
#include "Skore/Scene/GameObject.hpp"
#include "Skore/Scene/SceneTypes.hpp"

namespace Skore
{
    void TransformComponent::UpdateTransform()
    {
        if (gameObject->GetParent() != nullptr)
        {
            if (TransformComponent* parentTransform = gameObject->GetParent()->GetComponent<TransformComponent>())
            {
                UpdateTransform(parentTransform);
                return;
            }
        }
        UpdateTransform(nullptr);
    }

    void TransformComponent::UpdateTransform(const TransformComponent* parentTransform)
    {
        worldTransform = parentTransform != nullptr ? parentTransform->GetWorldTransform() * GetLocalTransform() : GetLocalTransform();

        SceneEventDesc desc{
            .type = SceneEventType::TransformChanged
        };

        gameObject->NotifyEvent(desc);

        for (GameObject* child : gameObject->GetChildren())
        {
            if (TransformComponent* childTransform = child->GetComponent<TransformComponent>())
            {
                childTransform->UpdateTransform(this);
            }
        }
    }

    void TransformComponent::OnStart()
    {
        UpdateTransform();
    }

    void TransformComponent::OnChange()
    {
        UpdateTransform();
    }

    void TransformComponent::RegisterType(NativeTypeHandler<TransformComponent>& type)
    {
        type.Field<&TransformComponent::position, &TransformComponent::GetPosition, &TransformComponent::SetPosition>("position").Attribute<UIProperty>();
        type.Field<&TransformComponent::rotation, &TransformComponent::GetRotation, &TransformComponent::SetRotation>("rotation").Attribute<UIProperty>();
        type.Field<&TransformComponent::scale, &TransformComponent::GetScale, &TransformComponent::SetScale>("scale").Attribute<UIProperty>();

        type.Attribute<ComponentDesc>(ComponentDesc{.allowMultiple = false});
    }


}
