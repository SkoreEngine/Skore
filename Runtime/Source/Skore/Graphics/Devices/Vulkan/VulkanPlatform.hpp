#pragma once

namespace Skore::Platform
{
	SK_API void                    SetVulkanLoader(PFN_vkGetInstanceProcAddr procAddr);
	SK_API Span<const char* const> GetRequiredInstanceExtensions();
	SK_API bool                    GetPhysicalDevicePresentationSupport(VkInstance instance, VkPhysicalDevice device, u32 queueFamily);
	SK_API bool                    CreateWindowSurface(Window window, VkInstance instance, VkSurfaceKHR* surface);
}
