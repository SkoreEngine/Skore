/**
 * @file editor_gpu.c
 * @brief Windowed sk-ui swapchain present (player host pipeline).
 */

#include "editor_gpu.h"

#include "allocator.h"
#include "dxc_compiler.h"
#include "render_device.h"

#include <stdint.h>
#include <string.h>

#define EDITOR_GPU_MAX_SWAP_IMAGES 8u

struct sk_editor_gpu_t {
	const sk_ui_api_t* ui;
	const sk_render_device_api_t* api;
	const sk_dxc_compiler_api_t* dxc;
	const sk_logger_api_t* logger_api;
	sk_logger_t* log;
	sk_render_device_t device;
	sk_queue_t queue;
	sk_swapchain_t swapchain;
	sk_render_pass_t render_pass;
	sk_texture_view_t views[EDITOR_GPU_MAX_SWAP_IMAGES];
	sk_framebuffer_t framebuffers[EDITOR_GPU_MAX_SWAP_IMAGES];
	u32 image_count;
	u32 fb_w;
	u32 fb_h;
	sk_pixel_format_t format;
	sk_ui_renderer_t* ui_renderer;
	sk_command_buffer_t cmd;
	sk_fence_t fence;
	sk_semaphore_t acquire_sem;
};

static sk_adapter_t editor_gpu_select_adapter(const sk_render_device_api_t* api, sk_render_device_t dev) {
	u32 adapter_count = api->get_adapter_count(dev);
	sk_adapter_t best = sk_adapter_t_zero();
	u32 best_score = 0u;
	u32 i;
	for (i = 0u; i < adapter_count; ++i) {
		sk_adapter_t candidate = api->get_adapter(dev, i);
		u32 score = api->get_adapter_score(dev, candidate);
		if (score > best_score) {
			best_score = score;
			best = candidate;
		}
	}
	return best;
}

static void editor_gpu_destroy_frame_targets(sk_editor_gpu_t* g) {
	u32 i;
	if (g->api == NULL || !sk_render_device_t_is_valid(g->device)) {
		return;
	}
	for (i = 0u; i < g->image_count; ++i) {
		if (sk_framebuffer_t_is_valid(g->framebuffers[i])) {
			g->api->destroy_framebuffer(g->device, g->framebuffers[i]);
			g->framebuffers[i] = sk_framebuffer_t_zero();
		}
		if (sk_texture_view_t_is_valid(g->views[i])) {
			g->api->destroy_texture_view(g->device, g->views[i]);
			g->views[i] = sk_texture_view_t_zero();
		}
	}
	g->image_count = 0u;
}

