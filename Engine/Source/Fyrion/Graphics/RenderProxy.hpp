#pragma once
#include "Fyrion/Core/HashMap.hpp"
#include "Fyrion/Core/Math.hpp"
#include "Fyrion/Core/Optional.hpp"
#include "Fyrion/Core/Span.hpp"
#include "Fyrion/Graphics/GraphicsTypes.hpp"
#include "Fyrion/Graphics/RenderUtils.hpp"
#include "Fyrion/Scene/Proxy.hpp"

namespace Fyrion
{
    class TextureAsset;
}

namespace Fyrion
{
    class MaterialAsset;
    class MeshAsset;

    // maybe make it abstract and implement it in the RenderPipeline??
    class FY_API RenderProxy final : public Proxy
    {
    public:
        FY_BASE_TYPES(Proxy);

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

        void              AddCamera(VoidPtr pointer, const CameraData& camera);
        void              RemoveCamera(VoidPtr pointer);
        const CameraData* GetCamera() const;

        static void RegisterType(NativeTypeHandler<RenderProxy>& type);

    private:
        Array<MeshRenderData>   meshRenders;
        HashMap<VoidPtr, usize> meshRendersLookup;

        Array<LightRenderData>  lights;
        HashMap<VoidPtr, usize> lightsLookup;
        u32                     directionalShadowCaster = U32_MAX;

        TextureAsset*              panoramaSky = nullptr;
        SpecularMapGenerator       specularMapGenerator;
        DiffuseIrradianceGenerator diffuseIrradianceGenerator;


        struct CameraStorage
        {
            VoidPtr    ptr;
            CameraData data;
        };

        Optional<CameraStorage> cameraData;
    };
}