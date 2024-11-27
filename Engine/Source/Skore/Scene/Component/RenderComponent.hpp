#pragma once
#include "Component.hpp"
#include "TransformComponent.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Graphics/RenderProxy.hpp"
#include "Skore/Graphics/Assets/MaterialAsset.hpp"


namespace Skore
{
    class MeshAsset;

    class SK_API RenderComponent : public Component
    {
    public:
        SK_BASE_TYPES(Component);

        void OnStart() override;

        void SetMesh(MeshAsset* mesh);

        MeshAsset*           GetMesh() const;
        Span<MaterialAsset*> GetMaterials() const;
        void                 OnChange() override;
        void                 OnDestroy() override;

        void ProcessEvent(const SceneEventDesc& event) override;

        static void RegisterType(NativeTypeHandler<RenderComponent>& type);

    private:
        MeshAsset*            mesh = nullptr;
        Array<MaterialAsset*> materials = {};

        TransformComponent* transform = nullptr;
        RenderProxy*        renderProxy = nullptr;
    };
}
