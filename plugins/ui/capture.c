/*
 * capture.c — headless offscreen UI render with CPU readback (APX-226).
 *
 * Owns an RGBA8 offscreen color target (device-owned texture; no swapchain,
 * window, or surface), the standard UI GPU renderer bound to it, a
 * host-visible readback buffer, and the queue / command buffers / fences for
 * one frame of work. This is a thin orchestration layer over existing pieces
 * (the ui renderer in render.c + the RHI texture/pass/framebuffer/copy APIs)
 * following the proven recipe in tests/integration/ui_render.c — not a
 * parallel renderer.
 *
 * capture_frame() runs, per frame:
 *   renderer_prepare (geometry/atlas upload)  → submit + wait
 *   begin_render_pass (explicit CLEAR)        → renderer_encode
 *   end_render_pass → memory_barrier → copy_texture_to_buffer
 *   submit + wait → buffer_map → row-wise copy → buffer_unmap
 *
 * The readback buffer is created exactly width*height*4 bytes and the copy
 * uses buffer_row_length = 0 (tightly packed rows), so the mapped rows have
 * a pitch of width*4. The row-wise copy keeps the returned sk_ui_cpu_image_t
 * tightly packed by construction (stride = width * 4) even if a backend ever
 * padded rows differently.
 *
 * Color / alpha contract: the output is straight (non-premultiplied) RGBA8,
 * R in the low byte, row-major, top-left origin. Draw-list colors are packed
 * straight (sk_ui_pack_color) and the UI pipeline blends with standard
 * straight-alpha factors (SRC_ALPHA / ONE_MINUS_SRC_ALPHA, src alpha ONE),
 * so the cleared target + draw list compose to straight alpha.
 */

#include "ui_internal.h"

#include "render_device.h"

#include <stdio.h>
#include <string.h>

#define UI_CAPTURE_CHANNELS 4u /* RGBA8 */

struct sk_ui_capture_t {
	const sk_allocator_t* allocator;
	const sk_render_device_api_t* api;
	sk_render_device_t device;

	u32 width;
	u32 height;
	sk_clear_values_t clear_color;

	sk_texture_t color_tex;
	sk_texture_view_t color_view;
	sk_render_pass_t pass;
	sk_framebuffer_t fb;
	sk_buffer_t readback;
	sk_queue_t queue;
	sk_command_buffer_t upload_cmd;
	sk_command_buffer_t draw_cmd;
	sk_fence_t upload_fence;
	sk_fence_t draw_fence;

	sk_ui_renderer_t* renderer;

	/* CPU image returned by the last capture_frame (owned by the capture). */
	sk_ui_cpu_image_t image;
};

/* -------------------------------------------------------------------------- */
/* Public impl                                                                */
/* -------------------------------------------------------------------------- */

