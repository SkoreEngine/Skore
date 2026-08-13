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
 * The engine auto-loads both sk-render-device (stub) and
 * sk-vulkan-render-device (real backend) from the plugins folder; both register
 * under SK_RENDER_DEVICE_API_TYPE_ID, so which one wins is filesystem-order
 * dependent. Re-entering the vulkan plugin guarantees the real backend is
 * registered last (set_api replaces).
 */
static const sk_render_device_api_t* integration_render_device_api(sk_app_context_t* ctx, const sk_app_api_t* app_api) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t plugin_name = "sk-vulkan-render-device.dll";
#elif defined(__APPLE__)
	const_chr_t plugin_name = "sk-vulkan-render-device.dylib";
#else
	const_chr_t plugin_name = "sk-vulkan-render-device.so";
#endif
	if (integration_plugin_path(plugin_name, path, (u32)sizeof(path)) == 0) {
		app_api->load_plugin(ctx, path);
	}
	return (const sk_render_device_api_t*)app_api->get_api(ctx, SK_RENDER_DEVICE_API_TYPE_ID);
}

SK_TEST(vulkan_device_create_destroy) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL_MESSAGE(ctx, "app bootstrap must succeed");
	if (ctx == NULL) {
		return;
	}

	const sk_render_device_api_t* api = integration_render_device_api(ctx, boot.api);
	TEST_ASSERT_NOT_NULL_MESSAGE(api, "vulkan_render_device plugin must register sk_render_device_api_t");
	if (api == NULL) {
		sk_app_shutdown(ctx);
		return;
	}

	sk_render_device_t dev = api->init(ctx, NULL);
	if (!sk_render_device_t_is_valid(dev)) {
		TEST_IGNORE_MESSAGE("no Vulkan ICD available; skipping device integration test");
		sk_app_shutdown(ctx);
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
		sk_app_shutdown(ctx);
		return;
	}

	TEST_ASSERT_EQUAL_INT(0, api->select_adapter(dev, best));
	TEST_ASSERT_EQUAL_INT((int)SK_GRAPHICS_API_VULKAN, (int)api->get_api(dev));

	api->destroy(dev);
	sk_app_shutdown(ctx);
}

SK_TEST(vulkan_device_buffer_lifecycle) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL_MESSAGE(ctx, "app bootstrap must succeed");
	if (ctx == NULL) {
		return;
	}

	const sk_render_device_api_t* api = integration_render_device_api(ctx, boot.api);
	TEST_ASSERT_NOT_NULL_MESSAGE(api, "vulkan_render_device plugin must register sk_render_device_api_t");
	if (api == NULL) {
		sk_app_shutdown(ctx);
		return;
	}

	sk_render_device_t dev = api->init(ctx, NULL);
	if (!sk_render_device_t_is_valid(dev)) {
		TEST_IGNORE_MESSAGE("no Vulkan ICD available; skipping buffer integration test");
		sk_app_shutdown(ctx);
		return;
	}

	u32 adapter_count = api->get_adapter_count(dev);
	if (adapter_count == 0u) {
		api->destroy(dev);
		sk_app_shutdown(ctx);
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
		sk_app_shutdown(ctx);
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
	sk_app_shutdown(ctx);
}

