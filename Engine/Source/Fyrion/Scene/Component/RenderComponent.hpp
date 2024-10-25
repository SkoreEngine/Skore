#pragma once
#include "Component.hpp"
#include "TransformComponent.hpp"
#include "Fyrion/Core/Array.hpp"
#include "Fyrion/Graphics/Assets/MaterialAsset.hpp"
#include "Fyrion/Scene/Service/RenderService.hpp"


namespace Fyrion
{
    class MeshAsset;

    class FY_API RenderComponent : public Component
    {
    public:
        FY_BASE_TYPES(Component);

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
        RenderService*      renderService = nullptr;
        RID                 meshRID;
    };
}