sk_ui_capture_t* ui_capture_create_impl(const sk_ui_capture_desc_t* desc) {
	const sk_allocator_t* a;
	sk_ui_capture_t* c;
	const sk_render_device_api_t* api;
	sk_render_device_t dev;
	sk_texture_desc_t tdesc;
	sk_texture_view_desc_t vdesc;
	sk_attachment_desc_t attachment;
	sk_render_pass_desc_t pass_desc;
	sk_framebuffer_desc_t fb_desc;
	sk_buffer_desc_t rb_desc;
	sk_queue_desc_t q_desc;
	sk_command_buffer_desc_t cb_desc;
	sk_fence_desc_t f_desc;
	sk_ui_renderer_desc_t rdesc;
	char name[96];

	if (desc == NULL || desc->device_api == NULL || desc->dxc == NULL) {
		return NULL;
	}
	if (!sk_render_device_t_is_valid(desc->device) || desc->width == 0u || desc->height == 0u) {
		return NULL;
	}

	a = desc->allocator != NULL ? desc->allocator : sk_allocator_default();
	c = (sk_ui_capture_t*)a->alloc(a->instance, sizeof(sk_ui_capture_t));
	if (c == NULL) {
		return NULL;
	}
	memset(c, 0, sizeof(*c));
	c->allocator = a;
	c->api = desc->device_api;
	c->device = desc->device;
	c->width = desc->width;
	c->height = desc->height;
	c->clear_color = desc->clear_color;
	api = c->api;
	dev = c->device;

	/* Offscreen color target: render into, copy out. */
	memset(&tdesc, 0, sizeof(tdesc));
	tdesc.extent.width = c->width;
	tdesc.extent.height = c->height;
	tdesc.extent.depth = 1u;
	tdesc.mip_levels = 1u;
	tdesc.array_layers = 1u;
	tdesc.sample_count = 1u;
	tdesc.format = SK_PIXEL_FORMAT_RGBA8_UNORM;
	tdesc.usage_flags = (u32)SK_RESOURCE_USAGE_RENDER_TARGET | (u32)SK_RESOURCE_USAGE_COPY_SOURCE;
	snprintf(name, sizeof(name), "%s-color", desc->debug_name != NULL ? desc->debug_name : "ui-capture");
	tdesc.debug_name = name;
	c->color_tex = api->create_texture(dev, &tdesc);
	if (!sk_texture_t_is_valid(c->color_tex)) {
		ui_capture_destroy_impl(c);
		return NULL;
	}

	memset(&vdesc, 0, sizeof(vdesc));
	vdesc.texture = c->color_tex;
	vdesc.type = SK_TEXTURE_VIEW_TYPE_2D;
	vdesc.mip_level_count = 1u;
	vdesc.array_layer_count = 1u;
	snprintf(name, sizeof(name), "%s-view", desc->debug_name != NULL ? desc->debug_name : "ui-capture");
	vdesc.debug_name = name;
	c->color_view = api->create_texture_view(dev, &vdesc);
	if (!sk_texture_view_t_is_valid(c->color_view)) {
		ui_capture_destroy_impl(c);
		return NULL;
	}

	memset(&attachment, 0, sizeof(attachment));
	attachment.initial_state = SK_RESOURCE_STATE_UNDEFINED;
	attachment.final_state = SK_RESOURCE_STATE_COPY_SOURCE;
	attachment.load_op = SK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.store_op = SK_ATTACHMENT_STORE_OP_STORE;
	attachment.sample_count = 1u;
	attachment.format = SK_PIXEL_FORMAT_RGBA8_UNORM;

	memset(&pass_desc, 0, sizeof(pass_desc));
	pass_desc.attachments = &attachment;
	pass_desc.attachment_count = 1u;
	snprintf(name, sizeof(name), "%s-pass", desc->debug_name != NULL ? desc->debug_name : "ui-capture");
	pass_desc.debug_name = name;
	c->pass = api->create_render_pass(dev, &pass_desc);
	if (!sk_render_pass_t_is_valid(c->pass)) {
		ui_capture_destroy_impl(c);
		return NULL;
	}

	memset(&fb_desc, 0, sizeof(fb_desc));
	fb_desc.render_pass = c->pass;
	fb_desc.attachments = &c->color_view;
	fb_desc.attachment_count = 1u;
	snprintf(name, sizeof(name), "%s-fb", desc->debug_name != NULL ? desc->debug_name : "ui-capture");
	fb_desc.debug_name = name;
	c->fb = api->create_framebuffer(dev, &fb_desc);
	if (!sk_framebuffer_t_is_valid(c->fb)) {
		ui_capture_destroy_impl(c);
		return NULL;
	}

	/* Host-visible readback: exactly width*height*4 bytes (tight rows). */
	memset(&rb_desc, 0, sizeof(rb_desc));
	rb_desc.size = (u64)c->width * c->height * UI_CAPTURE_CHANNELS;
	rb_desc.usage_flags = (u32)SK_RESOURCE_USAGE_COPY_DEST;
	rb_desc.host_visible = true;
	snprintf(name, sizeof(name), "%s-readback", desc->debug_name != NULL ? desc->debug_name : "ui-capture");
	rb_desc.debug_name = name;
	c->readback = api->create_buffer(dev, &rb_desc);
	if (!sk_buffer_t_is_valid(c->readback)) {
		ui_capture_destroy_impl(c);
		return NULL;
	}

	memset(&q_desc, 0, sizeof(q_desc));
	q_desc.queue_type = (u32)SK_QUEUE_TYPE_GRAPHICS;
	c->queue = api->create_queue(dev, &q_desc);
	if (!sk_queue_t_is_valid(c->queue)) {
		ui_capture_destroy_impl(c);
		return NULL;
	}

	memset(&cb_desc, 0, sizeof(cb_desc));
	cb_desc.level = SK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cb_desc.queue_type = (u32)SK_QUEUE_TYPE_GRAPHICS;
	snprintf(name, sizeof(name), "%s-upload-cmd", desc->debug_name != NULL ? desc->debug_name : "ui-capture");
	cb_desc.debug_name = name;
	c->upload_cmd = api->create_command_buffer(dev, &cb_desc);
	snprintf(name, sizeof(name), "%s-draw-cmd", desc->debug_name != NULL ? desc->debug_name : "ui-capture");
	cb_desc.debug_name = name;
	c->draw_cmd = api->create_command_buffer(dev, &cb_desc);
	if (!sk_command_buffer_t_is_valid(c->upload_cmd) || !sk_command_buffer_t_is_valid(c->draw_cmd)) {
		ui_capture_destroy_impl(c);
		return NULL;
	}

	memset(&f_desc, 0, sizeof(f_desc));
	snprintf(name, sizeof(name), "%s-upload-fence", desc->debug_name != NULL ? desc->debug_name : "ui-capture");
	f_desc.debug_name = name;
	c->upload_fence = api->create_fence(dev, &f_desc);
	snprintf(name, sizeof(name), "%s-draw-fence", desc->debug_name != NULL ? desc->debug_name : "ui-capture");
	f_desc.debug_name = name;
	c->draw_fence = api->create_fence(dev, &f_desc);
	if (!sk_fence_t_is_valid(c->upload_fence) || !sk_fence_t_is_valid(c->draw_fence)) {
		ui_capture_destroy_impl(c);
		return NULL;
	}

	/* Standard UI GPU renderer bound to the offscreen pass (reuses render.c). */
	memset(&rdesc, 0, sizeof(rdesc));
	rdesc.device_api = api;
	rdesc.device = dev;
	rdesc.dxc = desc->dxc;
	rdesc.render_pass = c->pass;
	rdesc.allocator = a;
	c->renderer = ui_renderer_create_impl(&rdesc);
	if (c->renderer == NULL) {
		ui_capture_destroy_impl(c);
		return NULL;
	}

	return c;
}

