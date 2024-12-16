#include "RenderInstances.hpp"

#include "Graphics.hpp"
#include "RenderGlobals.hpp"
#include "Assets/MeshAsset.hpp"
#include "Skore/Core/Chronometer.hpp"
#include "Skore/Core/Logger.hpp"

namespace Skore
{
    void RenderInstances::Init(u64 initSize)
    {
        CreateBuffers(initSize);

        pendingIndirectDraws.Reserve(initSize);
        updatedIndirectDrawsCopyInfo.Reserve(initSize);

        freeItems.Resize(initSize);
        for (int i = 0; i < initSize; ++i)
        {
            freeItems[i] = initSize - i - 1;
        }
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
        //
        // if (!updatedIndirectDrawsCopyInfo.Empty())
        // {
        //     RenderCommands& tempCmd = Graphics::GetCmd();
        //     tempCmd.Begin();
        //     for (BufferCopyInfo& bufferCopyInfo : updatedIndirectDrawsCopyInfo)
        //     {
        //         tempCmd.CopyBuffer(allDrawCommands, allDrawCommands, &bufferCopyInfo);
        //     }
        //     tempCmd.SubmitAndWait(Graphics::GetMainQueue());
        //
        //     updatedIndirectDrawsCopyInfo.Clear();
        // }
    }

    void RenderInstances::AddMesh(VoidPtr pointer, MeshAsset* mesh, Span<MaterialAsset*> materials, const Mat4& initialTransform)
    {
        auto it = meshes.Find(pointer);
        if (it == meshes.end())
        {
            it = meshes.Emplace(pointer, RenderMesh{}).first;
        }

        MeshLookupData* meshLookupData = RenderGlobals::GetMeshLookupData(mesh);

        RenderMesh& instance = it->second;
        instance.drawCalls.Reserve(materials.Size());
        u32 size = Math::Max(instance.drawCalls.Size(), materials.Size());

        for (int i = 0; i < size; ++i)
        {
            if (i >= instance.drawCalls.Size())
            {
                instance.drawCalls.EmplaceBack();
            }
            else if (i >= materials.Size())
            {
                RemoveDrawCall(*(instance.drawCalls.begin() + i));
                instance.drawCalls.Erase(instance.drawCalls.begin() + i);
                continue;
            }

            if (i >= mesh->primitives.Size())
            {
                //primitives == materials, so it should not happen
                RemoveDrawCall(*(instance.drawCalls.begin() + i));
                instance.drawCalls.Erase(instance.drawCalls.begin() + i);
                continue;
            }

            RenderDrawCall& drawCall = instance.drawCalls[i];
            MeshPrimitive& primitive = mesh->primitives[i];

            if (drawCall.instanceIndex == U32_MAX)
            {
                if (freeItems.Empty())
                {
                    Resize();
                }

                instanceCount++;

                drawCall.instanceIndex = freeItems.Back();
                freeItems.PopBack();

                // DrawIndexedIndirectArguments& drawIndexedIndirectArguments = *Graphics::GetBufferMappedMemory<DrawIndexedIndirectArguments>(allDrawCommands, drawCall.instanceIndex);
                // drawIndexedIndirectArguments.indexCountPerInstance = primitive.indexCount;
                // drawIndexedIndirectArguments.instanceCount = 1;
                // drawIndexedIndirectArguments.startIndexLocation = primitive.firstIndex + static_cast<u32>(meshLookupData->indexBufferOffset / sizeof(u32));
                // drawIndexedIndirectArguments.startInstanceLocation = drawCall.instanceIndex;

                pendingIndirectDraws.EmplaceBack(DrawIndexedIndirectArguments{
                    .indexCountPerInstance = primitive.indexCount,
                    .instanceCount = 1,
                    .startIndexLocation = primitive.firstIndex + static_cast<u32>(meshLookupData->indexBufferOffset / sizeof(u32)),
                    .startInstanceLocation = drawCall.instanceIndex,
                });
            }

            InstanceGPUData* gpuData = Graphics::GetBufferMappedMemory<InstanceGPUData>(instanceBuffer, drawCall.instanceIndex);
            gpuData->materialIndex = RenderGlobals::FindOrCreateMaterialInstance(materials[i]);
            gpuData->vertexOffset = meshLookupData->vertexBufferOffset;
            gpuData->instanceIndex = drawCall.instanceIndex;

            *Graphics::GetBufferMappedMemory<Mat4>(transformBuffer, drawCall.instanceIndex) = initialTransform;
        }

        instance.drawCalls.ShrinkToFit();
    }


