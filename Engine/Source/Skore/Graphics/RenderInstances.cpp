#include "RenderInstances.hpp"

#include "Graphics.hpp"
#include "RenderGlobals.hpp"
#include "Assets/MeshAsset.hpp"
#include "Skore/Core/Chronometer.hpp"
#include "Skore/Core/Logger.hpp"

namespace Skore
{
    struct InstanceGPUData
    {
        u32 materialIndex;
        u32 vertexOffset;
        u32 _pad0;
        u32 _pad1;
    };

    void RenderInstances::Init(u64 initSize)
    {
        CreateBuffers(initSize);
        maxInstanceCount = initSize;
        pendingIndirectDraws.Reserve(initSize);

        stagingBuffer = Graphics::CreateBuffer({
            .usage = BufferUsage::TransferSrc | BufferUsage::TransferDst,
            .size = sizeof(DrawIndexedIndirectArguments) * 1000,
            .allocation = BufferAllocation::TransferToCPU
        });
    }

    void RenderInstances::Destroy() const
    {
        Graphics::DestroyBuffer(instanceBuffer);
        Graphics::DestroyBuffer(transformBuffer);
        Graphics::DestroyBuffer(prevTransformBuffer);
        Graphics::DestroyBuffer(allDrawCommands);
        Graphics::DestroyBuffer(stagingBuffer);
    }

    void RenderInstances::Flush(RenderCommands& cmd)
    {
        if (!pendingIndirectDraws.Empty())
        {
            Chronometer c;
            u32 processed = 0;
            while (processed < pendingIndirectDraws.Size())
            {
                u32 toProcess = Math::Min(static_cast<u32>(pendingIndirectDraws.Size()) - processed, 1000u);
                memcpy(Graphics::GetBufferMappedMemory(stagingBuffer), pendingIndirectDraws.begin() + processed, sizeof(DrawIndexedIndirectArguments) * toProcess);

                BufferCopyInfo bufferCopyInfo{
                    .srcOffset = 0,
                    .dstOffset = pendingIndirectDraws[processed].startInstanceLocation * sizeof(DrawIndexedIndirectArguments),
                    .size = sizeof(DrawIndexedIndirectArguments) * toProcess
                };

                RenderCommands& tempCmd = Graphics::GetCmd();
                tempCmd.Begin();
                tempCmd.CopyBuffer(stagingBuffer, allDrawCommands, &bufferCopyInfo);
                tempCmd.SubmitAndWait(Graphics::GetMainQueue());

                processed += toProcess;
            }
            pendingIndirectDraws.Clear();
            c.Print();
        }
    }