void ui_capture_destroy_impl(sk_ui_capture_t* capture) {
	const sk_allocator_t* a;
	const sk_render_device_api_t* api;
	sk_render_device_t dev;

	if (capture == NULL) {
		return;
	}
	a = capture->allocator;
	api = capture->api;
	dev = capture->device;

	if (capture->renderer != NULL) {
		ui_renderer_destroy_impl(capture->renderer);
		capture->renderer = NULL;
	}
	if (sk_fence_t_is_valid(capture->draw_fence)) {
		api->destroy_fence(dev, capture->draw_fence);
	}
	if (sk_fence_t_is_valid(capture->upload_fence)) {
		api->destroy_fence(dev, capture->upload_fence);
	}
	if (sk_command_buffer_t_is_valid(capture->draw_cmd)) {
		api->destroy_command_buffer(dev, capture->draw_cmd);
	}
	if (sk_command_buffer_t_is_valid(capture->upload_cmd)) {
		api->destroy_command_buffer(dev, capture->upload_cmd);
	}
	if (sk_queue_t_is_valid(capture->queue)) {
		api->destroy_queue(dev, capture->queue);
	}
	if (sk_buffer_t_is_valid(capture->readback)) {
		api->destroy_buffer(dev, capture->readback);
	}
	if (sk_framebuffer_t_is_valid(capture->fb)) {
		api->destroy_framebuffer(dev, capture->fb);
	}
	if (sk_render_pass_t_is_valid(capture->pass)) {
		api->destroy_render_pass(dev, capture->pass);
	}
	if (sk_texture_view_t_is_valid(capture->color_view)) {
		api->destroy_texture_view(dev, capture->color_view);
	}
	if (sk_texture_t_is_valid(capture->color_tex)) {
		api->destroy_texture(dev, capture->color_tex);
	}
	if (capture->image.pixels != NULL) {
		a->free(a->instance, capture->image.pixels);
	}
	memset(capture, 0, sizeof(*capture));
	a->free(a->instance, capture);
}

/* Submit one command buffer and wait on its fence, then reset it so the
 * same fence can be reused by the next frame (RHI contract: signal_fence
 * must be unsignaled at submit time — reset after every wait, mirroring
 * sk_vkrd_submit_and_wait). */
static i32 ui_capture_submit_and_wait(sk_ui_capture_t* c, sk_command_buffer_t cmd, sk_fence_t fence) {
	const sk_render_device_api_t* api = c->api;
	sk_submit_info_t submit;

	memset(&submit, 0, sizeof(submit));
	submit.command_buffers = &cmd;
	submit.command_buffer_count = 1u;
	submit.signal_fence = fence;
	if (api->submit(c->device, c->queue, &submit) != 0) {
		return -1;
	}
	if (api->wait_fences(c->device, &fence, 1u, true, UINT64_MAX) != 0) {
		return -1;
	}
	api->reset_fences(c->device, &fence, 1u);
	return 0;
}

