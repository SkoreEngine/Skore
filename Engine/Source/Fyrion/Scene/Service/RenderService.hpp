#pragma once
#include "Service.hpp"
#include "Fyrion/Core/HashMap.hpp"
#include "Fyrion/Core/Math.hpp"
#include "Fyrion/Core/Optional.hpp"
#include "Fyrion/Core/Span.hpp"
#include "Fyrion/Graphics/GraphicsTypes.hpp"

namespace Fyrion
{
    class MaterialAsset;
    class MeshAsset;

    // maybe make it abstract and implement it in the RenderPipeline??
    class FY_API RenderService final : public Service
    {
    public:
        FY_BASE_TYPES(Service);

        void OnStart() override;

        void                 SetMesh(VoidPtr pointer, MeshAsset* mesh, Span<MaterialAsset*> materials, const Mat4& matrix);
        void                 RemoveMesh(VoidPtr pointer);
        Span<MeshRenderData> GetMeshesToRender();

        void              AddDirectionalLight(VoidPtr address, const DirectionalLight& directionalLight);
        void              RemoveDirectionalLight(VoidPtr address);
        DirectionalLight* GetDirectionalLight();

        static void RegisterType(NativeTypeHandler<RenderService>& type);

    private:
        Array<MeshRenderData>   meshRenders;
        HashMap<VoidPtr, usize> meshRendersLookup;

        Optional<DirectionalLight> directionalLight;
    };
}