    void RenderInstances::AddMesh(VoidPtr pointer, MeshAsset* mesh, Span<MaterialAsset*> materials, const Mat4& initialTransform)
    {
        if (auto it = meshes.Find(pointer))
        {
            return;
        }

        if (mesh->primitives.Size() != materials.Size())
        {
            return;
        }

        if (MeshLookupData* meshLookupData = RenderGlobals::FindOrCreateMeshLookupData(mesh))
        {
            RenderMeshStorage meshStorage{};

            for (u32 i = 0; i < mesh->primitives.Size(); ++i)
            {
                MeshPrimitive& primitive = mesh->primitives[i];
                MaterialAsset* materialAsset = materials[i];

                if (drawCalls.Size() >= maxInstanceCount)
                {
                    Resize();
                }

                u32 drawIndex = drawCalls.Size();
                meshStorage.drawcalls.EmplaceBack(drawIndex);

                RenderDrawCall drawCall = RenderDrawCall{
                    .owner = pointer,
                    .drawIndex = drawIndex,
                    .transform = initialTransform,
                    .materialAsset = materialAsset,
                    .indexCount = primitive.indexCount,
                    .firstIndex = +static_cast<u32>(meshLookupData->indexBufferOffset / sizeof(u32)) + primitive.firstIndex,
                    .vertexOffset = meshLookupData->vertexBufferOffset,
                    .materialIndex = RenderGlobals::FindOrCreateMaterialInstance(materialAsset)
                };

                drawCalls.EmplaceBack(drawCall);

                InstanceGPUData* gpuData = Graphics::GetBufferMappedMemory<InstanceGPUData>(instanceBuffer, drawCall.drawIndex);
                gpuData->materialIndex = RenderGlobals::FindOrCreateMaterialInstance(materials[i]);
                gpuData->vertexOffset = meshLookupData->vertexBufferOffset;

                *Graphics::GetBufferMappedMemory<Mat4>(transformBuffer, drawCall.drawIndex) = initialTransform;


                DrawIndexedIndirectArguments drawIndexedIndirectArguments = DrawIndexedIndirectArguments{
                    .indexCountPerInstance = primitive.indexCount,
                    .instanceCount = 1,
                    .startIndexLocation = drawCall.firstIndex,
                    .startInstanceLocation = drawCall.drawIndex,
                };


               // *Graphics::GetBufferMappedMemory<DrawIndexedIndirectArguments>(allDrawCommands, drawCall.drawIndex) = drawIndexedIndirectArguments;

                pendingIndirectDraws.EmplaceBack(drawIndexedIndirectArguments);
            }
            meshes.Insert(pointer, meshStorage);
        }
    }


    void RenderInstances::RemoveMesh(VoidPtr pointer)
    {
        auto it = meshes.Find(pointer);
        if (it != meshes.end())
        {
            for (u32 drawCall : it->second.drawcalls)
            {



                //RemoveDrawCall(drawCall);
            }
            meshes.Erase(it);
        }
    }

    void RenderInstances::UpdateTransform(VoidPtr pointer, const Mat4& transform)
    {
        auto it = meshes.Find(pointer);
        if (it != meshes.end())
        {
            for (u32 drawCall : it->second.drawcalls)
            {
                *Graphics::GetBufferMappedMemory<Mat4>(transformBuffer, drawCalls[drawCall].drawIndex) = transform;
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

    // void RenderInstances::RemoveDrawCall(const RenderDrawCall& drawCall)
    // {
    //     if (instanceCount - 1 != drawCall.instanceIndex)
    //     {
    //         //copy data
    //         *Graphics::GetBufferMappedMemory<InstanceGPUData>(instanceBuffer, drawCall.instanceIndex) =
    //             *Graphics::GetBufferMappedMemory<InstanceGPUData>(instanceBuffer, instanceCount - 1);
    //
    //         //copy matrix
    //         *Graphics::GetBufferMappedMemory<Mat4>(transformBuffer, drawCall.instanceIndex) =
    //             *Graphics::GetBufferMappedMemory<Mat4>(transformBuffer, instanceCount - 1);
    //
    //         BufferCopyInfo bufferCopyInfo{
    //             .srcOffset = (instanceCount - 1) * sizeof(DrawIndexedIndirectArguments),
    //             .dstOffset = drawCall.instanceIndex * sizeof(DrawIndexedIndirectArguments),
    //             .size = sizeof(DrawIndexedIndirectArguments)
    //         };
    //
    //         //last drawCall.instanceIndex needs to receive drawCall.instanceIndex
    //
    //         //instanceCount - 1
    //
    //         RenderCommands& tempCmd = Graphics::GetCmd();
    //         tempCmd.Begin();
    //         tempCmd.CopyBuffer(allDrawCommands, allDrawCommands, &bufferCopyInfo);
    //         tempCmd.SubmitAndWait(Graphics::GetMainQueue());
    //     }
    //
    //     //delete last one
    //     freeItems.EmplaceBack(instanceCount - 1);
    //     instanceCount--;
    // }

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
            .allocation = BufferAllocation::TransferToCPU
        });

        drawCalls.Reserve(size);
    }
}
