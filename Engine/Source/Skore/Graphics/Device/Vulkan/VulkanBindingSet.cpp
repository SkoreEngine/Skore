#include "VulkanBindingSet.hpp"

#include "VulkanDevice.hpp"
#include "VulkanUtils.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/Assets/ShaderAsset.hpp"

namespace Skore
{
    VulkanBindingSet::VulkanBindingSet(ShaderState* shaderState, VulkanDevice& vulkanDevice) : vulkanDevice(vulkanDevice),
                                                                                               shaderState(shaderState)
    {
        shaderState->AddBindingSetDependency(this);
        LoadInfo();
    }

    void VulkanBindingSet::Reload()
    {
        for (auto& descriptorIt : descriptorSets)
        {
            for (auto& data : descriptorIt.second->data)
            {
                vkDestroyDescriptorSetLayout(vulkanDevice.device, data.descriptorSetLayout, nullptr);
                vkFreeDescriptorSets(vulkanDevice.device, vulkanDevice.descriptorPool, 1, &data.descriptorSet);
            }
        }

        descriptorSets.Clear();
        valueDescriptorSetLookup.Clear();
        descriptorLayoutLookup.Clear();

        LoadInfo();

        //it should recreate the bindingVars but keep the original values
        //note: never remove a bindingVar, even if it's not present anymore in the shader.
        auto oldBindingVar = Traits::Move(bindingVars);
        for (const auto& bindingVarIt : oldBindingVar)
        {
            VulkanBindingVar* oldVar = bindingVarIt.second;

            VulkanBindingVar* newVar = static_cast<VulkanBindingVar*>(GetVar(bindingVarIt.first));
            newVar->vulkanTextures = Traits::Move(oldVar->vulkanTextures);
            newVar->vulkanTextureViews = Traits::Move(oldVar->vulkanTextureViews);
            newVar->valueBuffer = Traits::Move(oldVar->valueBuffer);
            newVar->sampler = oldVar->sampler;
            newVar->buffer = oldVar->buffer;
        }
    }

    void VulkanBindingSet::RemoveShaderDependency()
    {
        shaderState = nullptr;
    }

    void VulkanBindingSet::LoadInfo()
    {
        if (shaderState)
        {
            descriptorLayouts = shaderState->shaderInfo.descriptors;
        }

        for (const DescriptorLayout& descriptorLayout : descriptorLayouts)
        {
            auto setIt = descriptorLayoutLookup.Find(descriptorLayout.set);

            if (setIt == descriptorLayoutLookup.end())
            {
                descriptorLayoutLookup.Insert(descriptorLayout.set, descriptorLayout);
            }

            for (const DescriptorBinding& binding : descriptorLayout.bindings)
            {
                if (auto it = valueDescriptorSetLookup.Find(binding.name); it == valueDescriptorSetLookup.end())
                {
                    valueDescriptorSetLookup.Emplace(String{binding.name}, (u32)descriptorLayout.set);
                }
            }
        }
    }

    VulkanBindingSet::~VulkanBindingSet()
    {
        if (shaderState)
        {
            shaderState->RemoveBindingSetDependency(this);
        }


        for (auto& bindingVar : bindingVars)
        {
            vulkanDevice.allocator.DestroyAndFree(bindingVar.second);
        }

        for (auto& descriptorIt : descriptorSets)
        {
            for (auto& data : descriptorIt.second->data)
            {
                vkDestroyDescriptorSetLayout(vulkanDevice.device, data.descriptorSetLayout, nullptr);
                vkFreeDescriptorSets(vulkanDevice.device, vulkanDevice.descriptorPool, 1, &data.descriptorSet);
            }
        }
    }

    void VulkanBindingSetDescriptor::MarkDirty()
    {
        for (auto& d : data)
        {
            d.dirty = true;
        }
    }

