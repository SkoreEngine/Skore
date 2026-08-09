/*
 * vulkan_render_device.c — Vulkan render device backend (scaffold).
 *
 * APX-49: plugin registration and build wiring for the Vulkan backend.
 * volk (meta-loader) and VMA (memory allocator) are vendored in thirdparty/
 * and linked into this plugin; the real device implementation lands in APX-50.
 */

#include "vulkan_render_device.h"

#include "app.h"

/* Vulkan stack (headers are SYSTEM includes from thirdparty targets). */
#include "volk.h"
#include "vk_mem_alloc.h"

/* Registers the static API table (called from plugin_entry_point). */
void sk_vulkan_render_device_init(sk_app_context_t* context, const sk_app_api_t* app_api);

/* ------------------------------------------------------------------ */
/* Stub implementations (loader + allocator wiring proof)              */
/* ------------------------------------------------------------------ */

static i32 sk_vkrd_init(void) {
	return (i32)volkInitialize();
}

static void sk_vkrd_shutdown(void) {
	volkFinalize();
}

static u32 sk_vkrd_volk_version(void) {
	return (u32)VOLK_HEADER_VERSION;
}

static u32 sk_vkrd_vma_allocator_size(void) {
	return (u32)sizeof(VmaAllocator);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

static sk_vulkan_render_device_api_t vulkan_render_device_api = {
	.init = sk_vkrd_init,
	.shutdown = sk_vkrd_shutdown,
	.volk_version = sk_vkrd_volk_version,
	.vma_allocator_size = sk_vkrd_vma_allocator_size,
};

void sk_vulkan_render_device_init(sk_app_context_t* context, const sk_app_api_t* app_api) {
	app_api->set_api(context, SK_VULKAN_RENDER_DEVICE_API_TYPE_ID, &vulkan_render_device_api);
}
