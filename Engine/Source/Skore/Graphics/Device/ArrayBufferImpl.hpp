#pragma once

#include "vk_mem_alloc.h"
#include "Skore/Graphics/GraphicsTypes.hpp"

namespace Skore
{
    class SK_API ArrayBufferImpl final : public ArrayBuffer
    {
    public:
        ArrayBufferImpl(const ArrayBufferCreation& arrayBufferCreation)
        {
            VmaVirtualBlockCreateInfo blockCreateInfo = {};
            blockCreateInfo.size = arrayBufferCreation.initialSize;
            vmaCreateVirtualBlock(&blockCreateInfo, &currentBlock);
        }

        ArrayBufferInfo Create(u64 size, VoidPtr userData) override
        {
            VmaVirtualAllocationCreateInfo allocCreateInfo = {};
            allocCreateInfo.size = size;

            VmaVirtualAllocation alloc;
            VkDeviceSize         offset;
            if (vmaVirtualAllocate(currentBlock, &allocCreateInfo, &alloc, &offset) != VK_SUCCESS)
            {
                //Reallocate
            }
            return {alloc, offset};
        }


        void Set(u64 offset, VoidPtr data, u64 size) override
        {
            //TODO
        }

        Buffer GetGPUBuffer() override
        {
            return currentBuffer;
        }

        u64 Reserve(u64 size) override
        {
            return 0;
        }

        VoidPtr GetMappedMemory(u64 offset) override
        {
            return static_cast<u8*>(Graphics::GetBufferMappedMemory(currentBuffer)) + offset;
        }

        ~ArrayBufferImpl() override
        {
            vmaDestroyVirtualBlock(currentBlock);
        }

    private:
        Buffer          currentBuffer;
        VmaVirtualBlock currentBlock;
    };
}
