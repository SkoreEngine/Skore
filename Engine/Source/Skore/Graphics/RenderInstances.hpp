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
        u32 instanceIndex{U32_MAX};
    };

    struct RenderMesh
    {
        VoidPtr               pointer;
        Array<RenderDrawCall> drawCalls;
    };

    struct InstanceStorage
    {
        DrawIndexedIndirectArguments drawIndexedIndirectArguments;
        u32  materialIndex;
        u32  vertexOffset;
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
        u32    maxInstanceCount = 0;
        //frames in flight?

//        Array<u64> freeItems;

        HashMap<VoidPtr, RenderMesh> meshes;
        Array<InstanceStorage>       storage;

        Array<DrawIndexedIndirectArguments> pendingIndirectDraws;
    private:
        void CreateBuffers(u32 size);
        void Resize();

        void RemoveDrawCall(const RenderDrawCall& drawCall);

    };
}
