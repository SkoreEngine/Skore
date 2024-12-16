#include "RenderInstances.hpp"

#include "Graphics.hpp"
#include "RenderGlobals.hpp"
#include "Assets/MeshAsset.hpp"
#include "Skore/Core/Logger.hpp"

namespace Skore
{
    void RenderInstances::Init(u64 initSize)
    {
        instanceBuffer = Graphics::CreateBuffer({
            .usage = BufferUsage::StorageBuffer,
            .size = sizeof(InstanceGPUData) * initSize,
            .allocation = BufferAllocation::TransferToCPU
        });

        transformBuffer = Graphics::CreateBuffer({
            .usage = BufferUsage::StorageBuffer | BufferUsage::TransferSrc,
            .size = sizeof(Mat4) * initSize,
            .allocation = BufferAllocation::TransferToCPU
        });

        prevTransformBuffer = Graphics::CreateBuffer({
            .usage = BufferUsage::StorageBuffer | BufferUsage::TransferDst,
            .size = sizeof(Mat4) * initSize,
            .allocation = BufferAllocation::GPUOnly
        });

        allDrawCommands = Graphics::CreateBuffer({
            .usage = BufferUsage::StorageBuffer | BufferUsage::IndirectBuffer,
            .size = initSize * sizeof(DrawIndexedIndirectArguments),
            .allocation = BufferAllocation::GPUOnly
        });

        pendingIndirectDraws.Reserve(initSize);

        instanceCount = 0;
        maxInstanceCount = initSize;
    }

    void RenderInstances::Destroy() const
    {
        Graphics::DestroyBuffer(instanceBuffer);
        Graphics::DestroyBuffer(transformBuffer);
        Graphics::DestroyBuffer(prevTransformBuffer);
        Graphics::DestroyBuffer(allDrawCommands);
    }

    void RenderInstances::Flush(RenderCommands& cmd)
    {
        if (!pendingIndirectDraws.Empty())
        {
            usize size = sizeof(DrawIndexedIndirectArguments) * pendingIndirectDraws.Size();

            Buffer scratchBuffer = Graphics::CreateBuffer({
                .usage = BufferUsage::TransferSrc,
                .size = size,
                .allocation = BufferAllocation::TransferToCPU
            });

            memcpy(Graphics::GetBufferMappedMemory(scratchBuffer), pendingIndirectDraws.begin(), size);

            BufferCopyInfo bufferCopyInfo{
                .srcOffset = 0,
                .dstOffset = pendingIndirectDraws[0].startInstanceLocation * sizeof(DrawIndexedIndirectArguments),
                .size = size
            };

            RenderCommands& tempCmd = Graphics::GetCmd();
            tempCmd.Begin();
            tempCmd.CopyBuffer(scratchBuffer, allDrawCommands, &bufferCopyInfo);
            tempCmd.SubmitAndWait(Graphics::GetMainQueue());

            Graphics::DestroyBuffer(scratchBuffer);

            pendingIndirectDraws.Clear();
        }
    }

    void RenderInstances::AddMesh(VoidPtr pointer, MeshAsset* mesh, Span<MaterialAsset*> materials, const Mat4& initialTransform)
    {
        auto it = meshes.Find(pointer);
        if (it == meshes.end())
        {
            if (instanceCount + materials.Size() >= maxInstanceCount)
            {
                //TODO - resize.
            }
            it = meshes.Emplace(pointer, RenderMesh{}).first;
        }

        MeshLookupData* meshLookupData = RenderGlobals::GetMeshLookupData(mesh);

        RenderMesh& instance = it->second;
        instance.drawCalls.Reserve(materials.Size());
        u32 size = Math::Max(instance.drawCalls.Size(), materials.Size());

        count += size;

        Logger::GetLogger("RenderInstance").Info("size {} ", count);

        for (int i = 0; i < size; ++i)
        {
            if (i >= instance.drawCalls.Size())
            {
                instance.drawCalls.EmplaceBack();
            }
            else if (i >= materials.Size())
            {
                instance.drawCalls.Erase(instance.drawCalls.begin() + i);
                //TODO - remove instance.
                continue;
            }

            if (i >= mesh->primitives.Size())
            {
                //primitives == materials, so it should not happen
                //TODO remove instance
                continue;
            }

            RenderDrawCall& drawCall = instance.drawCalls[i];
            MeshPrimitive& primitive = mesh->primitives[i];

            if (drawCall.instanceIndex == U64_MAX)
            {
                drawCall.instanceIndex = instanceCount++;
            }

            InstanceGPUData* gpuData = Graphics::GetBufferMappedMemory<InstanceGPUData>(instanceBuffer, drawCall.instanceIndex);
            gpuData->materialIndex = RenderGlobals::FindOrCreateMaterialInstance(materials[i]);
            gpuData->vertexOffset = meshLookupData->vertexBufferOffset;

            *Graphics::GetBufferMappedMemory<Mat4>(transformBuffer, drawCall.instanceIndex) = initialTransform;

            pendingIndirectDraws.EmplaceBack(DrawIndexedIndirectArguments{
                .indexCountPerInstance = primitive.indexCount,
                .instanceCount = 1,
                .startIndexLocation = primitive.firstIndex + static_cast<u32>(meshLookupData->indexBufferOffset / sizeof(u32)),
                .startInstanceLocation = static_cast<u32>(drawCall.instanceIndex),
            });
        }

        instance.drawCalls.ShrinkToFit();
    }


    void RenderInstances::RemoveMesh(VoidPtr pointer)
    {
        // if (auto it = meshRendersLookup.Find(pointer); it != meshRendersLookup.end())
        // {
        //     if (!meshRenders.Empty())
        //     {
        //         MeshRenderData& back = meshRenders.Back();
        //         meshRendersLookup[back.pointer] = it->second;
        //         meshRenders[it->second] = Traits::Move(back);
        //         meshRenders.PopBack();
        //     }
        //     meshRendersLookup.Erase(it);
        //
        //
        //     //TODO: need to free drawcall.index
        // }
    }

    void RenderInstances::UpdateTransform(VoidPtr pointer, const Mat4& transform)
    {
        auto it = meshes.Find(pointer);
        if (it != meshes.end())
        {
            for (RenderDrawCall& drawCall : it->second.drawCalls)
            {
                *Graphics::GetBufferMappedMemory<Mat4>(transformBuffer, drawCall.instanceIndex) = transform;
            }
        }
    }
}
