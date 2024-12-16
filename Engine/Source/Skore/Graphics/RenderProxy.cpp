#include "RenderProxy.hpp"

#include "RenderGlobals.hpp"
#include "Assets/MaterialAsset.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/Assets/TextureAsset.hpp"

namespace Skore
{
    RenderProxy::RenderProxy()
    {
        toCubemap.Init({512, 512}, Format::RGBA16F);
        diffuseIrradianceGenerator.Init({64, 64});
        specularMapGenerator.Init({128, 128}, 6);

        instances.Init(5000);
    }

    RenderProxy::~RenderProxy()
    {
        Graphics::WaitQueue();
        instances.Destroy();
        specularMapGenerator.Destroy();
        diffuseIrradianceGenerator.Destroy();
        toCubemap.Destroy();
    }

    RenderInstances& RenderProxy::GetInstances()
    {
        return instances;
    }

    void RenderProxy::AddLight(VoidPtr address, const LightProperties& directionalLight)
    {
        auto it = lightsLookup.Find(address);
        if (it == lightsLookup.end())
        {
            it = lightsLookup.Emplace(address, lights.Size()).first;
            lights.EmplaceBack();
        }

        lights[it->second] = LightRenderData{address, directionalLight};


        if (directionalLight.type == LightType::Directional && directionalLight.castShadows && directionalShadowCaster == U32_MAX)
        {
            directionalShadowCaster = it->second;
        }
    }

    void RenderProxy::RemoveLight(VoidPtr address)
    {
        if (auto it = lightsLookup.Find(address); it != lightsLookup.end())
        {
            if (!lights.Empty())
            {
                LightRenderData& back = lights.Back();
                lightsLookup[back.pointer] = it->second;
                lights[it->second] = Traits::Move(back);
                lights.PopBack();
            }

            lightsLookup.Erase(it);

            //replace directional shadow caster
            if (it->second == directionalShadowCaster)
            {
                directionalShadowCaster = U32_MAX;
                for (int i = 0; i < lights.Size(); ++i)
                {
                    if (lights[i].properties.type == LightType::Directional && lights[i].properties.castShadows)
                    {
                        directionalShadowCaster = i;
                        break;
                    }
                }
            }
        }
    }

    Span<LightRenderData> RenderProxy::GetLights()
    {
        return lights;
    }

    Optional<LightProperties> RenderProxy::GetDirectionalShadowCaster()
    {
        if (directionalShadowCaster != U32_MAX)
        {
            return std::make_optional(lights[directionalShadowCaster].properties);
        }
        return {};
    }

    void RenderProxy::SetPanoramaSky(TextureAsset* panoramaSky)
    {
        if (panoramaSky != nullptr && this->panoramaSky != panoramaSky)
        {
            Texture texture = panoramaSky->GetTexture();

            RenderCommands& cmd = Graphics::GetCmd();
            cmd.Begin();
            toCubemap.Convert(cmd, texture);
            cmd.SubmitAndWait(Graphics::GetMainQueue());

            Texture cubemap = toCubemap.GetTexture();

            cmd.Begin();
            diffuseIrradianceGenerator.Generate(cmd, cubemap);
            specularMapGenerator.Generate(cmd, cubemap);
            cmd.SubmitAndWait(Graphics::GetMainQueue());

            //toCubemap.Destroy();
        }

        this->panoramaSky = panoramaSky;
    }

    TextureAsset* RenderProxy::GetPanoramaSky() const
    {
        return panoramaSky;
    }

    Texture RenderProxy::GetDiffuseIrradiance()
    {
        return diffuseIrradianceGenerator.GetTexture();
    }

    Texture RenderProxy::GetSpecularMap()
    {
        return specularMapGenerator.GetTexture();
    }

    Texture RenderProxy::GetSkyCubeMap()
    {
        return toCubemap.GetTexture();
    }

    void RenderProxy::AddCamera(VoidPtr pointer, const CameraData& camera)
    {
        cameraData = MakeOptional<CameraStorage>(CameraStorage{pointer, camera});
    }

    void RenderProxy::RemoveCamera(VoidPtr pointer)
    {
        if (cameraData && cameraData->ptr == pointer)
        {
            cameraData = {};
        }
    }

    const CameraData* RenderProxy::GetCamera() const
    {
        if (cameraData)
        {
            return &cameraData->data;
        }
        return nullptr;
    }


    void RenderProxy::RegisterType(NativeTypeHandler<RenderProxy>& type)
    {

    }
}
