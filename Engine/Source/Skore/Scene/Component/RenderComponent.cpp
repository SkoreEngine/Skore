#include "RenderComponent.hpp"

#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Registry.hpp"
#include "Skore/Graphics/Assets/MeshAsset.hpp"
#include "Skore/Scene/GameObject.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/SceneTypes.hpp"

namespace Skore
{
    void RenderComponent::OnStart()
    {
        transform = gameObject->GetComponent<TransformComponent>();
        renderProxy = gameObject->GetScene()->GetProxy<RenderProxy>();

        if (renderProxy && mesh && transform)
        {
            renderProxy->SetMesh(this, mesh, materials, transform->GetWorldTransform());
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
        }
        else
        {
            materials.Clear();
        }

        //maybe test if object is active?
        if (renderProxy)
        {
            renderProxy->SetMesh(this, mesh, materials, transform->GetWorldTransform());
        }
    }

    void RenderComponent::OnDestroy()
    {
        if (renderProxy)
        {
            renderProxy->RemoveMesh(this);
        }
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