i32 ui_capture_frame_impl(sk_ui_capture_t* capture, const sk_ui_capture_frame_info_t* info, sk_ui_cpu_image_t* out_image) {
	const sk_render_device_api_t* api;
	sk_render_device_t dev;
	sk_command_buffer_begin_info_t begin_info;
	sk_ui_renderer_prepare_info_t prep;
	sk_ui_renderer_encode_info_t enc;
	sk_begin_render_pass_info_t rp_info;
	sk_buffer_texture_copy_t copy;
	sk_ui_cpu_image_t* img;
	void_ptr_t mapped;
	u32 row_bytes;
	u32 y;
	u8* dst;
	const u8* src;

	if (capture == NULL || info == NULL || info->draw_list == NULL || out_image == NULL) {
		return -1;
	}
	api = capture->api;
	dev = capture->device;

	/* 1) Geometry / atlas upload (outside a render pass). */
	memset(&begin_info, 0, sizeof(begin_info));
	begin_info.usage_flags = (u32)SK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT;
	if (api->begin_command_buffer(dev, capture->upload_cmd, &begin_info) != 0) {
		return -1;
	}
	memset(&prep, 0, sizeof(prep));
	prep.cmd = capture->upload_cmd;
	prep.draw_list = info->draw_list;
	prep.font_system = info->font_system;
	prep.font = info->font;
	if (ui_renderer_prepare_impl(capture->renderer, &prep) != 0) {
		return -1;
	}
	api->end_command_buffer(dev, capture->upload_cmd);
	if (ui_capture_submit_and_wait(capture, capture->upload_cmd, capture->upload_fence) != 0) {
		return -1;
	}

	/* 2) Draw into the offscreen target with the explicit clear color. */
	if (api->begin_command_buffer(dev, capture->draw_cmd, &begin_info) != 0) {
		return -1;
	}
	memset(&rp_info, 0, sizeof(rp_info));
	rp_info.render_pass = capture->pass;
	rp_info.framebuffer = capture->fb;
	rp_info.clear_values = &capture->clear_color;
	api->begin_render_pass(dev, capture->draw_cmd, &rp_info);

	memset(&enc, 0, sizeof(enc));
	enc.cmd = capture->draw_cmd;
	enc.draw_list = info->draw_list;
	enc.target_width = capture->width;
	enc.target_height = capture->height;
	enc.images = info->images;
	if (ui_renderer_encode_impl(capture->renderer, &enc) != 0) {
		return -1;
	}
	api->end_render_pass(dev, capture->draw_cmd);

	/* 3) Copy target → host-visible readback buffer (tight rows). */
	api->memory_barrier(dev, capture->draw_cmd);
	memset(&copy, 0, sizeof(copy));
	copy.buffer = capture->readback;
	copy.texture = capture->color_tex;
	copy.texture_extent.width = capture->width;
	copy.texture_extent.height = capture->height;
	copy.texture_extent.depth = 1u;
	/* buffer_row_length = 0: tightly packed rows (pitch = width * 4). */
	api->copy_texture_to_buffer(dev, capture->draw_cmd, &copy);
	api->end_command_buffer(dev, capture->draw_cmd);
	if (ui_capture_submit_and_wait(capture, capture->draw_cmd, capture->draw_fence) != 0) {
		return -1;
	}

	/* 4) Map and unpack rows into a tightly packed CPU image. */
	img = &capture->image;
	row_bytes = capture->width * UI_CAPTURE_CHANNELS;
	if (img->pixels == NULL || img->width != capture->width || img->height != capture->height || img->channels != UI_CAPTURE_CHANNELS) {
		if (img->pixels != NULL) {
			capture->allocator->free(capture->allocator->instance, img->pixels);
			img->pixels = NULL;
		}
		img->pixels = (u8*)capture->allocator->alloc(capture->allocator->instance, (size_t)row_bytes * capture->height);
		if (img->pixels == NULL) {
			memset(img, 0, sizeof(*img));
			return -1;
		}
	}
	mapped = api->buffer_map(dev, capture->readback);
	if (mapped == NULL) {
		return -1;
	}
	src = (const u8*)mapped;
	dst = img->pixels;
	for (y = 0u; y < capture->height; ++y) {
		memcpy(dst + (size_t)y * row_bytes, src + (size_t)y * row_bytes, row_bytes);
	}
	api->buffer_unmap(dev, capture->readback);

	img->width = capture->width;
	img->height = capture->height;
	img->channels = UI_CAPTURE_CHANNELS;
	*out_image = *img;
	return 0;
}