SK_TEST(vulkan_device_update_buffer_above_64k) {
	enum { WORD_COUNT = 20000u };
	enum { BYTE_COUNT = WORD_COUNT * (u32)sizeof(u32) };
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	const sk_render_device_api_t* api;
	sk_render_device_t dev;
	sk_adapter_t best = sk_adapter_t_zero();
	u32 best_score = 0u;
	u32 i;
	sk_buffer_desc_t desc;
	sk_buffer_t gpu_buf;
	sk_buffer_t readback;
	sk_queue_t queue;
	sk_command_buffer_t cmd;
	sk_command_buffer_begin_info_t begin_info;
	sk_queue_desc_t q_desc;
	sk_command_buffer_desc_t cb_desc;
	static u32 src[WORD_COUNT];
	const u32* got;

	TEST_ASSERT_TRUE(BYTE_COUNT > 65536u);
	TEST_ASSERT_NOT_NULL_MESSAGE(ctx, "app bootstrap must succeed");
	if (ctx == NULL) {
		return;
	}

	api = integration_render_device_api(ctx, boot.api);
	TEST_ASSERT_NOT_NULL_MESSAGE(api, "vulkan_render_device plugin must register sk_render_device_api_t");
	if (api == NULL) {
		sk_app_shutdown(ctx);
		return;
	}

	dev = api->init(ctx, NULL);
	if (!sk_render_device_t_is_valid(dev)) {
		TEST_IGNORE_MESSAGE("no Vulkan ICD available; skipping update_buffer integration test");
		sk_app_shutdown(ctx);
		return;
	}

	for (i = 0u; i < api->get_adapter_count(dev); ++i) {
		sk_adapter_t candidate = api->get_adapter(dev, i);
		u32 score = api->get_adapter_score(dev, candidate);
		if (score > best_score) {
			best_score = score;
			best = candidate;
		}
	}
	if (!sk_adapter_t_is_valid(best) || api->select_adapter(dev, best) != 0) {
		api->destroy(dev);
		sk_app_shutdown(ctx);
		TEST_IGNORE_MESSAGE("no suitable Vulkan adapter; skipping update_buffer integration test");
		return;
	}

	for (i = 0u; i < WORD_COUNT; ++i) {
		src[i] = i * 3u + 1u;
	}

	memset(&desc, 0, sizeof(desc));
	desc.size = BYTE_COUNT;
	desc.usage_flags = (u32)SK_RESOURCE_USAGE_COPY_DEST | (u32)SK_RESOURCE_USAGE_COPY_SOURCE;
	desc.debug_name = "update-src-gpu";
	gpu_buf = api->create_buffer(dev, &desc);
	TEST_ASSERT_TRUE(sk_buffer_t_is_valid(gpu_buf));

	memset(&desc, 0, sizeof(desc));
	desc.size = BYTE_COUNT;
	desc.usage_flags = (u32)SK_RESOURCE_USAGE_COPY_DEST;
	desc.host_visible = true;
	desc.debug_name = "update-readback";
	readback = api->create_buffer(dev, &desc);
	TEST_ASSERT_TRUE(sk_buffer_t_is_valid(readback));

	memset(&q_desc, 0, sizeof(q_desc));
	q_desc.queue_type = (u32)SK_QUEUE_TYPE_GRAPHICS;
	queue = api->create_queue(dev, &q_desc);
	TEST_ASSERT_TRUE(sk_queue_t_is_valid(queue));

	memset(&cb_desc, 0, sizeof(cb_desc));
	cb_desc.level = SK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cb_desc.queue_type = (u32)SK_QUEUE_TYPE_GRAPHICS;
	cb_desc.debug_name = "update-cmd";
	cmd = api->create_command_buffer(dev, &cb_desc);
	TEST_ASSERT_TRUE(sk_command_buffer_t_is_valid(cmd));

	if (!sk_buffer_t_is_valid(gpu_buf) || !sk_buffer_t_is_valid(readback) || !sk_queue_t_is_valid(queue) || !sk_command_buffer_t_is_valid(cmd)) {
		if (sk_command_buffer_t_is_valid(cmd)) {
			api->destroy_command_buffer(dev, cmd);
		}
		if (sk_queue_t_is_valid(queue)) {
			api->destroy_queue(dev, queue);
		}
		if (sk_buffer_t_is_valid(readback)) {
			api->destroy_buffer(dev, readback);
		}
		if (sk_buffer_t_is_valid(gpu_buf)) {
			api->destroy_buffer(dev, gpu_buf);
		}
		api->destroy(dev);
		sk_app_shutdown(ctx);
		return;
	}

	memset(&begin_info, 0, sizeof(begin_info));
	begin_info.usage_flags = (u32)SK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT;
	TEST_ASSERT_EQUAL_INT(0, api->begin_command_buffer(dev, cmd, &begin_info));
	api->update_buffer(dev, cmd, gpu_buf, 0u, BYTE_COUNT, src);
	api->memory_barrier(dev, cmd);
	api->copy_buffer(dev, cmd, gpu_buf, readback, BYTE_COUNT, 0u, 0u);
	api->end_command_buffer(dev, cmd);
	TEST_ASSERT_EQUAL_INT(0, api->submit_and_wait(dev, queue, cmd));

	got = (const u32*)api->buffer_map(dev, readback);
	TEST_ASSERT_NOT_NULL(got);
	if (got != NULL) {
		TEST_ASSERT_EQUAL_UINT32(src[0], got[0]);
		TEST_ASSERT_EQUAL_UINT32(src[WORD_COUNT / 2u], got[WORD_COUNT / 2u]);
		TEST_ASSERT_EQUAL_UINT32(src[WORD_COUNT - 1u], got[WORD_COUNT - 1u]);
		for (i = 0u; i < WORD_COUNT; ++i) {
			if (got[i] != src[i]) {
				TEST_FAIL_MESSAGE("update_buffer payload mismatch after 64KiB split");
				break;
			}
		}
	}
	api->buffer_unmap(dev, readback);

	api->destroy_command_buffer(dev, cmd);
	api->destroy_queue(dev, queue);
	api->destroy_buffer(dev, readback);
	api->destroy_buffer(dev, gpu_buf);
	api->destroy(dev);
	sk_app_shutdown(ctx);
}

#endif /* SK_TESTS */
