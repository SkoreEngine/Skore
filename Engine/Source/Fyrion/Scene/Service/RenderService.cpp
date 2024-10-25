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

    void RenderService::RegisterType(NativeTypeHandler<RenderService>& type)
    {

    }
}
