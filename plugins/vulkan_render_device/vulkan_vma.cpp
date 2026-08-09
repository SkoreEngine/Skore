/*
 * vulkan_vma.cpp — VMA implementation translation unit.
 *
 * VMA's vk_mem_alloc.h implementation block is C++ (uses <cstdint>, std::*),
 * so it is compiled in a dedicated C++ TU; every other plugin TU includes the
 * header for declarations/types only. The allocator is created with explicit
 * vkGetInstanceProcAddr / vkGetDeviceProcAddr (see select_adapter), matching
 * VMA_DYNAMIC_VULKAN_FUNCTIONS.
 */

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