    void RenderInstances::RemoveMesh(VoidPtr pointer)
    {
        auto it = meshes.Find(pointer);
        if (it != meshes.end())
        {
            for (RenderDrawCall& drawCall : it->second.drawCalls)
            {
                RemoveDrawCall(drawCall);
            }
            meshes.Erase(it);
        }
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

    void RenderInstances::Resize()
    {
        Buffer oldInstanceBuffer = instanceBuffer;
        Buffer oldTransformBuffer = transformBuffer;
        Buffer oldPrevTransformBuffer = prevTransformBuffer;
        Buffer oldAllDrawCommands = allDrawCommands;

        u32 newSize = (maxInstanceCount * 3) / 2;
        CreateBuffers(newSize);

        u32 diff = newSize - maxInstanceCount;
        freeItems.Resize(diff);
        for (int i = 0; i < diff; ++i)
        {
            freeItems[i] = newSize - i - 1;
        }
        RenderCommands& tempCmd = Graphics::GetCmd();
        tempCmd.Begin();

        BufferCopyInfo bufferCopyInfo{
            .srcOffset = 0,
            .dstOffset = 0,
        };

        bufferCopyInfo.size = maxInstanceCount * sizeof(InstanceGPUData);
        tempCmd.CopyBuffer(oldInstanceBuffer, instanceBuffer, &bufferCopyInfo);

        bufferCopyInfo.size = maxInstanceCount * sizeof(Mat4);
        tempCmd.CopyBuffer(oldTransformBuffer, transformBuffer, &bufferCopyInfo);
        tempCmd.CopyBuffer(oldPrevTransformBuffer, prevTransformBuffer, &bufferCopyInfo);

        bufferCopyInfo.size = maxInstanceCount * sizeof(DrawIndexedIndirectArguments);
        tempCmd.CopyBuffer(oldAllDrawCommands, allDrawCommands, &bufferCopyInfo);

        tempCmd.SubmitAndWait(Graphics::GetMainQueue());

        Graphics::DestroyBuffer(oldInstanceBuffer);
        Graphics::DestroyBuffer(oldTransformBuffer);
        Graphics::DestroyBuffer(oldPrevTransformBuffer);
        Graphics::DestroyBuffer(oldAllDrawCommands);

        maxInstanceCount = newSize;
    }

    void RenderInstances::RemoveDrawCall(const RenderDrawCall& drawCall)
    {
        //copy data
        *Graphics::GetBufferMappedMemory<InstanceGPUData>(instanceBuffer, drawCall.instanceIndex) =
            *Graphics::GetBufferMappedMemory<InstanceGPUData>(instanceBuffer, instanceCount - 1);

        //copy matrix
        *Graphics::GetBufferMappedMemory<Mat4>(transformBuffer, drawCall.instanceIndex) =
            *Graphics::GetBufferMappedMemory<Mat4>(transformBuffer, instanceCount - 1);
        //
        // //copy DrawIndexedIndirectArguments
        // *Graphics::GetBufferMappedMemory<DrawIndexedIndirectArguments>(allDrawCommands, drawCall.instanceIndex) =
        //     *Graphics::GetBufferMappedMemory<DrawIndexedIndirectArguments>(allDrawCommands, instanceCount - 1);

        //delete last one
        freeItems.EmplaceBack(instanceCount - 1);
        instanceCount--;
    }

    void RenderInstances::CreateBuffers(u32 size)
    {
        instanceBuffer = Graphics::CreateBuffer({
            .usage = BufferUsage::StorageBuffer | BufferUsage::TransferSrc | BufferUsage::TransferDst,
            .size = sizeof(InstanceGPUData) * size,
            .allocation = BufferAllocation::TransferToCPU
        });

        transformBuffer = Graphics::CreateBuffer({
            .usage = BufferUsage::StorageBuffer | BufferUsage::TransferSrc | BufferUsage::TransferDst,
            .size = sizeof(Mat4) * size,
            .allocation = BufferAllocation::TransferToCPU
        });

        prevTransformBuffer = Graphics::CreateBuffer({
            .usage = BufferUsage::StorageBuffer | BufferUsage::TransferSrc | BufferUsage::TransferDst,
            .size = sizeof(Mat4) * size,
            .allocation = BufferAllocation::GPUOnly
        });

        allDrawCommands = Graphics::CreateBuffer({
            .usage = BufferUsage::StorageBuffer | BufferUsage::TransferSrc | BufferUsage::IndirectBuffer | BufferUsage::TransferDst,
            .size = size * sizeof(DrawIndexedIndirectArguments),
            .allocation = BufferAllocation::GPUOnly
        });
    }
}
