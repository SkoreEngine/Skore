#include "RenderComponent.hpp"

#include "Fyrion/Core/Attributes.hpp"
#include "Fyrion/Core/Registry.hpp"
#include "Fyrion/Graphics/Assets/MeshAsset.hpp"
#include "Fyrion/Scene/GameObject.hpp"
#include "Fyrion/Scene/Scene.hpp"
#include "Fyrion/Scene/SceneTypes.hpp"

namespace Fyrion
{
    void RenderComponent::OnStart()
    {
        transform = gameObject->GetComponent<TransformComponent>();
        renderService = gameObject->GetScene()->GetService<RenderService>();

        if (mesh && transform)
        {
            renderService->SetMesh(this, mesh, materials, transform->GetWorldTransform());
        }
    }

    MeshAsset* RenderComponent::GetMesh() const
    {
        return mesh;
    }

    Span<MaterialAsset*> RenderComponent::GetMaterials() const
    {
        return materials;
    }

    void RenderComponent::OnChange()
    {
        if (mesh)
        {
            materials = mesh->materials;
            renderService->SetMesh(this, mesh, materials, transform->GetWorldTransform());
        }
        else
        {
            materials.Clear();
            renderService->RemoveMesh(this);
        }
    }

    void RenderComponent::OnDestroy()
    {
        renderService->RemoveMesh(this);
    }

    void RenderComponent::ProcessEvent(const SceneEventDesc& event)
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

    void RenderComponent::SetMesh(MeshAsset* mesh)
    {
        this->mesh = mesh;
        OnChange();
    }

    void RenderComponent::RegisterType(NativeTypeHandler<RenderComponent>& type)
    {
        type.Field<&RenderComponent::mesh>("mesh").Attribute<UIProperty>();
        type.Field<&RenderComponent::materials>("materials").Attribute<UIProperty>().Attribute<UIArrayProperty>(UIArrayProperty{.canAdd = false, .canRemove = false});

        type.Attribute<ComponentDesc>(ComponentDesc{.dependencies = {GetTypeID<TransformComponent>()}});
    }
}
