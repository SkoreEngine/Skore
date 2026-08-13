/*
 * test_render_device.c — CPU-mock render device integration test (APX-103).
 *
 * Exercises the sk_render_device_api_t registered by the sk-test-render-device
 * plugin through the app registry without any GPU backend:
 *   init → create buffer → map/update round-trip → texture barrier → destroy.
 *
 * The engine auto-loads sk-render-device (stub), sk-vulkan-render-device (real
 * backend), and sk-test-render-device (CPU mock) from the plugins folder; all
 * register under SK_RENDER_DEVICE_API_TYPE_ID, so which one wins is
 * filesystem-order dependent. Re-entering the test-render-device plugin
 * guarantees the CPU mock is registered last (set_api replaces), mirroring the
 * reload pattern in vulkan_device.c.
 *
 * Unlike the Vulkan integration tests this never skips: the mock device always
 * inits, so the RHI path is verified even on machines with no Vulkan ICD.
 */

#include "app.h"
#include "filesystem.h"
#include "path.h"
#include "render_device.h"
#include "test.h"

#include <string.h>

#ifdef SK_TESTS

static i32 integration_plugin_path(const_chr_t plugin_filename, char* out, u32 out_cap) {
	const sk_filesystem_api_t* fs = sk_test_filesystem_table();
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
 * Reload the CPU-mock plugin so it registers sk_render_device_api_t last and
 * wins SK_RENDER_DEVICE_API_TYPE_ID regardless of auto-load order.
 */
static const sk_render_device_api_t* integration_render_device_api(sk_app_context_t* ctx, const sk_app_api_t* app_api) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t plugin_name = "sk-test-render-device.dll";
#elif defined(__APPLE__)
	const_chr_t plugin_name = "sk-test-render-device.dylib";
#else
	const_chr_t plugin_name = "sk-test-render-device.so";
#endif
	if (integration_plugin_path(plugin_name, path, (u32)sizeof(path)) == 0) {
		app_api->load_plugin(ctx, path);
	}
	return (const sk_render_device_api_t*)app_api->get_api(ctx, SK_RENDER_DEVICE_API_TYPE_ID);
}

SK_TEST(test_render_device_create_buffer_and_map) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL_MESSAGE(ctx, "app bootstrap must succeed");
	if (ctx == NULL) {
		return;
	}

	const sk_render_device_api_t* api = integration_render_device_api(ctx, boot.api);
	TEST_ASSERT_NOT_NULL_MESSAGE(api, "test_render_device plugin must register sk_render_device_api_t");
	if (api == NULL) {
		sk_app_shutdown(ctx);
		return;
	}

	sk_render_device_t dev = api->init(ctx, NULL);
	TEST_ASSERT_TRUE_MESSAGE(sk_render_device_t_is_valid(dev), "CPU mock device must init without a Vulkan ICD");
	if (sk_render_device_t_is_valid(dev)) {
		sk_device_properties_t props = api->get_properties(dev);
		TEST_ASSERT_EQUAL_STRING("TestRenderDevice", props.device_name);
		TEST_ASSERT_EQUAL_INT(SK_GRAPHICS_API_NONE, (int)api->get_api(dev));

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

			sk_command_buffer_t cmd = api->create_command_buffer(dev, NULL);
			TEST_ASSERT_TRUE(sk_command_buffer_t_is_valid(cmd));
			if (sk_command_buffer_t_is_valid(cmd)) {
				TEST_ASSERT_EQUAL_INT(0, api->begin_command_buffer(dev, cmd, NULL));
				u32 values[2] = {7u, 42u};
				api->update_buffer(dev, cmd, buf, 0u, sizeof(values), values);
				api->end_command_buffer(dev, cmd);

				u32* mapped = (u32*)api->buffer_map(dev, buf);
				TEST_ASSERT_NOT_NULL(mapped);
				if (mapped != NULL) {
					TEST_ASSERT_EQUAL_UINT32(7u, mapped[0]);
					TEST_ASSERT_EQUAL_UINT32(42u, mapped[1]);
				}
				api->buffer_unmap(dev, buf);
				api->destroy_command_buffer(dev, cmd);
			}
			api->destroy_buffer(dev, buf);
		}
		api->destroy(dev);
	}
	sk_app_shutdown(ctx);
}

SK_TEST(test_render_device_texture_barrier) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL_MESSAGE(ctx, "app bootstrap must succeed");
	if (ctx == NULL) {
		return;
	}

	const sk_render_device_api_t* api = integration_render_device_api(ctx, boot.api);
	TEST_ASSERT_NOT_NULL_MESSAGE(api, "test_render_device plugin must register sk_render_device_api_t");
	if (api == NULL) {
		sk_app_shutdown(ctx);
		return;
	}

	sk_render_device_t dev = api->init(ctx, NULL);
	TEST_ASSERT_TRUE_MESSAGE(sk_render_device_t_is_valid(dev), "CPU mock device must init without a Vulkan ICD");
	if (sk_render_device_t_is_valid(dev)) {
		sk_texture_desc_t desc;
		memset(&desc, 0, sizeof(desc));
		desc.extent.width = 32u;
		desc.extent.height = 32u;
		desc.extent.depth = 1u;
		desc.mip_levels = 3u;
		desc.array_layers = 1u;
		desc.format = SK_PIXEL_FORMAT_RGBA8_UNORM;

		sk_texture_t tex = api->create_texture(dev, &desc);
		TEST_ASSERT_TRUE(sk_texture_t_is_valid(tex));
		if (sk_texture_t_is_valid(tex)) {
			sk_command_buffer_t cmd = api->create_command_buffer(dev, NULL);
			TEST_ASSERT_TRUE(sk_command_buffer_t_is_valid(cmd));
			if (sk_command_buffer_t_is_valid(cmd)) {
				TEST_ASSERT_EQUAL_INT(0, api->begin_command_buffer(dev, cmd, NULL));
				api->resource_barrier_texture(dev, cmd, tex, SK_RESOURCE_STATE_UNDEFINED, SK_RESOURCE_STATE_SHADER_READ, 0u, UINT32_MAX, 0u, UINT32_MAX, SK_BARRIER_SYNC_AUTOMATIC,
											  SK_BARRIER_SYNC_AUTOMATIC);
				api->end_command_buffer(dev, cmd);
				api->destroy_command_buffer(dev, cmd);
			}
			api->destroy_texture(dev, tex);
		}
		api->destroy(dev);
	}
	sk_app_shutdown(ctx);
}

#endif /* SK_TESTS */
