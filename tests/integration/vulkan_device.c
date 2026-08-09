/*
 * vulkan_device.c — Vulkan device + buffer integration test (APX-61).
 *
 * Exercises the real sk_render_device_api_t registered by the
 * sk-vulkan-render-device plugin through the app registry:
 *   init → select adapter → create buffer → destroy buffer → destroy.
 *
 * Uses the same path the engine uses headlessly: sk_app_init auto-loads the
 * plugin DLLs from {app_folder}/plugins, then the registered render device API
 * drives a Vulkan instance/device (llvmpipe/lavapipe software rendering where
 * that is the only ICD). No shaders and no present/draw path are required.
 *
 * When no Vulkan loader/device is present (e.g. CI without a Vulkan ICD) the
 * test is skipped rather than failed.
 */

#include "app.h"
#include "filesystem.h"
#include "path.h"
#include "render_device.h"
#include "test.h"

#include <string.h>

#ifdef SK_TESTS

static i32 integration_plugin_path(const_chr_t plugin_filename, char* out, u32 out_cap) {
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	char base[SK_FS_PATH_MAX];
	char plugins[SK_FS_PATH_MAX];

	if (fs->app_folder(base, (u32)sizeof(base)) != 0 || base[0] == '\0') {
		if (fs->current_dir(base, (u32)sizeof(base)) != 0) {
			return -1;
		}
	}
	i32 n = sk_path_join(sk_str_view_cstr(base), sk_str_view_cstr("plugins"), plugins, (u32)sizeof(plugins));
	if (n < 0) {
		return -1;
	}
	n = sk_path_join(sk_str_view_cstr(plugins), sk_str_view_cstr(plugin_filename), out, out_cap);
	return (n < 0) ? -1 : 0;
}

/*
 * The engine auto-loads both sk-render-device (stub) and
 * sk-vulkan-render-device (real backend) from the plugins folder; both register
 * under SK_RENDER_DEVICE_API_TYPE_ID, so which one wins is filesystem-order
 * dependent. Re-entering the vulkan plugin guarantees the real backend is
 * registered last (set_api replaces).
 */
static const sk_render_device_api_t* integration_render_device_api(sk_app_context_t* ctx) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t plugin_name = "sk-vulkan-render-device.dll";
#elif defined(__APPLE__)
	const_chr_t plugin_name = "sk-vulkan-render-device.dylib";
#else
	const_chr_t plugin_name = "sk-vulkan-render-device.so";
#endif
	if (integration_plugin_path(plugin_name, path, (u32)sizeof(path)) == 0) {
		sk_app_api()->load_plugin(ctx, path);
	}
	return (const sk_render_device_api_t*)sk_app_api()->get_api(ctx, SK_RENDER_DEVICE_API_TYPE_ID);
}

SK_TEST(vulkan_device_create_destroy) {
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL_MESSAGE(ctx, "app bootstrap must succeed");
	if (ctx == NULL) {
		return;
	}

	const sk_render_device_api_t* api = integration_render_device_api(ctx);
	TEST_ASSERT_NOT_NULL_MESSAGE(api, "vulkan_render_device plugin must register sk_render_device_api_t");
	if (api == NULL) {
		sk_app_destroy(ctx);
		return;
	}

	sk_render_device_t dev = api->init(ctx, NULL);
	if (!sk_render_device_t_is_valid(dev)) {
		TEST_IGNORE_MESSAGE("no Vulkan ICD available; skipping device integration test");
		sk_app_destroy(ctx);
		return;
	}

	u32 adapter_count = api->get_adapter_count(dev);
	TEST_ASSERT_TRUE(adapter_count > 0u);

	/* Pick the highest-scored adapter (the engine's device-selection path). */
	sk_adapter_t best = sk_adapter_t_zero();
	u32 best_score = 0u;
	for (u32 i = 0u; i < adapter_count; ++i) {
		sk_adapter_t candidate = api->get_adapter(dev, i);
		u32 score = api->get_adapter_score(dev, candidate);
		if (score > best_score) {
			best_score = score;
			best = candidate;
		}
	}
	TEST_ASSERT_TRUE(sk_adapter_t_is_valid(best));
	if (!sk_adapter_t_is_valid(best)) {
		api->destroy(dev);
		sk_app_destroy(ctx);
		return;
	}

	TEST_ASSERT_EQUAL_INT(0, api->select_adapter(dev, best));
	TEST_ASSERT_EQUAL_INT((int)SK_GRAPHICS_API_VULKAN, (int)api->get_api(dev));

	api->destroy(dev);
	sk_app_destroy(ctx);
}

SK_TEST(vulkan_device_buffer_lifecycle) {
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL_MESSAGE(ctx, "app bootstrap must succeed");
	if (ctx == NULL) {
		return;
	}

	const sk_render_device_api_t* api = integration_render_device_api(ctx);
	TEST_ASSERT_NOT_NULL_MESSAGE(api, "vulkan_render_device plugin must register sk_render_device_api_t");
	if (api == NULL) {
		sk_app_destroy(ctx);
		return;
	}

	sk_render_device_t dev = api->init(ctx, NULL);
	if (!sk_render_device_t_is_valid(dev)) {
		TEST_IGNORE_MESSAGE("no Vulkan ICD available; skipping buffer integration test");
		sk_app_destroy(ctx);
		return;
	}

	u32 adapter_count = api->get_adapter_count(dev);
	if (adapter_count == 0u) {
		api->destroy(dev);
		sk_app_destroy(ctx);
		TEST_IGNORE_MESSAGE("no Vulkan adapters; skipping buffer integration test");
		return;
	}

	/* Highest-scored adapter, matching the engine's device-selection path. */
	sk_adapter_t best = sk_adapter_t_zero();
	u32 best_score = 0u;
	for (u32 i = 0u; i < adapter_count; ++i) {
		sk_adapter_t candidate = api->get_adapter(dev, i);
		u32 score = api->get_adapter_score(dev, candidate);
		if (score > best_score) {
			best_score = score;
			best = candidate;
		}
	}
	if (!sk_adapter_t_is_valid(best)) {
		api->destroy(dev);
		sk_app_destroy(ctx);
		TEST_IGNORE_MESSAGE("no suitable Vulkan adapter; skipping buffer integration test");
		return;
	}
	TEST_ASSERT_EQUAL_INT(0, api->select_adapter(dev, best));

	sk_buffer_desc_t desc;
	memset(&desc, 0, sizeof(desc));
	desc.size = 4096u;
	desc.usage_flags = (u32)SK_RESOURCE_USAGE_VERTEX_BUFFER | (u32)SK_RESOURCE_USAGE_COPY_DEST;
	desc.host_visible = true;
	desc.persistent_mapped = false;
	desc.debug_name = "integration-buffer";

	sk_buffer_t buf = api->create_buffer(dev, &desc);
	TEST_ASSERT_TRUE(sk_buffer_t_is_valid(buf));
	if (sk_buffer_t_is_valid(buf)) {
		sk_buffer_desc_t got = api->get_buffer_desc(dev, buf);
		TEST_ASSERT_EQUAL_UINT64(4096u, got.size);
		TEST_ASSERT_EQUAL_STRING("integration-buffer", got.debug_name);
		api->destroy_buffer(dev, buf);
	}

	api->destroy(dev);
	sk_app_destroy(ctx);
}

#endif /* SK_TESTS */