static i32 editor_gpu_build_frame_targets(sk_editor_gpu_t* g) {
	u32 i;
	u32 count;

	editor_gpu_destroy_frame_targets(g);

	count = g->api->get_swapchain_image_count(g->device, g->swapchain);
	if (count == 0u || count > EDITOR_GPU_MAX_SWAP_IMAGES) {
		return -1;
	}
	g->format = g->api->get_swapchain_format(g->device, g->swapchain);
	{
		sk_extent3d_t ext = g->api->get_swapchain_extent(g->device, g->swapchain);
		g->fb_w = ext.width;
		g->fb_h = ext.height;
	}
	if (g->fb_w == 0u || g->fb_h == 0u || g->format == SK_PIXEL_FORMAT_UNKNOWN) {
		return -1;
	}

	if (sk_render_pass_t_is_valid(g->render_pass)) {
		g->api->destroy_render_pass(g->device, g->render_pass);
		g->render_pass = sk_render_pass_t_zero();
	}
	{
		sk_attachment_desc_t att;
		sk_render_pass_desc_t pass_desc;
		memset(&att, 0, sizeof(att));
		att.initial_state = SK_RESOURCE_STATE_UNDEFINED;
		att.final_state = SK_RESOURCE_STATE_PRESENT;
		att.load_op = SK_ATTACHMENT_LOAD_OP_CLEAR;
		att.store_op = SK_ATTACHMENT_STORE_OP_STORE;
		att.stencil_load_op = SK_ATTACHMENT_LOAD_OP_DONT_CARE;
		att.stencil_store_op = SK_ATTACHMENT_STORE_OP_DONT_CARE;
		att.sample_count = 1u;
		att.format = g->format;
		memset(&pass_desc, 0, sizeof(pass_desc));
		pass_desc.attachments = &att;
		pass_desc.attachment_count = 1u;
		pass_desc.debug_name = "editor-ui-pass";
		g->render_pass = g->api->create_render_pass(g->device, &pass_desc);
		if (!sk_render_pass_t_is_valid(g->render_pass)) {
			return -1;
		}
	}

	for (i = 0u; i < count; ++i) {
		sk_texture_t image = g->api->get_swapchain_image(g->device, g->swapchain, i);
		sk_texture_view_desc_t vdesc;
		sk_framebuffer_desc_t fb_desc;
		if (!sk_texture_t_is_valid(image)) {
			editor_gpu_destroy_frame_targets(g);
			return -1;
		}
		memset(&vdesc, 0, sizeof(vdesc));
		vdesc.texture = image;
		vdesc.type = SK_TEXTURE_VIEW_TYPE_2D;
		vdesc.mip_level_count = 1u;
		vdesc.array_layer_count = 1u;
		vdesc.debug_name = "editor-swap-view";
		g->views[i] = g->api->create_texture_view(g->device, &vdesc);
		if (!sk_texture_view_t_is_valid(g->views[i])) {
			editor_gpu_destroy_frame_targets(g);
			return -1;
		}
		memset(&fb_desc, 0, sizeof(fb_desc));
		fb_desc.render_pass = g->render_pass;
		fb_desc.attachments = &g->views[i];
		fb_desc.attachment_count = 1u;
		fb_desc.debug_name = "editor-swap-fb";
		g->framebuffers[i] = g->api->create_framebuffer(g->device, &fb_desc);
		if (!sk_framebuffer_t_is_valid(g->framebuffers[i])) {
			editor_gpu_destroy_frame_targets(g);
			return -1;
		}
	}
	g->image_count = count;
	return 0;
}

static void editor_gpu_wait_idle(sk_editor_gpu_t* g) {
	if (g->api == NULL || !sk_render_device_t_is_valid(g->device)) {
		return;
	}
	(void)g->api->wait_idle(g->device);
}

static void editor_gpu_drain_acquire_sem(sk_editor_gpu_t* g) {
	sk_submit_info_t submit;
	if (!sk_semaphore_t_is_valid(g->acquire_sem) || !sk_queue_t_is_valid(g->queue) || !sk_fence_t_is_valid(g->fence)) {
		return;
	}
	g->api->reset_fences(g->device, &g->fence, 1u);
	memset(&submit, 0, sizeof(submit));
	submit.wait_semaphores = &g->acquire_sem;
	submit.wait_semaphore_count = 1u;
	submit.signal_fence = g->fence;
	if (g->api->submit(g->device, g->queue, &submit) == 0) {
		(void)g->api->wait_fences(g->device, &g->fence, 1u, true, UINT64_MAX);
	}
}

static i32 editor_gpu_recreate_swapchain(sk_editor_gpu_t* g, sk_window_t window, u32 width, u32 height) {
	if (g->api == NULL || !sk_swapchain_t_is_valid(g->swapchain)) {
		return -1;
	}
	editor_gpu_wait_idle(g);
	editor_gpu_destroy_frame_targets(g);
	if (g->api->resize_swapchain(g->device, g->swapchain, width, height) != 0) {
		g->api->destroy_swapchain(g->device, g->swapchain);
		{
			sk_swapchain_desc_t sc;
			memset(&sc, 0, sizeof(sc));
			sc.window = window;
			sc.width = width;
			sc.height = height;
			sc.format = SK_PIXEL_FORMAT_UNKNOWN;
			sc.vsync = true;
			sc.debug_name = "editor-swapchain";
			g->swapchain = g->api->create_swapchain(g->device, &sc);
		}
		if (!sk_swapchain_t_is_valid(g->swapchain)) {
			return -1;
		}
	}
	if (editor_gpu_build_frame_targets(g) != 0) {
		return -1;
	}
	if (g->ui_renderer != NULL && g->ui != NULL) {
		if (g->ui->renderer_set_render_pass(g->ui_renderer, g->render_pass) != 0) {
			return -1;
		}
	}
	return 0;
}

