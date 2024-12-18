#pragma once
#include "GraphicsTypes.hpp"
#include "Skore/Core/HashMap.hpp"

namespace Skore
{
    struct RenderDrawCall
    {
        VoidPtr        owner;
        u32            drawIndex;
        Mat4           transform;
        MaterialAsset* materialAsset;
        u32            indexCount;
        u32            firstIndex;
        u64            vertexOffset;
        u32            materialIndex;
    };

    struct RenderMeshStorage
    {
        Array<u32> drawcalls;
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
        Buffer stagingBuffer{};
        u32    maxInstanceCount = 0;

        Array<RenderDrawCall>               drawCalls;
        HashMap<VoidPtr, RenderMeshStorage> meshes;

        Array<DrawIndexedIndirectArguments> pendingIndirectDraws;

    private:
        void CreateBuffers(u32 size);
        void Resize();
        // void RemoveDrawCall(const RenderDrawCall& drawCall);
    };
}
