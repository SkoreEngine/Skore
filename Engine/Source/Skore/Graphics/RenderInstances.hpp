#pragma once
#include "GraphicsTypes.hpp"
#include "Skore/Core/HashMap.hpp"

namespace Skore
{

    struct InstanceGPUData
    {
        u32  materialIndex;
        u32  vertexOffset;
        u32  _pad0;
        u32  _pad1;
    };

    struct RenderDrawCall
    {
        u64 instanceIndex{U64_MAX};
    };

    struct RenderMesh
    {
        VoidPtr               pointer;
        Array<RenderDrawCall> drawCalls;
    };

    class RenderInstances
    {
    public:

        void Init(u64 initSize);
        void Destroy() const;
        void Flush(RenderCommands& cmd);

        void AddMesh(VoidPtr pointer, MeshAsset* mesh, Span<MaterialAsset*> materials, const Mat4& initialTransform);
        void RemoveMesh(VoidPtr pointer);
        void UpdateTransform(VoidPtr pointer, const Mat4& transform);

        Buffer instanceBuffer{};
        Buffer transformBuffer{};
        Buffer prevTransformBuffer{};
        Buffer allDrawCommands{};
        u64    allDrawsOffset{};
        u32    instanceCount{};
        u32    maxInstanceCount = 0;
        //frames in flight?

        Array<u64> freeItems;
        u32 count = 0;

        HashMap<VoidPtr, RenderMesh> meshes;

        Array<DrawIndexedIndirectArguments> pendingIndirectDraws;

    };
}
