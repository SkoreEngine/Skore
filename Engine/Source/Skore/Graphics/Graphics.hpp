#pragma once

#include "Assets/ShaderAsset.hpp"
#include "Skore/Core/Image.hpp"
#include "Skore/Graphics/GraphicsTypes.hpp"

namespace Skore::Graphics
{
    SK_API Span<Adapter>   GetAdapters();
    SK_API Swapchain       CreateSwapchain(const SwapchainCreation& swapchainCreation);
    SK_API RenderPass      CreateRenderPass(const RenderPassCreation& renderPassCreation);
    SK_API Buffer          CreateBuffer(const BufferCreation& bufferCreation);
    SK_API ArrayBuffer*    CreateArrayBuffer(const ArrayBufferCreation& arrayBufferCreation);
    SK_API Texture         CreateTexture(const TextureCreation& textureCreation);
    SK_API Texture         CreateTextureFromImage(const Image& image);
    SK_API TextureView     CreateTextureView(const TextureViewCreation& textureViewCreation);
    SK_API Sampler         CreateSampler(const SamplerCreation& samplerCreation);
    SK_API PipelineState   CreateGraphicsPipelineState(const GraphicsPipelineCreation& graphicsPipelineCreation);
    SK_API PipelineState   CreateComputePipelineState(const ComputePipelineCreation& computePipelineCreation);
    SK_API BindingSet*     CreateBindingSet(ShaderState* shaderState);
    SK_API DescriptorSet   CreateDescriptorSet(const DescriptorSetCreation& descriptorSetCreation);
    SK_API void            WriteDescriptorSet(DescriptorSet descriptorSet, Span<DescriptorSetWriteInfo> bindings);
    SK_API void            DestroySwapchain(const Swapchain& swapchain);
    SK_API void            DestroyRenderPass(const RenderPass& renderPass);
    SK_API void            DestroyBuffer(const Buffer& buffer);
    SK_API void            DestroyArrayBuffer(ArrayBuffer* arrayBuffer);
    SK_API void            DestroyTexture(const Texture& texture);
    SK_API void            DestroyTextureView(const TextureView& textureView);
    SK_API void            DestroySampler(const Sampler& sampler);
    SK_API void            DestroyGraphicsPipelineState(const PipelineState& pipelineState);
    SK_API void            DestroyComputePipelineState(const PipelineState& pipelineState);
    SK_API void            DestroyBindingSet(BindingSet* bindingSet);
    SK_API void            DestroyDescriptorSet(DescriptorSet descriptorSet);
    SK_API RenderPass      AcquireNextRenderPass(Swapchain swapchain);
    SK_API void            WaitQueue();
    SK_API void            UpdateBufferData(const BufferDataInfo& bufferDataInfo);
    SK_API void            UpdateTextureData(const TextureDataInfo& textureDataInfo);
    SK_API void            UpdateTextureLayout(Texture texture, ResourceLayout oldLayout, ResourceLayout newLayout);
    SK_API VoidPtr         GetBufferMappedMemory(const Buffer& buffer);
    SK_API void            GetTextureData(const TextureGetDataInfo& info, Array<u8>& data);
    SK_API TextureCreation GetTextureCreationInfo(Texture texture);
    SK_API RenderCommands& GetCmd();
    SK_API GPUQueue        GetMainQueue();
    SK_API RenderApiType   GetRenderApi();
    SK_API Sampler         GetLinearSampler();
    SK_API Sampler         GetNearestSampler();
    SK_API Texture         GetDefaultTexture();
    SK_API void            AddTask(GraphicsTaskType graphicsTask, VoidPtr userData, FnGraphicsTask task);

    template <typename T>
    T* GetBufferMappedMemory(Buffer buffer, u32 index)
    {
        return reinterpret_cast<T*>(static_cast<u8*>(GetBufferMappedMemory(buffer)) + index * sizeof(T));
    }

}