static void editor_gpu_log_warn(sk_editor_gpu_t* g, const_chr_t msg) {
	if (g->logger_api != NULL && g->log != NULL) {
		sk_log_warn(g->logger_api, g->log, "%s", msg);
	}
}

sk_editor_gpu_t* sk_editor_gpu_create(sk_app_context_t* app, const sk_app_api_t* app_api, const sk_ui_api_t* ui, const sk_platform_window_api_t* win_api,
									  sk_window_t window, const sk_logger_api_t* logger_api, sk_logger_t* log) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_editor_gpu_t* g;
	sk_extent_t fb;
	sk_adapter_t adapter;
	sk_queue_desc_t q_desc;
	sk_swapchain_desc_t sc_desc;
	sk_command_buffer_desc_t cb_desc;
	sk_fence_desc_t f_desc;
	sk_semaphore_desc_t sem_desc;
	sk_ui_renderer_desc_t r_desc;

	g = (sk_editor_gpu_t*)alloc->alloc(alloc->instance, sizeof(sk_editor_gpu_t));
	if (g == NULL) {
		return NULL;
	}
	memset(g, 0, sizeof(*g));
	g->ui = ui;
	g->logger_api = logger_api;
	g->log = log;
	g->api = (const sk_render_device_api_t*)app_api->get_api(app, SK_RENDER_DEVICE_API_TYPE_ID);
	g->dxc = (const sk_dxc_compiler_api_t*)app_api->get_api(app, SK_DXC_COMPILER_API_TYPE_ID);
	if (g->api == NULL || g->api->create_swapchain == NULL || g->api->present == NULL) {
		editor_gpu_log_warn(g, "render device API missing swapchain/present (need sk-vulkan-render-device)");
		alloc->free(alloc->instance, g);
		return NULL;
	}
	if (g->dxc == NULL || g->dxc->init == NULL) {
		editor_gpu_log_warn(g, "dxc API missing (need sk-dxc-compiler)");
		alloc->free(alloc->instance, g);
		return NULL;
	}
	if (g->dxc->init() != 0) {
		editor_gpu_log_warn(g, "dxc init failed");
		alloc->free(alloc->instance, g);
		return NULL;
	}

	g->device = g->api->init(app, NULL);
	if (!sk_render_device_t_is_valid(g->device)) {
		editor_gpu_log_warn(g, "render device init failed (no GPU/ICD?)");
		g->dxc->shutdown();
		alloc->free(alloc->instance, g);
		return NULL;
	}

	adapter = editor_gpu_select_adapter(g->api, g->device);
	if (!sk_adapter_t_is_valid(adapter) || g->api->select_adapter(g->device, adapter) != 0) {
		editor_gpu_log_warn(g, "no suitable GPU adapter");
		sk_editor_gpu_destroy(g);
		return NULL;
	}

	memset(&q_desc, 0, sizeof(q_desc));
	q_desc.queue_type = (u32)SK_QUEUE_TYPE_GRAPHICS;
	g->queue = g->api->create_queue(g->device, &q_desc);
	if (!sk_queue_t_is_valid(g->queue)) {
		sk_editor_gpu_destroy(g);
		return NULL;
	}

	fb = win_api->get_framebuffer_size(window);
	memset(&sc_desc, 0, sizeof(sc_desc));
	sc_desc.window = window;
	sc_desc.width = fb.width;
	sc_desc.height = fb.height;
	sc_desc.format = SK_PIXEL_FORMAT_UNKNOWN;
	sc_desc.vsync = true;
	sc_desc.debug_name = "editor-swapchain";
	g->swapchain = g->api->create_swapchain(g->device, &sc_desc);
	if (!sk_swapchain_t_is_valid(g->swapchain)) {
		editor_gpu_log_warn(g, "create_swapchain failed");
		sk_editor_gpu_destroy(g);
		return NULL;
	}

	if (editor_gpu_build_frame_targets(g) != 0) {
		editor_gpu_log_warn(g, "swapchain frame targets failed");
		sk_editor_gpu_destroy(g);
		return NULL;
	}

	memset(&cb_desc, 0, sizeof(cb_desc));
	cb_desc.level = SK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cb_desc.queue_type = (u32)SK_QUEUE_TYPE_GRAPHICS;
	cb_desc.debug_name = "editor-ui-cmd";
	g->cmd = g->api->create_command_buffer(g->device, &cb_desc);
	if (!sk_command_buffer_t_is_valid(g->cmd)) {
		sk_editor_gpu_destroy(g);
		return NULL;
	}

	memset(&f_desc, 0, sizeof(f_desc));
	f_desc.debug_name = "editor-ui-fence";
	g->fence = g->api->create_fence(g->device, &f_desc);
	if (!sk_fence_t_is_valid(g->fence)) {
		sk_editor_gpu_destroy(g);
		return NULL;
	}

	memset(&sem_desc, 0, sizeof(sem_desc));
	sem_desc.debug_name = "editor-ui-acquire";
	g->acquire_sem = g->api->create_semaphore(g->device, &sem_desc);
	if (!sk_semaphore_t_is_valid(g->acquire_sem)) {
		sk_editor_gpu_destroy(g);
		return NULL;
	}

	memset(&r_desc, 0, sizeof(r_desc));
	r_desc.device_api = g->api;
	r_desc.device = g->device;
	r_desc.dxc = g->dxc;
	r_desc.render_pass = g->render_pass;
	g->ui_renderer = ui->renderer_create(&r_desc);
	if (g->ui_renderer == NULL) {
		editor_gpu_log_warn(g, "ui renderer_create failed (DXC/HLSL?)");
		sk_editor_gpu_destroy(g);
		return NULL;
	}

	if (g->logger_api != NULL && g->log != NULL) {
		sk_log_info(g->logger_api, g->log, "GPU present ready (%ux%u, %u images)", g->fb_w, g->fb_h, g->image_count);
	}
	return g;
}

