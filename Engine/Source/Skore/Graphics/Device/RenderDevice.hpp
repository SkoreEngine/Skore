#pragma once

#include "Skore/Graphics/GraphicsTypes.hpp"

namespace Skore
{
    struct RenderDevice
    {
        virtual Span<Adapter>   GetAdapters() = 0;
        virtual void            CreateDevice(Adapter adapter) = 0;
        virtual Swapchain       CreateSwapchain(const SwapchainCreation& swapchainCreation) = 0;
        virtual RenderPass      CreateRenderPass(const RenderPassCreation& renderPassCreation) = 0;
        virtual Buffer          CreateBuffer(const BufferCreation& bufferCreation) = 0;
        virtual Texture         CreateTexture(const TextureCreation& textureCreation) = 0;
        virtual TextureView     CreateTextureView(const TextureViewCreation& textureViewCreation) = 0;
        virtual Sampler         CreateSampler(const SamplerCreation& samplerCreation) = 0;
        virtual PipelineState   CreateGraphicsPipelineState(const GraphicsPipelineCreation& graphicsPipelineCreation) = 0;
        virtual PipelineState   CreateComputePipelineState(const ComputePipelineCreation& computePipelineCreation) = 0;
        virtual BindingSet*     CreateBindingSet(ShaderState* shaderState) = 0;
        virtual DescriptorSet   CreateDescriptorSet(const DescriptorSetCreation& descriptorSetCreation) = 0;
        virtual void            WriteDescriptorSet(DescriptorSet descriptorSet, Span<DescriptorSetWriteInfo> bindings) = 0;
        virtual void            DestroySwapchain(const Swapchain& swapchain) = 0;
        virtual void            DestroyRenderPass(const RenderPass& renderPass) = 0;
        virtual void            DestroyBuffer(const Buffer& buffer) = 0;
        virtual void            DestroyTexture(const Texture& texture) = 0;
        virtual void            DestroyTextureView(const TextureView& textureView) = 0;
        virtual void            DestroySampler(const Sampler& sampler) = 0;
        virtual void            DestroyGraphicsPipelineState(const PipelineState& pipelineState) = 0;
        virtual void            DestroyComputePipelineState(const PipelineState& pipelineState) = 0;
        virtual void            DestroyBindingSet(BindingSet* bindingSet) = 0;
        virtual void            DestroyDescriptorSet(DescriptorSet descriptorSet) = 0;
        virtual RenderCommands& BeginFrame() = 0;
        virtual RenderPass      AcquireNextRenderPass(Swapchain swapchain) = 0;
        virtual void            EndFrame(Swapchain swapchain) = 0;
        virtual void            WaitQueue() = 0;
        virtual GPUQueue        GetMainQueue() = 0;
        virtual RenderCommands& GetTempCmd() = 0;
        virtual void            UpdateBufferData(const BufferDataInfo& bufferDataInfo) = 0;
        virtual VoidPtr         GetBufferMappedMemory(const Buffer& buffer) = 0;
        virtual TextureCreation GetTextureCreationInfo(Texture texture) = 0;

        virtual void    ImGuiInit(Swapchain renderSwapchain) = 0;
        virtual void    ImGuiNewFrame() = 0;
        virtual void    ImGuiRender(RenderCommands& renderCommands) = 0;
        virtual VoidPtr GetImGuiTexture(const Texture& texture) = 0;

        virtual ~RenderDevice() {}
    };
}
