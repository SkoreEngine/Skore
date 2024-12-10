#pragma once
#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/Optional.hpp"
#include "Skore/Core/SharedPtr.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/UUID.hpp"
#include "Skore/Graphics/GraphicsTypes.hpp"
#include "Skore/Graphics/RenderUtils.hpp"
#include "Skore/Scene/Proxy.hpp"

namespace Skore
{
    class TextureAsset;
    class MaterialAsset;
    class MeshAsset;

    struct MeshRenderData
    {
        VoidPtr    pointer;
        Mat4       matrix;
        Mat4       prevMatrix;
        MeshAsset* mesh = nullptr;
        Array<u32> materials{};
    };

    // maybe make it abstract and implement it in the RenderPipeline??
    class SK_API RenderProxy final : public Proxy
    {
    public:
        SK_BASE_TYPES(Proxy);

        RenderProxy();
        ~RenderProxy() override;

        void                 SetMesh(VoidPtr pointer, MeshAsset* mesh, Span<MaterialAsset*> materials, const Mat4& matrix);
        void                 RemoveMesh(VoidPtr pointer);
        Span<MeshRenderData> GetMeshesToRender();

        void                      AddLight(VoidPtr address, const LightProperties& directionalLight);
        void                      RemoveLight(VoidPtr address);
        Span<LightRenderData>     GetLights();
        Optional<LightProperties> GetDirectionalShadowCaster();


        void          SetPanoramaSky(TextureAsset* panoramaSky);
        TextureAsset* GetPanoramaSky() const;

        Texture GetDiffuseIrradiance();
        Texture GetSpecularMap();
        Texture GetSkyCubeMap();

        void              AddCamera(VoidPtr pointer, const CameraData& camera);
        void              RemoveCamera(VoidPtr pointer);
        const CameraData* GetCamera() const;

        static void RegisterType(NativeTypeHandler<RenderProxy>& type);

        Optional<Texture> cubemapTest;

        DescriptorSet bindlessResources;
        DescriptorSet materialDescriptor;
    private:
        Array<MeshRenderData>   meshRenders;
        HashMap<VoidPtr, usize> meshRendersLookup;

        Array<LightRenderData>  lights;
        HashMap<VoidPtr, usize> lightsLookup;
        u32                     directionalShadowCaster = U32_MAX;

        TextureAsset*              panoramaSky = nullptr;
        SpecularMapGenerator       specularMapGenerator;
        DiffuseIrradianceGenerator diffuseIrradianceGenerator;
        EquirectangularToCubemap   toCubemap;

        HashMap<UUID, u32> materials;
        u32 currentBindlessIndex = 1;

        Buffer  materialStorageBuffer;
        u32     currentMaterialCount = 0;

        struct CameraStorage
        {
            VoidPtr    ptr;
            CameraData data;
        };

        Optional<CameraStorage> cameraData;

        Sampler materialSampler;

        u32 FindOrCreateMaterialInstance(const MaterialAsset* materialAsset);
    };
}