void sk_editor_gpu_destroy(sk_editor_gpu_t* g) {
	const sk_allocator_t* alloc;
	if (g == NULL) {
		return;
	}
	editor_gpu_wait_idle(g);
	if (g->ui != NULL && g->ui_renderer != NULL) {
		g->ui->renderer_destroy(g->ui_renderer);
		g->ui_renderer = NULL;
	}
	if (sk_render_device_t_is_valid(g->device)) {
		if (sk_fence_t_is_valid(g->fence)) {
			g->api->destroy_fence(g->device, g->fence);
			g->fence = sk_fence_t_zero();
		}
		if (sk_semaphore_t_is_valid(g->acquire_sem)) {
			g->api->destroy_semaphore(g->device, g->acquire_sem);
			g->acquire_sem = sk_semaphore_t_zero();
		}
		if (sk_command_buffer_t_is_valid(g->cmd)) {
			g->api->destroy_command_buffer(g->device, g->cmd);
			g->cmd = sk_command_buffer_t_zero();
		}
		editor_gpu_destroy_frame_targets(g);
		if (sk_render_pass_t_is_valid(g->render_pass)) {
			g->api->destroy_render_pass(g->device, g->render_pass);
			g->render_pass = sk_render_pass_t_zero();
		}
		if (sk_swapchain_t_is_valid(g->swapchain)) {
			g->api->destroy_swapchain(g->device, g->swapchain);
			g->swapchain = sk_swapchain_t_zero();
		}
		if (sk_queue_t_is_valid(g->queue)) {
			g->api->destroy_queue(g->device, g->queue);
			g->queue = sk_queue_t_zero();
		}
		g->api->destroy(g->device);
		g->device = sk_render_device_t_zero();
	}
	if (g->dxc != NULL) {
		g->dxc->shutdown();
		g->dxc = NULL;
	}
	alloc = sk_allocator_default();
	alloc->free(alloc->instance, g);
}

