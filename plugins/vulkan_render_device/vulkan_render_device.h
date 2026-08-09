#pragma once

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Type id for sk_vulkan_render_device_api_t in the app registry. */
#define SK_VULKAN_RENDER_DEVICE_API_TYPE_ID SK_TYPE_ID("sk.vulkan_render_device_api", 0xd715e779ce3920b1ULL, 0xa6cd7c937d5c2887ULL)

/*
 * Global module API table for the Vulkan render device backend.
 *
 * Scaffold surface (APX-49): establishes plugin registration plus the
 * volk / VMA / Vulkan-Headers build wiring. The full sk_render_device_api_t
 * implementation lands with the Vulkan device (APX-50).
 */
typedef struct sk_vulkan_render_device_api_t {
	/**
	 * Initialize the Vulkan loader (volkInitialize).
	 * @return 0 on success, non-zero (VK result) on failure.
	 */
	i32 (*init)(void);

	/**
	 * Shut the Vulkan loader down (volkFinalize).
	 */
	void (*shutdown)(void);

	/**
	 * Compiled volk header version.
	 * @return VOLK_HEADER_VERSION.
	 */
	u32 (*volk_version)(void);

	/**
	 * Size of the VMA allocator handle type (compile-time wiring proof).
	 * @return sizeof(VmaAllocator).
	 */
	u32 (*vma_allocator_size)(void);
} sk_vulkan_render_device_api_t;

#ifdef __cplusplus
}
#endif
