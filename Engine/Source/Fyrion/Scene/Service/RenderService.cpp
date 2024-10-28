#include "RenderService.hpp"

#include "Fyrion/Scene/Scene.hpp"

namespace Fyrion
{
    void RenderService::OnStart()
    {
        meshRenders.Reserve(scene->GetObjectCount());
    }

    void RenderService::SetMesh(VoidPtr pointer, MeshAsset* mesh, Span<MaterialAsset*> materials, const Mat4& matrix)
    {
        if (mesh == nullptr)
        {
            RemoveMesh(pointer);
            return;
        }

        auto it = meshRendersLookup.Find(pointer);
        if (it == meshRendersLookup.end())
        {
            it = meshRendersLookup.Emplace(pointer, meshRenders.Size()).first;
            meshRenders.EmplaceBack();
        }

        meshRenders[it->second].pointer = pointer;
        meshRenders[it->second].mesh = mesh;
        meshRenders[it->second].materials = materials;
        meshRenders[it->second].matrix = matrix;
    }

    void RenderService::RemoveMesh(VoidPtr pointer)
    {
        if (auto it = meshRendersLookup.Find(pointer); it != meshRendersLookup.end())
        {
            if (!meshRenders.Empty())
            {
                MeshRenderData& back = meshRenders.Back();
                meshRendersLookup[back.pointer] = it->second;
                meshRenders[it->second] = Traits::Move(back);
                meshRenders.PopBack();
            }
            meshRendersLookup.Erase(it);
        }
    }


    Span<MeshRenderData> RenderService::GetMeshesToRender()
    {
        return meshRenders;
    }

    void RenderService::AddLight(VoidPtr address, const LightProperties& directionalLight)
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

    void RenderService::RemoveLight(VoidPtr address)
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

    Span<LightRenderData> RenderService::GetLights()
    {
        return lights;
    }

    Optional<LightProperties> RenderService::GetDirectionalShadowCaster()
    {
        if (directionalShadowCaster != U32_MAX)
        {
            return std::make_optional(lights[directionalShadowCaster].properties);
        }
        return {};
    }

    void RenderService::RegisterType(NativeTypeHandler<RenderService>& type)
    {

    }
}