    void VulkanBindingSetDescriptor::CheckDescriptorSetData()
    {
        if (data.Empty() || data[frames[vulkanDevice.currentFrame]].frame != vulkanDevice.currentFrame)
        {
            DescriptorLayout& descriptorLayout = bindingSet.descriptorLayoutLookup[set];

            frames[vulkanDevice.currentFrame] = this->data.Size();
            VulkanDescriptorSetData& newData = this->data.EmplaceBack();
            newData.frame = vulkanDevice.currentFrame;

            bool hasRuntimeArray = false;
            Vulkan::CreateDescriptorSetLayout(vulkanDevice.device, descriptorLayout, &newData.descriptorSetLayout, &hasRuntimeArray);

            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = vulkanDevice.descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &newData.descriptorSetLayout;

            VkDescriptorSetVariableDescriptorCountAllocateInfoEXT countInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT};

            u32 maxBinding = MaxBindlessResources - 1;
            countInfo.descriptorSetCount = 1;
            countInfo.pDescriptorCounts = &maxBinding;

            if (hasRuntimeArray && vulkanDevice.deviceFeatures.bindlessSupported)
            {
                allocInfo.pNext = &countInfo;
            }

            VkResult result = vkAllocateDescriptorSets(vulkanDevice.device, &allocInfo, &newData.descriptorSet);

            if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL)
            {
                //TODO -- needs a pool of "descriptor pool"
                vulkanDevice.logger.Error("VK_ERROR_OUT_OF_POOL_MEMORY");
            }
            else if (result != VK_SUCCESS)
            {
                vulkanDevice.logger.Error("Error on vkAllocateDescriptorSets");
            }

            bindingVars.Resize(descriptorLayout.bindings.Size());

            for (int i = 0; i < descriptorLayout.bindings.Size(); ++i)
            {
                const DescriptorBinding& descriptorBinding = descriptorLayout.bindings[i];

                VulkanBindingVar* bindingVar = bindingSet.bindingVars.Emplace(descriptorBinding.name, vulkanDevice.allocator.Alloc<VulkanBindingVar>(bindingSet, descriptorBinding.name)).first->second;
                bindingVar->descriptorSet = this;
                bindingVar->binding = descriptorBinding.binding;
                bindingVar->descriptorType = descriptorBinding.descriptorType;
                bindingVar->renderType = descriptorBinding.renderType;
                bindingVar->size = descriptorBinding.size;
                bindingVar->count = descriptorBinding.count;
                bindingVar->descriptorBufferInfos.Resize(descriptorBinding.count);
                bindingVar->descriptorImageInfos.Resize(descriptorBinding.count);
                bindingVars[i] = bindingVar;
            }