i32 sk_editor_gpu_present(sk_editor_gpu_t* g, sk_ui_context_t* ctx, sk_ui_font_system_t* fonts, const sk_platform_window_api_t* win_api, sk_window_t window) {
	const sk_ui_draw_list_t* dl;
	sk_extent_t fb;
	sk_acquire_info_t acq;
	sk_device_result_t acq_rc;
	u32 image_index = 0u;
	sk_command_buffer_begin_info_t begin_info;
	sk_ui_renderer_prepare_info_t prep;
	sk_ui_renderer_encode_info_t enc;
	sk_clear_values_t clear;
	sk_begin_render_pass_info_t rp_info;
	sk_submit_info_t submit;
	sk_present_info_t present;

	if (g == NULL || g->ui == NULL || ctx == NULL || g->ui_renderer == NULL) {
		return -1;
	}

	fb = win_api->get_framebuffer_size(window);
	if (fb.width == 0u || fb.height == 0u) {
		return -1;
	}
	if (fb.width != g->fb_w || fb.height != g->fb_h) {
		if (editor_gpu_recreate_swapchain(g, window, fb.width, fb.height) != 0) {
			editor_gpu_log_warn(g, "swapchain recreate failed");
			return -1;
		}
	}

	dl = g->ui->get_draw_list(ctx);
	if (dl == NULL) {
		return -1;
	}

	memset(&acq, 0, sizeof(acq));
	acq.swapchain = g->swapchain;
	acq.timeout_ns = UINT64_MAX;
	acq.signal_semaphore = g->acquire_sem;
	acq_rc = g->api->acquire_next_image(g->device, &acq, &image_index);
	if (acq_rc == SK_DEVICE_RESULT_SWAPCHAIN_OUT_OF_DATE) {
		(void)editor_gpu_recreate_swapchain(g, window, fb.width, fb.height);
		return -1;
	}
	if (acq_rc != SK_DEVICE_RESULT_SUCCESS || image_index >= g->image_count) {
		if (acq_rc == SK_DEVICE_RESULT_SUCCESS) {
			editor_gpu_drain_acquire_sem(g);
		}
		return -1;
	}

	g->api->reset_command_buffer(g->device, g->cmd);
	memset(&begin_info, 0, sizeof(begin_info));
	begin_info.usage_flags = (u32)SK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT;
	if (g->api->begin_command_buffer(g->device, g->cmd, &begin_info) != 0) {
		editor_gpu_drain_acquire_sem(g);
		return -1;
	}

	memset(&prep, 0, sizeof(prep));
	prep.cmd = g->cmd;
	prep.draw_list = dl;
	prep.font_system = fonts;
	(void)g->ui->renderer_prepare(g->ui_renderer, &prep);

	memset(&clear, 0, sizeof(clear));
	clear.color.r = 0.05f;
	clear.color.g = 0.06f;
	clear.color.b = 0.08f;
	clear.color.a = 1.0f;
	memset(&rp_info, 0, sizeof(rp_info));
	rp_info.render_pass = g->render_pass;
	rp_info.framebuffer = g->framebuffers[image_index];
	rp_info.clear_values = &clear;
	g->api->begin_render_pass(g->device, g->cmd, &rp_info);

	memset(&enc, 0, sizeof(enc));
	enc.cmd = g->cmd;
	enc.draw_list = dl;
	enc.target_width = g->fb_w;
	enc.target_height = g->fb_h;
	(void)g->ui->renderer_encode(g->ui_renderer, &enc);

	g->api->end_render_pass(g->device, g->cmd);
	g->api->end_command_buffer(g->device, g->cmd);

	g->api->reset_fences(g->device, &g->fence, 1u);
	memset(&submit, 0, sizeof(submit));
	submit.command_buffers = &g->cmd;
	submit.command_buffer_count = 1u;
	submit.wait_semaphores = &g->acquire_sem;
	submit.wait_semaphore_count = 1u;
	submit.signal_fence = g->fence;
	if (g->api->submit(g->device, g->queue, &submit) != 0) {
		editor_gpu_drain_acquire_sem(g);
		return -1;
	}
	(void)g->api->wait_fences(g->device, &g->fence, 1u, true, UINT64_MAX);

	memset(&present, 0, sizeof(present));
	present.swapchain = g->swapchain;
	present.image_index = image_index;
	if (g->api->present(g->device, g->queue, &present) == SK_DEVICE_RESULT_SWAPCHAIN_OUT_OF_DATE) {
		(void)editor_gpu_recreate_swapchain(g, window, fb.width, fb.height);
		return -1;
	}
	return 0;
}
