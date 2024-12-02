#pragma once
#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/SharedPtr.hpp"
#include "Skore/Graphics/GraphicsTypes.hpp"

#include "volk.h"
#include "VulkanTypes.hpp"
#include "Skore/Core/FixedArray.hpp"

namespace Skore
{
    struct ShaderState;
    struct VulkanBindingVar;
    struct VulkanCommands;
    struct VulkanBindingSet;
    class VulkanDevice;


    struct VulkanDescriptorSetData
    {
        u8                    frame{};
        VkDescriptorSetLayout descriptorSetLayout{};
        VkDescriptorSet       descriptorSet{};
        bool                  dirty = true;
    };

    struct VulkanDescriptorSet
    {
        u32               set;
        VulkanDevice&     vulkanDevice;
        VulkanBindingSet& bindingSet;

        FixedArray<u8, SK_FRAMES_IN_FLIGHT> frames{0, 0};
        Array<VulkanDescriptorSetData>      data;

        Array<VulkanBindingVar*>      bindingVars{};
        Array<VkWriteDescriptorSet>   descriptorWrites;

        void MarkDirty();
        void CheckDescriptorSetData();
    };

    struct VulkanBindingVarBuffer
    {
        VulkanBuffer buffer;
        u8           frame;
    };

    struct VulkanUpdateDescriptorArray
    {
        Texture texture;
        usize   index;
    };

    struct VulkanBindingVar : BindingVar
    {
        VulkanBindingSet& bindingSet;

        VulkanDescriptorSet*          descriptorSet{};
        u32                           binding{};
        DescriptorType                descriptorType{};
        RenderType                    renderType{};
        u32                           size{};
        u32                           count{};
        u32                           descriptorArrayOffset = 0;
        Array<VkDescriptorImageInfo>  descriptorImageInfos = {};
        Array<VkDescriptorBufferInfo> descriptorBufferInfos = {};

        VulkanBindingVar(VulkanBindingSet& bindingSet) : bindingSet(bindingSet) {}
        ~VulkanBindingVar() override;

        VulkanSampler*               sampler{};
        VulkanBuffer*                buffer{}; //external buffers, BindingSet don't own it
        Array<VulkanTexture*>        vulkanTextures;
        Array<VulkanTextureView*>    vulkanTextureViews;

        Array<VulkanUpdateDescriptorArray> pendingTextures{};


        FixedArray<u8, SK_FRAMES_IN_FLIGHT> bufferFrames{0, 0};
        Array<VulkanBindingVarBuffer>       valueBuffer{}; //internal buffers created for "SetValue"

        void SetTexture(const Texture& texture) override;
        void SetTextureArray(Span<Texture> textureArray) override;
        void SetTextureAt(const Texture& texture, usize index) override;
        void SetTextureViewArray(Span<TextureView> textureViews) override;
        void SetTextureView(const TextureView& textureView) override;
        void SetSampler(const Sampler& sampler) override;
        void SetBuffer(const Buffer& buffer) override;
        void SetValue(ConstPtr ptr, usize size) override;

        void MarkDirty();
    };

    struct VulkanBindingSet : BindingSet
    {
        VulkanDevice&          vulkanDevice;
        ShaderState*           shaderState = nullptr;
        Span<DescriptorLayout> descriptorLayouts;

        //shader reflection data
        HashMap<String, u32>           valueDescriptorSetLookup{};
        HashMap<u32, DescriptorLayout> descriptorLayoutLookup{};

        //binding set values
        HashMap<String, VulkanBindingVar*> bindingVars;

        //runtime vulkan data
        HashMap<u32, SharedPtr<VulkanDescriptorSet>> descriptorSets{};

        VulkanBindingSet(ShaderState* shaderState, VulkanDevice& vulkanDevice);
        VulkanBindingSet(Span<DescriptorLayout> descriptorLayouts, VulkanDevice& vulkanDevice);
        ~VulkanBindingSet() override;

        BindingVar* GetVar(const StringView& name) override;
        void        Reload() override;
        void        RemoveShaderDependency() override;
        void        LoadInfo();

        void Bind(VulkanCommands& cmd, const PipelineState& pipeline);
    };
}