            u32 total = 0;
            for (u32 b = 0; b < descriptorLayout.bindings.Size(); ++b)
            {
                bindingVars[b]->descriptorArrayOffset = total;
                total += descriptorLayout.bindings[b].count;
            }
            descriptorWrites.Resize(total);
        }
    }

    VulkanBindingVar::~VulkanBindingVar()
    {
        for (VulkanBindingVarBuffer& bindingVarBuffer : valueBuffer)
        {
            if (bindingVarBuffer.buffer.buffer && bindingVarBuffer.buffer.allocation)
            {
                vmaDestroyBuffer(bindingSet.vulkanDevice.vmaAllocator, bindingVarBuffer.buffer.buffer, bindingVarBuffer.buffer.allocation);
            }
        }
        valueBuffer.Clear();
        descriptorBufferInfos.Clear();
        descriptorImageInfos.Clear();
    }

    void VulkanBindingVar::SetTexture(const Texture& texture)
    {
        VulkanTexture* newTexture = static_cast<VulkanTexture*>(texture.handler);
        if (newTexture != nullptr)
        {
            if (vulkanTextures.Empty())
            {
                vulkanTextures.Resize(1);
            }

            if (vulkanTextures[0] == nullptr || vulkanTextures[0]->id != newTexture->id)
            {
                vulkanTextures[0] = newTexture;
                MarkDirty();
            }
        }
        else if (vulkanTextures[0] != nullptr)
        {
            vulkanTextures[0] = nullptr;
            MarkDirty();
        }
    }

    void VulkanBindingVar::SetTextureArray(Span<Texture> textureArray)
    {
        if (textureArray.Size() != vulkanTextures.Size())
        {
            vulkanTextures.Clear();
            vulkanTextures.Resize(textureArray.Size());

            for (int i = 0; i < textureArray.Size(); ++i)
            {
                vulkanTextures[i] = static_cast<VulkanTexture*>(textureArray[i].handler);
            }

            MarkDirty();
            return;
        }

        bool dirty = false;

        for (int i = 0; i < textureArray.Size(); ++i)
        {
            if (vulkanTextures[i] != textureArray[i].handler)
            {
                vulkanTextures[i] = static_cast<VulkanTexture*>(textureArray[i].handler);
                dirty = true;
            }
        }

        if (dirty)
        {
            MarkDirty();
        }
    }

    void VulkanBindingVar::SetTextureAt(const Texture& texture, usize index)
    {
        //not sure about the heap alloc here.
        // auto resource = MakeShared<DescriptorArrayResource>();
        // pendingTextures.EmplaceBack(VulkanUpdateDescriptorArray{
        //     .resource = resource,
        //     .writeDescriptor = {
        //         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        //         .dstBinding = binding,
        //         .dstArrayElement = static_cast<u32>(index),
        //         .descriptorCount = 1,
        //         .descriptorType = Vulkan::CastDescriptorType(descriptorType),
        //         .pImageInfo = &resource->imageInfo
        //     }
        // });

        pendingTextures.EmplaceBack(VulkanUpdateDescriptorArray{
            .texture = texture,
            .index = index,
        });

        MarkDirty();
    }

    void VulkanBindingVar::SetTextureViewArray(Span<TextureView> textureViews)
    {
        if (textureViews.Size() != vulkanTextureViews.Size())
        {
            vulkanTextureViews.Clear();
            vulkanTextureViews.Resize(textureViews.Size());

            for (int i = 0; i < textureViews.Size(); ++i)
            {
                vulkanTextureViews[i] = static_cast<VulkanTextureView*>(textureViews[i].handler);
            }

            MarkDirty();
            return;
        }

        bool dirty = false;

        for (int i = 0; i < textureViews.Size(); ++i)
        {
            if (vulkanTextureViews[i] != textureViews[i].handler)
            {
                vulkanTextureViews[i] = static_cast<VulkanTextureView*>(textureViews[i].handler);
                dirty = true;
            }
        }

        if (dirty)
        {
            MarkDirty();
        }
    }

    void VulkanBindingVar::SetTextureView(const TextureView& p_textureView)
    {
        if (vulkanTextureViews.Empty())
        {
            vulkanTextureViews.Resize(1);
        }

        if (vulkanTextureViews[0] != p_textureView.handler)
        {
            vulkanTextureViews[0] = static_cast<VulkanTextureView*>(p_textureView.handler);
            MarkDirty();
        }
    }

    void VulkanBindingVar::SetSampler(const Sampler& p_sampler)
    {
        if (sampler != p_sampler.handler)
        {
            sampler = static_cast<VulkanSampler*>(p_sampler.handler);
            MarkDirty();
        }
    }

    void VulkanBindingVar::SetBuffer(const Buffer& p_buffer)
    {
        if (buffer != p_buffer.handler)
        {
            buffer = static_cast<VulkanBuffer*>(p_buffer.handler);
            MarkDirty();
        }
    }

    void VulkanBindingVar::SetValue(ConstPtr ptr, usize size)
    {
        if (valueBuffer.Empty() || valueBuffer[bufferFrames[bindingSet.vulkanDevice.currentFrame]].frame != bindingSet.vulkanDevice.currentFrame)
        {
            VkBufferCreateInfo bufferInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bufferInfo.size = size;
            bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

            VmaAllocationCreateInfo vmaAllocInfo = {};
            vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

            bufferFrames[bindingSet.vulkanDevice.currentFrame] = valueBuffer.Size();
            VulkanBindingVarBuffer& bindingVarBuffer = valueBuffer.EmplaceBack();
            bindingVarBuffer.frame = bindingSet.vulkanDevice.currentFrame;

            VulkanBuffer& vulkanBuffer = bindingVarBuffer.buffer;
            vulkanBuffer.bufferCreation.size = size;

            vmaCreateBuffer(bindingSet.vulkanDevice.vmaAllocator,
                            &bufferInfo,
                            &vmaAllocInfo,
                            &vulkanBuffer.buffer,
                            &vulkanBuffer.allocation,
                            &vulkanBuffer.allocInfo);
        }

        VulkanBuffer& vulkanBuffer = valueBuffer[bufferFrames[bindingSet.vulkanDevice.currentFrame]].buffer;
        char*         memory = static_cast<char*>(vulkanBuffer.allocInfo.pMappedData);
        MemCopy(memory, ptr, size);
    }

    void VulkanBindingVar::MarkDirty()
    {
        if (descriptorSet)
        {
            descriptorSet->MarkDirty();
        }
    }

    BindingVar* VulkanBindingSet::GetVar(const StringView& name)
    {
        auto it = bindingVars.Find(name);
        if (it == bindingVars.end())
        {
            //find the attribute descriptor set number
            auto varDescriptorSetIt = valueDescriptorSetLookup.Find(name);
            if (varDescriptorSetIt == valueDescriptorSetLookup.end())
            {
                return bindingVars.Emplace(name, vulkanDevice.allocator.Alloc<VulkanBindingVar>(*this, name)).first->second;
            }

            u32 set = varDescriptorSetIt->second;

            auto descriptorSetIt = descriptorSets.Find(set);

            //if there is no VulkanDescriptorSet for the attribute, create one.
            if (descriptorSetIt == descriptorSets.end())
            {
                descriptorSetIt = descriptorSets.Emplace(set, MakeShared<VulkanBindingSetDescriptor>(set, vulkanDevice, *this)).first;
            }

            SharedPtr<VulkanBindingSetDescriptor> vulkanDescriptorSet = descriptorSetIt->second;
            vulkanDescriptorSet->CheckDescriptorSetData();
            it = bindingVars.Find(name);
            if (it != bindingVars.end())
            {
                return it->second;
            }
        }

        if (it->second->descriptorSet)
        {
            it->second->descriptorSet->CheckDescriptorSetData();
        }
        return it->second;
    }

    void VulkanBindingSet::Bind(VulkanCommands& cmd, const PipelineState& pipeline)
    {
        VulkanPipelineState* vulkanPipelineState = static_cast<VulkanPipelineState*>(pipeline.handler);

        for (auto& descriptorIt : descriptorSets)
        {
            VulkanBindingSetDescriptor*     descriptorSet = descriptorIt.second.Get();
            VulkanDescriptorSetData& data = descriptorSet->data[descriptorIt.second->frames[vulkanDevice.currentFrame]];
            if (data.dirty)
            {
                for (u32 b = 0; b < descriptorSet->bindingVars.Size(); ++b)
                {
                    VulkanBindingVar* vulkanBindingVar = descriptorSet->bindingVars[b];
                    for (u32 arrayElement = 0; arrayElement < vulkanBindingVar->count; ++arrayElement)
                    {
                        VkWriteDescriptorSet& writeDescriptorSet = descriptorSet->descriptorWrites[vulkanBindingVar->descriptorArrayOffset + arrayElement];
                        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        writeDescriptorSet.dstSet = data.descriptorSet;
                        writeDescriptorSet.descriptorCount = 1;
                        writeDescriptorSet.descriptorType = Vulkan::CastDescriptorType(vulkanBindingVar->descriptorType);
                        writeDescriptorSet.dstBinding = vulkanBindingVar->binding;
                        writeDescriptorSet.dstArrayElement = arrayElement;

                        switch (vulkanBindingVar->descriptorType)
                        {
                            case DescriptorType::SampledImage:
                            case DescriptorType::StorageImage:
                            {
                                bool depthFormat = false;

                                if (vulkanBindingVar->vulkanTextureViews.Size() > arrayElement && vulkanBindingVar->vulkanTextureViews[arrayElement]->imageView)
                                {
                                    vulkanBindingVar->descriptorImageInfos[arrayElement].imageView = vulkanBindingVar->vulkanTextureViews[arrayElement]->imageView;
                                }
                                else if (vulkanBindingVar->vulkanTextures.Size() > arrayElement  && vulkanBindingVar->vulkanTextures[arrayElement]->image)
                                {
                                    depthFormat = vulkanBindingVar->vulkanTextures[arrayElement]->creation.format == Format::Depth;
                                    vulkanBindingVar->descriptorImageInfos[arrayElement].imageView = static_cast<VulkanTextureView*>(vulkanBindingVar->vulkanTextures[arrayElement]->textureView.handler)->imageView;
                                }
                                else
                                {
                                    vulkanBindingVar->descriptorImageInfos[arrayElement].imageView = static_cast<VulkanTextureView*>(static_cast<VulkanTexture*>(Graphics::GetDefaultTexture().handler)->textureView.
                                        handler)->imageView;
                                }

                                vulkanBindingVar->descriptorImageInfos[arrayElement].imageLayout = depthFormat
                                                                                         ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                                                                         : vulkanBindingVar->descriptorType == DescriptorType::SampledImage
                                                                                         ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                                                         : VK_IMAGE_LAYOUT_GENERAL;

                                writeDescriptorSet.pImageInfo = &vulkanBindingVar->descriptorImageInfos[arrayElement];
                                break;
                            }
                            case DescriptorType::Sampler:
                            {
                                VkSampler sampler = nullptr;

                                if (vulkanBindingVar->sampler)
                                {
                                    sampler = vulkanBindingVar->sampler->sampler;
                                } else if (vulkanBindingVar->name == "nearestSampler")
                                {
                                    sampler = static_cast<VulkanSampler*>(Graphics::GetNearestSampler().handler)->sampler;
                                }
                                else
                                {
                                    sampler = static_cast<VulkanSampler*>(Graphics::GetLinearSampler().handler)->sampler;
                                }
                                vulkanBindingVar->descriptorImageInfos[arrayElement].sampler = sampler;
                                writeDescriptorSet.pImageInfo = &vulkanBindingVar->descriptorImageInfos[arrayElement];
                                break;
                            }
                            case DescriptorType::UniformBuffer:
                            case DescriptorType::StorageBuffer:

                                if (vulkanBindingVar->buffer)
                                {
                                    vulkanBindingVar->descriptorBufferInfos[arrayElement].offset = 0;
                                    vulkanBindingVar->descriptorBufferInfos[arrayElement].buffer = vulkanBindingVar->buffer->buffer;
                                    vulkanBindingVar->descriptorBufferInfos[arrayElement].range = vulkanBindingVar->buffer->bufferCreation.size;
                                }
                                else if (!vulkanBindingVar->valueBuffer.Empty())
                                {
                                    VulkanBuffer& vulkanBuffer = vulkanBindingVar->valueBuffer[vulkanBindingVar->bufferFrames[vulkanDevice.currentFrame]].buffer;
                                    vulkanBindingVar->descriptorBufferInfos[arrayElement].offset = 0;
                                    vulkanBindingVar->descriptorBufferInfos[arrayElement].buffer = vulkanBuffer.buffer;
                                    vulkanBindingVar->descriptorBufferInfos[arrayElement].range = vulkanBuffer.bufferCreation.size;
                                }
                                else
                                {
                                    //TODO make a default buffer?
                                }
                                writeDescriptorSet.pBufferInfo = &vulkanBindingVar->descriptorBufferInfos[arrayElement];
                                break;
                            case DescriptorType::AccelerationStructure:
                                break;
                        }
                    }
                }

                vkUpdateDescriptorSets(vulkanDevice.device, descriptorSet->descriptorWrites.Size(), descriptorSet->descriptorWrites.Data(), 0, nullptr);
                data.dirty = false;
            }

            vkCmdBindDescriptorSets(cmd.commandBuffer,
                                    vulkanPipelineState->bindingPoint,
                                    vulkanPipelineState->layout,
                                    descriptorIt.first,
                                    1,
                                    &data.descriptorSet,
                                    0,
                                    nullptr);
        }
    }
}
