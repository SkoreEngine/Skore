/* Entry point for the exported game (player host).
 *
 * Hosts a simple sk-ui widget playground for manual testing:
 *   panel, view, label, button, checkbox, slider, text_input, scroll_view, image
 *
 * Per-frame pipeline:
 *   poll_events → mouse → input_dispatch → style_resolve → layout →
 *   apply_scale → paint → [GPU] prepare → render pass + encode → present
 *
 * Plugins (window, ui, vulkan/dxc, …) come from sk_app_init auto-load of
 * {app_folder}/plugins — no manual load_plugin here.
 */

#include "app.h"
#include "dxc_compiler.h"
#include "filesystem.h"
#include "logger.h"
#include "path.h"
#include "platform_window.h"
#include "profiler.h"
#include "render_device.h"
#include "render_graph.h"
#include "render_pipeline.h"
#include "ui.h"

/* Embedded fixture font from the ui plugin testdata (same as goldens). */
#include "skore_test_font_ttf.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PLAYER_MAX_SWAP_IMAGES 8u

typedef struct player_gpu_t {
	const sk_render_device_api_t* api;
	const sk_dxc_compiler_api_t* dxc;
	sk_render_device_t device;
	sk_queue_t queue;
	sk_swapchain_t swapchain;
	sk_render_pass_t render_pass;
	sk_texture_view_t views[PLAYER_MAX_SWAP_IMAGES];
	sk_framebuffer_t framebuffers[PLAYER_MAX_SWAP_IMAGES];
	u32 image_count;
	u32 fb_w;
	u32 fb_h;
	sk_pixel_format_t format;
	sk_ui_renderer_t* ui_renderer;
	sk_command_buffer_t cmd;
	sk_fence_t fence;
	sk_semaphore_t acquire_sem;
	i32 ready;
} player_gpu_t;

typedef struct player_ui_state_t {
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_ui_font_system_t* fonts;
	sk_ui_font_t* font;
	sk_ui_node_t screen;
	sk_ui_node_t status;
	sk_ui_node_t clicks_label;
	sk_ui_node_t volume_label;
	sk_ui_node_t btn_click;
	sk_ui_node_t btn_reset;
	sk_ui_node_t checkbox;
	sk_ui_node_t slider;
	sk_ui_node_t name_input;
	sk_ui_node_t scroll;
	f32 last_scale_x;
	f32 last_scale_y;
	f32 pointer_x;
	f32 pointer_y;
	i32 pointer_down;
	u32 click_count;
	u32 frame;
	i32 dock_demo;
	sk_logger_t* log;
	player_gpu_t gpu;
} player_ui_state_t;

/* -------------------------------------------------------------------------- */
/* Widget callbacks                                                           */
/* -------------------------------------------------------------------------- */

static void player_set_status(player_ui_state_t* st, const_chr_t text) {
	if (st->ui == NULL || st->ctx == NULL || !sk_ui_node_is_valid(st->status)) {
		return;
	}
	(void)st->ui->label_set_text(st->ctx, st->status, text);
}

static void player_refresh_clicks(player_ui_state_t* st) {
	char buf[64];
	if (st->ui == NULL || !sk_ui_node_is_valid(st->clicks_label)) {
		return;
	}
	snprintf(buf, sizeof(buf), "Clicks: %u", st->click_count);
	(void)st->ui->label_set_text(st->ctx, st->clicks_label, buf);
}

static void player_refresh_volume(player_ui_state_t* st, f32 value) {
	char buf[64];
	if (st->ui == NULL || !sk_ui_node_is_valid(st->volume_label)) {
		return;
	}
	snprintf(buf, sizeof(buf), "Volume: %.0f%%", (double)(value * 100.0f));
	(void)st->ui->label_set_text(st->ctx, st->volume_label, buf);
}

static void player_on_button_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	player_ui_state_t* st = (player_ui_state_t*)user;
	(void)ctx;
	(void)node;
	if (event == NULL || event->type != SK_UI_EVENT_CLICK) {
		return;
	}
	st->click_count += 1u;
	player_refresh_clicks(st);
	player_set_status(st, "Button clicked");
}

static void player_on_reset_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	player_ui_state_t* st = (player_ui_state_t*)user;
	(void)ctx;
	(void)node;
	if (event == NULL || event->type != SK_UI_EVENT_CLICK) {
		return;
	}
	st->click_count = 0u;
	player_refresh_clicks(st);
	(void)st->ui->slider_set_value(st->ctx, st->slider, 0.7f);
	player_refresh_volume(st, 0.7f);
	(void)st->ui->checkbox_set_checked(st->ctx, st->checkbox, 0);
	(void)st->ui->text_input_set_text(st->ctx, st->name_input, "Player");
	player_set_status(st, "Reset");
}

static void player_on_checkbox(sk_ui_context_t* ctx, sk_ui_node_t node, i32 value, void_ptr_t user) {
	player_ui_state_t* st = (player_ui_state_t*)user;
	(void)ctx;
	(void)node;
	player_set_status(st, value ? "Fullscreen: on" : "Fullscreen: off");
}

static void player_on_slider(sk_ui_context_t* ctx, sk_ui_node_t node, f32 value, void_ptr_t user) {
	player_ui_state_t* st = (player_ui_state_t*)user;
	(void)ctx;
	(void)node;
	player_refresh_volume(st, value);
	player_set_status(st, "Slider moved");
}

/* -------------------------------------------------------------------------- */
/* Layout helpers                                                             */
/* -------------------------------------------------------------------------- */

static void player_style_screen(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_PADDING | SK_UI_SP_ROW_GAP |
			 SK_UI_SP_BACKGROUND_COLOR;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.justify_content = SK_UI_JUSTIFY_CENTER;
	p.layout.align_items = SK_UI_ALIGN_CENTER;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_percent(100.0f);
	p.layout.padding.left = 24.0f;
	p.layout.padding.top = 24.0f;
	p.layout.padding.right = 24.0f;
	p.layout.padding.bottom = 24.0f;
	p.layout.row_gap = 12.0f;
	p.background_color = sk_ui_rgba(0.07f, 0.08f, 0.11f, 1.0f);
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void player_style_card(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_MAX_WIDTH | SK_UI_SP_ALIGN_SELF | SK_UI_SP_PADDING | SK_UI_SP_ROW_GAP | SK_UI_SP_BACKGROUND_COLOR |
			 SK_UI_SP_BORDER_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_CORNER_RADIUS;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	/* Stretch the cross axis (width) up to max_width, keep the height fitting
	 * the content. A percent width would drop the max (Clay has no min/max on
	 * percent axes) and span the window; flex_grow would also grow the main
	 * axis and stretch the card down the whole screen. */
	p.layout.width = sk_ui_auto();
	p.layout.align_self = SK_UI_ALIGN_STRETCH;
	p.layout.max_width = sk_ui_pt(420.0f);
	p.layout.padding.left = 16.0f;
	p.layout.padding.top = 16.0f;
	p.layout.padding.right = 16.0f;
	p.layout.padding.bottom = 16.0f;
	p.layout.row_gap = 10.0f;
	p.layout.border.left = 1.0f;
	p.layout.border.top = 1.0f;
	p.layout.border.right = 1.0f;
	p.layout.border.bottom = 1.0f;
	p.background_color = sk_ui_rgba(0.14f, 0.15f, 0.19f, 1.0f);
	p.border_color = sk_ui_rgba(0.32f, 0.36f, 0.44f, 1.0f);
	p.corner_radius = 8.0f;
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void player_style_row(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_COLUMN_GAP | SK_UI_SP_WIDTH;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.align_items = SK_UI_ALIGN_CENTER;
	p.layout.column_gap = 10.0f;
	p.layout.width = sk_ui_percent(100.0f);
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void player_style_title(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FONT_SIZE | SK_UI_SP_COLOR | SK_UI_SP_HEIGHT | SK_UI_SP_WIDTH;
	p.font_size = 22.0f;
	p.color = sk_ui_rgba(0.95f, 0.96f, 0.98f, 1.0f);
	p.layout.height = sk_ui_pt(28.0f);
	p.layout.width = sk_ui_percent(100.0f);
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void player_style_hint(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FONT_SIZE | SK_UI_SP_COLOR | SK_UI_SP_HEIGHT | SK_UI_SP_WIDTH;
	p.font_size = 13.0f;
	p.color = sk_ui_rgba(0.72f, 0.76f, 0.82f, 1.0f);
	p.layout.height = sk_ui_pt(18.0f);
	p.layout.width = sk_ui_percent(100.0f);
	(void)ui->node_set_inline_style(ctx, node, &p);
}

/**
 * Hint label sitting *inside* a row (volume, checkbox caption).
 * Width must fit the text: a 100% width would eat the whole row and push the
 * sibling widget outside the card (Clay does not shrink percent children).
 */
static void player_style_hint_inline(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	player_style_hint(ui, ctx, node);
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH;
	p.layout.width = sk_ui_auto();
	(void)ui->node_merge_inline_style(ctx, node, &p);
}

static void player_style_field(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_FLEX_GROW;
	p.layout.width = sk_ui_auto();
	p.layout.min_height = sk_ui_pt(30.0f);
	p.layout.flex_grow = 1.0f;
	(void)ui->node_merge_inline_style(ctx, node, &p);
}

static void player_style_slider(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW;
	p.layout.width = sk_ui_auto();
	p.layout.height = sk_ui_pt(22.0f);
	p.layout.flex_grow = 1.0f;
	(void)ui->node_merge_inline_style(ctx, node, &p);
}

static void player_style_scroll(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_pt(72.0f);
	(void)ui->node_merge_inline_style(ctx, node, &p);
}

static void player_style_image(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_CORNER_RADIUS;
	p.layout.width = sk_ui_pt(56.0f);
	p.layout.height = sk_ui_pt(56.0f);
	p.layout.min_width = sk_ui_pt(56.0f);
	p.layout.min_height = sk_ui_pt(56.0f);
	p.background_color = sk_ui_rgba(0.22f, 0.48f, 0.78f, 1.0f);
	p.corner_radius = 6.0f;
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void player_style_button(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_MIN_HEIGHT | SK_UI_SP_FLEX_GROW;
	p.layout.min_height = sk_ui_pt(32.0f);
	p.layout.flex_grow = 1.0f;
	(void)ui->node_merge_inline_style(ctx, node, &p);
}

static void player_set_click(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_fn fn, void_ptr_t user) {
	sk_ui_node_callbacks_t cb;
	memset(&cb, 0, sizeof(cb));
	cb.on_click = fn;
	cb.user = user;
	(void)ui->node_set_callbacks(ctx, node, &cb);
}

static i32 player_ui_build(player_ui_state_t* st) {
	const sk_ui_api_t* ui = st->ui;
	sk_ui_context_t* ctx = st->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t card;
	sk_ui_node_t title;
	sk_ui_node_t header;
	sk_ui_node_t logo;
	sk_ui_node_t header_col;
	sk_ui_node_t opts;
	sk_ui_node_t vol_row;
	sk_ui_node_t actions;
	sk_ui_node_t content;
	sk_ui_node_t line;
	sk_ui_node_t fs_lbl;
	sk_ui_style_props_t col_style;
	u32 i;

	st->screen = ui->widget_view(ctx, root, "player-screen");
	if (!sk_ui_node_is_valid(st->screen)) {
		return -1;
	}
	player_style_screen(ui, ctx, st->screen);

	card = ui->widget_panel(ctx, st->screen, "player-card");
	player_style_card(ui, ctx, card);

	title = ui->widget_label(ctx, card, "Skore UI Widget Playground", "player-title");
	player_style_title(ui, ctx, title);

	st->status = ui->widget_label(ctx, card, "Hover and click widgets to test", "player-status");
	player_style_hint(ui, ctx, st->status);

	header = ui->widget_view(ctx, card, "player-header");
	player_style_row(ui, ctx, header);

	logo = ui->widget_image(ctx, header, 1, "player-logo");
	player_style_image(ui, ctx, logo);
	(void)logo;

	header_col = ui->widget_view(ctx, header, "player-header-col");
	memset(&col_style, 0, sizeof(col_style));
	col_style.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_FLEX_GROW | SK_UI_SP_ROW_GAP | SK_UI_SP_WIDTH;
	col_style.layout.flex_direction = SK_UI_FLEX_COLUMN;
	col_style.layout.flex_grow = 1.0f;
	col_style.layout.row_gap = 8.0f;
	col_style.layout.width = sk_ui_auto();
	(void)ui->node_set_inline_style(ctx, header_col, &col_style);

	st->name_input = ui->widget_text_input(ctx, header_col, "Player", "player-name");
	player_style_field(ui, ctx, st->name_input);

	vol_row = ui->widget_view(ctx, header_col, "player-vol-row");
	player_style_row(ui, ctx, vol_row);
	st->volume_label = ui->widget_label(ctx, vol_row, "Volume: 70%", "player-vol-lbl");
	player_style_hint_inline(ui, ctx, st->volume_label);
	st->slider = ui->widget_slider(ctx, vol_row, 0.0f, 1.0f, 0.7f, "player-volume");
	player_style_slider(ui, ctx, st->slider);
	(void)ui->slider_set_on_change(ctx, st->slider, player_on_slider, st);

	opts = ui->widget_view(ctx, card, "player-options");
	player_style_row(ui, ctx, opts);
	st->checkbox = ui->widget_checkbox(ctx, opts, 0, "player-fullscreen");
	fs_lbl = ui->widget_label(ctx, opts, "Fullscreen", "player-fs-lbl");
	player_style_hint_inline(ui, ctx, fs_lbl);
	(void)ui->checkbox_set_on_change(ctx, st->checkbox, player_on_checkbox, st);

	st->scroll = ui->widget_scroll_view(ctx, card, "player-log");
	player_style_scroll(ui, ctx, st->scroll);
	content = ui->scroll_view_content(ctx, st->scroll);
	(void)ui->scroll_view_set_content_size(ctx, st->scroll, 360.0f, 120.0f);
	for (i = 0u; i < 5u; ++i) {
		static const char* lines[5] = {
			"Tip: click Click Me and watch the counter", "Tip: drag the volume slider",		   "Tip: toggle Fullscreen checkbox",
			"Tip: scroll this list with the wheel",		 "Tip: click the name field and type",
		};
		char id[32];
		snprintf(id, sizeof(id), "player-log-%u", i);
		line = ui->widget_label(ctx, content, lines[i], id);
		player_style_hint(ui, ctx, line);
	}

	actions = ui->widget_view(ctx, card, "player-actions");
	player_style_row(ui, ctx, actions);
	st->btn_click = ui->widget_button(ctx, actions, "Click Me", "player-click");
	player_style_button(ui, ctx, st->btn_click);
	player_set_click(ui, ctx, st->btn_click, player_on_button_click, st);

	st->btn_reset = ui->widget_button(ctx, actions, "Reset", "player-reset");
	player_style_button(ui, ctx, st->btn_reset);
	player_set_click(ui, ctx, st->btn_reset, player_on_reset_click, st);

	st->clicks_label = ui->widget_label(ctx, card, "Clicks: 0", "player-clicks");
	player_style_hint(ui, ctx, st->clicks_label);
	return 0;
}

/* -------------------------------------------------------------------------- */
/* GPU: swapchain + UI renderer present                                       */
/* -------------------------------------------------------------------------- */

static sk_adapter_t player_select_adapter(const sk_render_device_api_t* api, sk_render_device_t dev) {
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

static void player_gpu_destroy_frame_targets(player_gpu_t* g) {
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

static i32 player_gpu_build_frame_targets(player_gpu_t* g) {
	u32 i;
	u32 count;

	player_gpu_destroy_frame_targets(g);

	count = g->api->get_swapchain_image_count(g->device, g->swapchain);
	if (count == 0u || count > PLAYER_MAX_SWAP_IMAGES) {
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

	/* Recreate render pass when format is known (first time or after recreate). */
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
		pass_desc.debug_name = "player-ui-pass";
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
			player_gpu_destroy_frame_targets(g);
			return -1;
		}
		memset(&vdesc, 0, sizeof(vdesc));
		vdesc.texture = image;
		vdesc.type = SK_TEXTURE_VIEW_TYPE_2D;
		vdesc.mip_level_count = 1u;
		vdesc.array_layer_count = 1u;
		vdesc.debug_name = "player-swap-view";
		g->views[i] = g->api->create_texture_view(g->device, &vdesc);
		if (!sk_texture_view_t_is_valid(g->views[i])) {
			player_gpu_destroy_frame_targets(g);
			return -1;
		}
		memset(&fb_desc, 0, sizeof(fb_desc));
		fb_desc.render_pass = g->render_pass;
		fb_desc.attachments = &g->views[i];
		fb_desc.attachment_count = 1u;
		fb_desc.debug_name = "player-swap-fb";
		g->framebuffers[i] = g->api->create_framebuffer(g->device, &fb_desc);
		if (!sk_framebuffer_t_is_valid(g->framebuffers[i])) {
			player_gpu_destroy_frame_targets(g);
			return -1;
		}
	}
	g->image_count = count;
	return 0;
}

static void player_gpu_wait_idle(player_gpu_t* g) {
	if (g->api == NULL || !sk_render_device_t_is_valid(g->device)) {
		return;
	}
	(void)g->api->wait_idle(g->device);
}

static void player_gpu_drain_acquire_sem(player_gpu_t* g) {
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

static void player_gpu_shutdown(player_ui_state_t* st) {
	player_gpu_t* g = &st->gpu;
	if (g->api == NULL) {
		return;
	}
	player_gpu_wait_idle(g);
	if (st->ui != NULL && g->ui_renderer != NULL) {
		st->ui->renderer_destroy(g->ui_renderer);
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
		player_gpu_destroy_frame_targets(g);
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
	g->api = NULL;
	g->ready = 0;
}

static i32 player_gpu_recreate_swapchain(player_ui_state_t* st, sk_window_t window, u32 width, u32 height) {
	player_gpu_t* g = &st->gpu;
	if (g->api == NULL || !sk_swapchain_t_is_valid(g->swapchain)) {
		return -1;
	}
	player_gpu_wait_idle(g);
	player_gpu_destroy_frame_targets(g);
	if (g->api->resize_swapchain(g->device, g->swapchain, width, height) != 0) {
		/* recreate from scratch */
		g->api->destroy_swapchain(g->device, g->swapchain);
		{
			sk_swapchain_desc_t sc;
			memset(&sc, 0, sizeof(sc));
			sc.window = window;
			sc.width = width;
			sc.height = height;
			sc.format = SK_PIXEL_FORMAT_UNKNOWN;
			sc.vsync = true;
			sc.debug_name = "player-swapchain";
			g->swapchain = g->api->create_swapchain(g->device, &sc);
		}
		if (!sk_swapchain_t_is_valid(g->swapchain)) {
			return -1;
		}
	}
	if (player_gpu_build_frame_targets(g) != 0) {
		return -1;
	}
	if (g->ui_renderer != NULL && st->ui != NULL) {
		if (st->ui->renderer_set_render_pass(g->ui_renderer, g->render_pass) != 0) {
			return -1;
		}
	}
	return 0;
}

static i32 player_gpu_init(sk_app_context_t* app_ctx, player_ui_state_t* st, const sk_platform_window_api_t* win_api, sk_window_t window) {
	const sk_app_api_t* app_api = sk_app_api();
	player_gpu_t* g = &st->gpu;
	sk_extent_t fb;
	sk_adapter_t adapter;
	sk_queue_desc_t q_desc;
	sk_swapchain_desc_t sc_desc;
	sk_command_buffer_desc_t cb_desc;
	sk_fence_desc_t f_desc;
	sk_semaphore_desc_t sem_desc;
	sk_ui_renderer_desc_t r_desc;

	memset(g, 0, sizeof(*g));
	g->api = (const sk_render_device_api_t*)app_api->get_api(app_ctx, SK_RENDER_DEVICE_API_TYPE_ID);
	g->dxc = (const sk_dxc_compiler_api_t*)app_api->get_api(app_ctx, SK_DXC_COMPILER_API_TYPE_ID);
	if (g->api == NULL || g->api->create_swapchain == NULL || g->api->present == NULL) {
		if (st->log != NULL) {
			sk_log_warn(sk_logger_api(), st->log, "render device API missing swapchain/present (need sk-vulkan-render-device)");
		}
		return -1;
	}
	if (g->dxc == NULL || g->dxc->init == NULL) {
		if (st->log != NULL) {
			sk_log_warn(sk_logger_api(), st->log, "dxc API missing (need sk-dxc-compiler)");
		}
		return -1;
	}
	if (g->dxc->init() != 0) {
		if (st->log != NULL) {
			sk_log_warn(sk_logger_api(), st->log, "dxc init failed");
		}
		return -1;
	}

	g->device = g->api->init(app_ctx, NULL);
	if (!sk_render_device_t_is_valid(g->device)) {
		if (st->log != NULL) {
			sk_log_warn(sk_logger_api(), st->log, "render device init failed (no GPU/ICD?)");
		}
		g->dxc->shutdown();
		g->dxc = NULL;
		return -1;
	}

	adapter = player_select_adapter(g->api, g->device);
	if (!sk_adapter_t_is_valid(adapter) || g->api->select_adapter(g->device, adapter) != 0) {
		if (st->log != NULL) {
			sk_log_warn(sk_logger_api(), st->log, "no suitable GPU adapter");
		}
		player_gpu_shutdown(st);
		return -1;
	}

	memset(&q_desc, 0, sizeof(q_desc));
	q_desc.queue_type = (u32)SK_QUEUE_TYPE_GRAPHICS;
	g->queue = g->api->create_queue(g->device, &q_desc);
	if (!sk_queue_t_is_valid(g->queue)) {
		player_gpu_shutdown(st);
		return -1;
	}

	fb = win_api->get_framebuffer_size(window);
	memset(&sc_desc, 0, sizeof(sc_desc));
	sc_desc.window = window;
	sc_desc.width = fb.width;
	sc_desc.height = fb.height;
	sc_desc.format = SK_PIXEL_FORMAT_UNKNOWN;
	sc_desc.vsync = true;
	sc_desc.debug_name = "player-swapchain";
	g->swapchain = g->api->create_swapchain(g->device, &sc_desc);
	if (!sk_swapchain_t_is_valid(g->swapchain)) {
		if (st->log != NULL) {
			sk_log_warn(sk_logger_api(), st->log, "create_swapchain failed");
		}
		player_gpu_shutdown(st);
		return -1;
	}

	if (player_gpu_build_frame_targets(g) != 0) {
		if (st->log != NULL) {
			sk_log_warn(sk_logger_api(), st->log, "swapchain frame targets failed");
		}
		player_gpu_shutdown(st);
		return -1;
	}

	memset(&cb_desc, 0, sizeof(cb_desc));
	cb_desc.level = SK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cb_desc.queue_type = (u32)SK_QUEUE_TYPE_GRAPHICS;
	cb_desc.debug_name = "player-ui-cmd";
	g->cmd = g->api->create_command_buffer(g->device, &cb_desc);
	if (!sk_command_buffer_t_is_valid(g->cmd)) {
		player_gpu_shutdown(st);
		return -1;
	}

	memset(&f_desc, 0, sizeof(f_desc));
	f_desc.debug_name = "player-ui-fence";
	g->fence = g->api->create_fence(g->device, &f_desc);
	if (!sk_fence_t_is_valid(g->fence)) {
		player_gpu_shutdown(st);
		return -1;
	}

	memset(&sem_desc, 0, sizeof(sem_desc));
	sem_desc.debug_name = "player-ui-acquire";
	g->acquire_sem = g->api->create_semaphore(g->device, &sem_desc);
	if (!sk_semaphore_t_is_valid(g->acquire_sem)) {
		player_gpu_shutdown(st);
		return -1;
	}

	memset(&r_desc, 0, sizeof(r_desc));
	r_desc.device_api = g->api;
	r_desc.device = g->device;
	r_desc.dxc = g->dxc;
	r_desc.render_pass = g->render_pass;
	g->ui_renderer = st->ui->renderer_create(&r_desc);
	if (g->ui_renderer == NULL) {
		if (st->log != NULL) {
			sk_log_warn(sk_logger_api(), st->log, "ui renderer_create failed (DXC/HLSL?)");
		}
		player_gpu_shutdown(st);
		return -1;
	}

	g->ready = 1;
	if (st->log != NULL) {
		sk_log_info(sk_logger_api(), st->log, "GPU present ready (%ux%u, %u images)", g->fb_w, g->fb_h, g->image_count);
	}
	return 0;
}

static void player_gpu_present(player_ui_state_t* st, const sk_platform_window_api_t* win_api, sk_window_t window) {
	player_gpu_t* g = &st->gpu;
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

	if (!g->ready || st->ui == NULL || st->ctx == NULL) {
		return;
	}

	fb = win_api->get_framebuffer_size(window);
	if (fb.width == 0u || fb.height == 0u) {
		return;
	}
	if (fb.width != g->fb_w || fb.height != g->fb_h) {
		if (player_gpu_recreate_swapchain(st, window, fb.width, fb.height) != 0) {
			if (st->log != NULL && (st->frame % 120u) == 0u) {
				sk_log_warn(sk_logger_api(), st->log, "swapchain recreate failed");
			}
			return;
		}
	}

	dl = st->ui->get_draw_list(st->ctx);
	if (dl == NULL) {
		return;
	}

	memset(&acq, 0, sizeof(acq));
	acq.swapchain = g->swapchain;
	acq.timeout_ns = UINT64_MAX;
	acq.signal_semaphore = g->acquire_sem;
	acq_rc = g->api->acquire_next_image(g->device, &acq, &image_index);
	if (acq_rc == SK_DEVICE_RESULT_SWAPCHAIN_OUT_OF_DATE) {
		(void)player_gpu_recreate_swapchain(st, window, fb.width, fb.height);
		return;
	}
	if (acq_rc != SK_DEVICE_RESULT_SUCCESS || image_index >= g->image_count) {
		if (acq_rc == SK_DEVICE_RESULT_SUCCESS) {
			player_gpu_drain_acquire_sem(g);
		}
		return;
	}

	g->api->reset_command_buffer(g->device, g->cmd);
	memset(&begin_info, 0, sizeof(begin_info));
	begin_info.usage_flags = (u32)SK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT;
	if (g->api->begin_command_buffer(g->device, g->cmd, &begin_info) != 0) {
		player_gpu_drain_acquire_sem(g);
		return;
	}

	memset(&prep, 0, sizeof(prep));
	prep.cmd = g->cmd;
	prep.draw_list = dl;
	prep.font_system = st->fonts;
	(void)st->ui->renderer_prepare(g->ui_renderer, &prep);

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
	(void)st->ui->renderer_encode(g->ui_renderer, &enc);

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
		player_gpu_drain_acquire_sem(g);
		return;
	}
	(void)g->api->wait_fences(g->device, &g->fence, 1u, true, UINT64_MAX);

	memset(&present, 0, sizeof(present));
	present.swapchain = g->swapchain;
	present.image_index = image_index;
	if (g->api->present(g->device, g->queue, &present) == SK_DEVICE_RESULT_SWAPCHAIN_OUT_OF_DATE) {
		(void)player_gpu_recreate_swapchain(st, window, fb.width, fb.height);
	}
}

/* -------------------------------------------------------------------------- */
/* UI lifecycle                                                               */
/* -------------------------------------------------------------------------- */

static void player_on_content_scale(sk_window_t window, sk_content_scale_t scale, void_ptr_t user_data) {
	player_ui_state_t* st = (player_ui_state_t*)user_data;
	(void)window;
	if (st == NULL || st->ui == NULL || st->ctx == NULL) {
		return;
	}
	(void)st->ui->node_mark_dirty(st->ctx, st->ui->context_root(st->ctx), (u32)SK_UI_DIRTY_PAINT);
	st->last_scale_x = scale.x;
	st->last_scale_y = scale.y;
}

static i32 player_has_arg(int argc, char* argv[], const_chr_t flag) {
	i32 i;
	for (i = 1; i < argc; ++i) {
		if (argv[i] != NULL && strcmp(argv[i], flag) == 0) {
			return 1;
		}
	}
	return 0;
}

static i32 player_dump_dock_layout(player_ui_state_t* st) {
	const sk_ui_api_t* ui = st->ui;
	sk_ui_context_t* ctx = st->ctx;
	sk_ui_rect_t space;
	sk_ui_rect_t left;
	sk_ui_rect_t center;
	sk_ui_rect_t right_top;
	sk_ui_rect_t right_bottom;
	f32 w = 0.0f;
	f32 h = 0.0f;

	ui->sample_dock_demo_logical_size(&w, &h);
	space.x = 0.0f;
	space.y = 0.0f;
	space.width = w;
	space.height = h;
	if (ui->dockspace_layout(ctx, ui->dockspace_find(ctx, "dock-demo"), &space) != 0) {
		return -1;
	}
	if (ui->dock_node_get_rect(ctx, ui->dock_find_node_for_window(ctx, "dock-demo-hierarchy"), &left) != 0) {
		return -1;
	}
	if (ui->dock_node_get_rect(ctx, ui->dock_find_node_for_window(ctx, "dock-demo-scene"), &center) != 0) {
		return -1;
	}
	if (ui->dock_node_get_rect(ctx, ui->dock_find_node_for_window(ctx, "dock-demo-inspector"), &right_top) != 0) {
		return -1;
	}
	if (ui->dock_node_get_rect(ctx, ui->dock_find_node_for_window(ctx, "dock-demo-console"), &right_bottom) != 0) {
		return -1;
	}
	/* One tagged line so two runs can be compared after stripping logs. */
	printf("dock-demo-layout: "
		   "{\"space\":[%.9g,%.9g,%.9g,%.9g],\"left\":[%.9g,%.9g,%.9g,%.9g],\"center\":[%.9g,%.9g,%.9g,%.9g],\"right_top\":[%.9g,%.9g,%.9g,%.9g],\"right_bottom\":[%.9g,%.9g,%.9g,%"
		   ".9g]}\n",
		   (double)space.x, (double)space.y, (double)space.width, (double)space.height, (double)left.x, (double)left.y, (double)left.width, (double)left.height, (double)center.x,
		   (double)center.y, (double)center.width, (double)center.height, (double)right_top.x, (double)right_top.y, (double)right_top.width, (double)right_top.height,
		   (double)right_bottom.x, (double)right_bottom.y, (double)right_bottom.width, (double)right_bottom.height);
	return 0;
}

static i32 player_ui_init(sk_app_context_t* app_ctx, player_ui_state_t* st) {
	const sk_app_api_t* app_api = sk_app_api();
	sk_logger_t* log = st->log;
	i32 dock_demo = st->dock_demo;

	memset(st, 0, sizeof(*st));
	st->log = log;
	st->dock_demo = dock_demo;
	st->last_scale_x = 1.0f;
	st->last_scale_y = 1.0f;

	st->ui = (const sk_ui_api_t*)app_api->get_api(app_ctx, SK_UI_API_TYPE_ID);
	if (st->ui == NULL) {
		return -1;
	}
	if (st->ui->init() != 0) {
		return -1;
	}

	st->ctx = st->ui->context_create(NULL);
	if (st->ctx == NULL) {
		return -1;
	}

	st->fonts = st->ui->font_system_create(NULL, 512u, 512u);
	if (st->fonts != NULL) {
		st->font = st->ui->font_load_memory(st->fonts, skore_test_font_ttf, (u32)sizeof(skore_test_font_ttf));
	}

	if (st->dock_demo != 0) {
		return sk_ui_node_is_valid(st->ui->sample_dock_demo_build(st->ctx, SK_UI_NODE_INVALID)) ? 0 : -1;
	}
	return player_ui_build(st);
}

static void player_ui_shutdown(player_ui_state_t* st) {
	player_gpu_shutdown(st);
	if (st->ui == NULL) {
		return;
	}
	if (st->font != NULL) {
		st->ui->font_destroy(st->font);
		st->font = NULL;
	}
	if (st->fonts != NULL) {
		st->ui->font_system_destroy(st->fonts);
		st->fonts = NULL;
	}
	if (st->ctx != NULL) {
		st->ui->context_destroy(st->ctx);
		st->ctx = NULL;
	}
	st->ui->shutdown();
	st->ui = NULL;
}

/* -------------------------------------------------------------------------- */
/* Platform input → sk-ui ingress                                             */
/* -------------------------------------------------------------------------- */

/**
 * Platform key code → sk-ui key code. Printable keys share ASCII on both
 * sides; the named keys are mapped explicitly so neither enum has to depend
 * on the other.
 */
static i32 player_map_key(i32 key) {
	switch (key) {
	case SK_KEY_BACKSPACE:
		return SK_UI_KEY_BACKSPACE;
	case SK_KEY_TAB:
		return SK_UI_KEY_TAB;
	case SK_KEY_ENTER:
		return SK_UI_KEY_ENTER;
	case SK_KEY_ESCAPE:
		return SK_UI_KEY_ESCAPE;
	case SK_KEY_SPACE:
		return SK_UI_KEY_SPACE;
	case SK_KEY_DELETE:
		return SK_UI_KEY_DELETE;
	case SK_KEY_LEFT:
		return SK_UI_KEY_LEFT;
	case SK_KEY_RIGHT:
		return SK_UI_KEY_RIGHT;
	case SK_KEY_UP:
		return SK_UI_KEY_UP;
	case SK_KEY_DOWN:
		return SK_UI_KEY_DOWN;
	case SK_KEY_HOME:
		return SK_UI_KEY_HOME;
	case SK_KEY_END:
		return SK_UI_KEY_END;
	default:
		/* Printable ASCII passes through; anything else stays unknown. */
		return (key >= 32 && key < 127) ? key : SK_UI_KEY_UNKNOWN;
	}
}

/** Platform modifier bits → sk-ui modifier bits. */
static u32 player_map_mods(u32 mods) {
	u32 out = (u32)SK_UI_MOD_NONE;
	if ((mods & (u32)SK_KEY_MOD_SHIFT) != 0u) {
		out |= (u32)SK_UI_MOD_SHIFT;
	}
	if ((mods & (u32)SK_KEY_MOD_CTRL) != 0u) {
		out |= (u32)SK_UI_MOD_CTRL;
	}
	if ((mods & (u32)SK_KEY_MOD_ALT) != 0u) {
		out |= (u32)SK_UI_MOD_ALT;
	}
	if ((mods & (u32)SK_KEY_MOD_SUPER) != 0u) {
		out |= (u32)SK_UI_MOD_SUPER;
	}
	return out;
}

static void player_on_key(sk_window_t window, i32 key, i32 down, i32 repeat, u32 mods, void_ptr_t user_data) {
	player_ui_state_t* st = (player_ui_state_t*)user_data;
	sk_ui_input_event_t ev;
	(void)window;

	if (st == NULL || st->ui == NULL || st->ctx == NULL) {
		return;
	}
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_KEY;
	ev.x = st->pointer_x;
	ev.y = st->pointer_y;
	ev.key = player_map_key(key);
	ev.down = down;
	ev.repeat = repeat;
	ev.mods = player_map_mods(mods);
	(void)st->ui->input_dispatch(st->ctx, &ev);
}

static void player_on_char(sk_window_t window, const_chr_t utf8, void_ptr_t user_data) {
	player_ui_state_t* st = (player_ui_state_t*)user_data;
	sk_ui_input_event_t ev;
	(void)window;

	if (st == NULL || st->ui == NULL || st->ctx == NULL || utf8 == NULL) {
		return;
	}
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_TEXT;
	ev.x = st->pointer_x;
	ev.y = st->pointer_y;
	ev.text = utf8;
	(void)st->ui->input_dispatch(st->ctx, &ev);
}

static void player_on_scroll(sk_window_t window, f32 offset_x, f32 offset_y, void_ptr_t user_data) {
	player_ui_state_t* st = (player_ui_state_t*)user_data;
	sk_ui_input_event_t ev;
	(void)window;

	if (st == NULL || st->ui == NULL || st->ctx == NULL) {
		return;
	}
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_WHEEL;
	ev.x = st->pointer_x;
	ev.y = st->pointer_y;
	ev.scroll_x = offset_x;
	ev.scroll_y = offset_y;
	(void)st->ui->input_dispatch(st->ctx, &ev);
}

static void player_ui_dispatch_mouse(player_ui_state_t* st, const sk_platform_window_api_t* win_api, sk_window_t window) {
	sk_ui_input_event_t ev;
	f32 x = 0.0f;
	f32 y = 0.0f;
	i32 down;
	i32 moved;
	i32 pressed;
	i32 released;

	if (st->ui == NULL || st->ctx == NULL || win_api->get_cursor_pos == NULL || win_api->get_mouse_button == NULL) {
		return;
	}

	win_api->get_cursor_pos(window, &x, &y);
	down = win_api->get_mouse_button(window, SK_MOUSE_BUTTON_LEFT);
	moved = (x > st->pointer_x + 1.0e-3f || x < st->pointer_x - 1.0e-3f || y > st->pointer_y + 1.0e-3f || y < st->pointer_y - 1.0e-3f) ? 1 : 0;
	pressed = (down != 0 && st->pointer_down == 0) ? 1 : 0;
	released = (down == 0 && st->pointer_down != 0) ? 1 : 0;

	if (moved != 0) {
		memset(&ev, 0, sizeof(ev));
		ev.kind = SK_UI_INPUT_POINTER_MOVE;
		ev.x = x;
		ev.y = y;
		(void)st->ui->input_dispatch(st->ctx, &ev);
	}
	if (pressed != 0) {
		memset(&ev, 0, sizeof(ev));
		ev.kind = SK_UI_INPUT_POINTER_BUTTON;
		ev.x = x;
		ev.y = y;
		ev.button = SK_UI_POINTER_BUTTON_LEFT;
		ev.down = 1;
		(void)st->ui->input_dispatch(st->ctx, &ev);
	}
	if (released != 0) {
		memset(&ev, 0, sizeof(ev));
		ev.kind = SK_UI_INPUT_POINTER_BUTTON;
		ev.x = x;
		ev.y = y;
		ev.button = SK_UI_POINTER_BUTTON_LEFT;
		ev.down = 0;
		(void)st->ui->input_dispatch(st->ctx, &ev);
	}

	st->pointer_x = x;
	st->pointer_y = y;
	st->pointer_down = down;
}

static void player_ui_frame(player_ui_state_t* st, const sk_platform_window_api_t* win_api, sk_window_t window) {
	sk_extent_t logical;
	sk_content_scale_t scale;
	sk_ui_paint_params_t paint;
	const sk_ui_draw_list_t* dl;
	f32 sx;
	f32 sy;

	if (st->ui == NULL || st->ctx == NULL) {
		return;
	}

	logical = win_api->get_window_size(window);
	scale = win_api->get_window_content_scale(window);
	sx = scale.x > 0.0f ? scale.x : 1.0f;
	sy = scale.y > 0.0f ? scale.y : 1.0f;

	if (sx > st->last_scale_x + 1.0e-4f || sx < st->last_scale_x - 1.0e-4f || sy > st->last_scale_y + 1.0e-4f || sy < st->last_scale_y - 1.0e-4f) {
		(void)st->ui->node_mark_dirty(st->ctx, st->ui->context_root(st->ctx), (u32)SK_UI_DIRTY_PAINT);
		st->last_scale_x = sx;
		st->last_scale_y = sy;
	}

	player_ui_dispatch_mouse(st, win_api, window);

	(void)st->ui->style_resolve(st->ctx);
	(void)st->ui->layout(st->ctx, (f32)logical.width, (f32)logical.height);
	(void)st->ui->layout_apply_scale(st->ctx, sx, sy);

	memset(&paint, 0, sizeof(paint));
	paint.font_system = st->fonts;
	paint.font = st->font;
	(void)st->ui->paint(st->ctx, &paint);

	player_gpu_present(st, win_api, window);

	st->frame += 1u;
	if (st->log != NULL && (st->frame % 120u) == 0u) {
		dl = st->ui->get_draw_list(st->ctx);
		sk_log_info(sk_logger_api(), st->log, "ui frame %u: verts=%u cmds=%u reused=%d clicks=%u gpu=%d", st->frame, dl != NULL ? dl->vertex_count : 0u,
					dl != NULL ? dl->command_count : 0u, dl != NULL ? dl->reused : 0, st->click_count, st->gpu.ready);
	}
}

/**
 * Attach a rotating file sink under {app_folder}/logs/player.log.
 * Parent dir is created if missing. Returns NULL if setup fails (stdout still works).
 */
static sk_log_file_sink_t* player_attach_file_log(const sk_logger_api_t* logger_api) {
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	char app_dir[SK_FS_PATH_MAX];
	char logs_dir[SK_FS_PATH_MAX];
	char log_path[SK_FS_PATH_MAX];
	sk_log_file_sink_t* file_sink;

	if (fs->app_folder(app_dir, (u32)sizeof(app_dir)) != 0 || app_dir[0] == '\0') {
		return NULL;
	}
	if (sk_path_join(sk_str_view_cstr(app_dir), sk_str_view_cstr("logs"), logs_dir, (u32)sizeof(logs_dir)) < 0) {
		return NULL;
	}
	if (fs->create_directory(logs_dir) != 0 && fs->get_file_status(logs_dir) != SK_FILE_STATUS_DIRECTORY) {
		return NULL;
	}
	if (sk_path_join(sk_str_view_cstr(logs_dir), sk_str_view_cstr("player.log"), log_path, (u32)sizeof(log_path)) < 0) {
		return NULL;
	}

	file_sink = sk_log_file_sink_create(log_path, (u64)SK_LOG_FILE_SINK_DEFAULT_MAX_BYTES, (u32)SK_LOG_FILE_SINK_DEFAULT_MAX_FILES);
	if (file_sink == NULL) {
		return NULL;
	}
	if (logger_api->add_sink(sk_log_file_sink_sink(file_sink)) != 0) {
		sk_log_file_sink_destroy(file_sink);
		return NULL;
	}
	return file_sink;
}

int main(int argc, char* argv[]) {
	sk_app_context_t* ctx = sk_app_init(argc, argv);
	const sk_app_api_t* app_api;
	const sk_platform_window_api_t* win_api;
	const sk_render_graph_api_t* rg_api;
	const sk_logger_api_t* logger_api;
	const sk_profiler_api_t* prof_api;
	sk_window_t window;
	player_ui_state_t ui_state;
	sk_log_file_sink_t* file_sink = NULL;
	i32 dock_demo = player_has_arg(argc, argv, "--dock-demo");
	i32 dump_layout = player_has_arg(argc, argv, "--dump-layout");
	u32 win_w = dock_demo != 0 ? 1280u : 960u;
	u32 win_h = dock_demo != 0 ? 720u : 640u;
	u32 win_flags = dock_demo != 0 ? (u32)SK_WINDOW_FLAG_NONE : (u32)SK_WINDOW_FLAG_RESIZABLE;
	const_chr_t win_title = dock_demo != 0 ? "Skore — Docking Demo" : "Skore — UI Widget Playground";

	if (ctx == NULL) {
		return 1;
	}

	if (dock_demo != 0 && dump_layout != 0) {
		i32 rc;
		(void)sk_logger_api()->remove_sink(sk_logger_stdout_sink());
		memset(&ui_state, 0, sizeof(ui_state));
		ui_state.dock_demo = 1;
		rc = player_ui_init(ctx, &ui_state);
		if (rc == 0) {
			rc = player_dump_dock_layout(&ui_state);
		}
		player_ui_shutdown(&ui_state);
		sk_app_destroy(ctx);
		return rc == 0 ? 0 : 1;
	}

	/* Plugins auto-loaded from {app_folder}/plugins (window, ui, render_graph, …).
	 * The profiler table is optional: lifecycle (init/begin_frame/end_frame)
	 * is host-driven by sk-app when the plugin is present, and the zone macro
	 * below compiles to a no-op unless SK_ENABLE_PROFILER is on. */
	app_api = sk_app_api();
	logger_api = sk_logger_api();
	file_sink = player_attach_file_log(logger_api);
	win_api = app_api->get_api(ctx, SK_PLATFORM_WINDOW_API_TYPE_ID);
	rg_api = sk_render_graph_api_from_app(ctx, app_api);
	prof_api = app_api->get_api(ctx, SK_PROFILER_API_TYPE_ID);

	if (win_api == NULL || win_api->init() != 0) {
		if (file_sink != NULL) {
			(void)logger_api->remove_sink(sk_log_file_sink_sink(file_sink));
			sk_log_file_sink_destroy(file_sink);
		}
		sk_app_destroy(ctx);
		return 1;
	}

	if (rg_api == NULL || rg_api->create == NULL || rg_api->begin == NULL || rg_api->execute == NULL) {
		if (file_sink != NULL) {
			(void)logger_api->remove_sink(sk_log_file_sink_sink(file_sink));
			sk_log_file_sink_destroy(file_sink);
		}
		sk_app_destroy(ctx);
		return 1;
	}
	if (rg_api->init() != 0) {
		if (file_sink != NULL) {
			(void)logger_api->remove_sink(sk_log_file_sink_sink(file_sink));
			sk_log_file_sink_destroy(file_sink);
		}
		sk_app_destroy(ctx);
		return 1;
	}

	window = win_api->create_window(win_title, win_w, win_h, win_flags);
	if (window == NULL) {
		rg_api->shutdown();
		if (file_sink != NULL) {
			(void)logger_api->remove_sink(sk_log_file_sink_sink(file_sink));
			sk_log_file_sink_destroy(file_sink);
		}
		sk_app_destroy(ctx);
		return 1;
	}

	memset(&ui_state, 0, sizeof(ui_state));
	ui_state.dock_demo = dock_demo;
	ui_state.log = logger_api->create_logger("player-ui");

	if (player_ui_init(ctx, &ui_state) == 0) {
		sk_content_scale_t sc = win_api->get_window_content_scale(window);
		ui_state.last_scale_x = sc.x > 0.0f ? sc.x : 1.0f;
		ui_state.last_scale_y = sc.y > 0.0f ? sc.y : 1.0f;
		win_api->set_window_content_scale_callback(window, player_on_content_scale, &ui_state);
		win_api->set_window_key_callback(window, player_on_key, &ui_state);
		win_api->set_window_char_callback(window, player_on_char, &ui_state);
		win_api->set_window_scroll_callback(window, player_on_scroll, &ui_state);
		if (player_gpu_init(ctx, &ui_state, win_api, window) != 0 && ui_state.log != NULL) {
			sk_log_warn(logger_api, ui_state.log, "GPU present unavailable — CPU UI still runs (window stays blank)");
		}
		if (ui_state.log != NULL) {
			sk_extent_t logical = win_api->get_window_size(window);
			sk_extent_t physical = win_api->get_framebuffer_size(window);
			sk_log_info(logger_api, ui_state.log,
						dock_demo != 0 ? "docking demo ready (fixed 1280x720 layout, no persist)" : "widget playground ready (mouse/keyboard/wheel → sk-ui)");
			/* logical × scale must equal physical, or layout misses the window. */
			sk_log_info(logger_api, ui_state.log, "window: logical=%ux%u physical=%ux%u scale=%.2fx%.2f", logical.width, logical.height, physical.width, physical.height,
						(double)ui_state.last_scale_x, (double)ui_state.last_scale_y);
			if (file_sink != NULL) {
				sk_log_info(logger_api, ui_state.log, "file log sink active under app_folder/logs/player.log");
			}
		}
	} else if (ui_state.log != NULL) {
		sk_log_warn(logger_api, ui_state.log, "ui init failed (is sk-ui plugin in plugins/?)");
	}

	while (sk_app_tick(ctx)) {
		SK_PROFILE_CPU_ZONE(prof_api, "player frame");
		SK_PROFILE_CPU_ZONE(prof_api, "poll events");
		win_api->poll_events();
		player_ui_frame(&ui_state, win_api, window);

		if (win_api->window_should_close(window)) {
			app_api->request_shutdown(ctx);
		}
	}

	player_ui_shutdown(&ui_state);
	if (ui_state.log != NULL) {
		logger_api->destroy_logger(ui_state.log);
		ui_state.log = NULL;
	}
	if (file_sink != NULL) {
		(void)logger_api->remove_sink(sk_log_file_sink_sink(file_sink));
		sk_log_file_sink_destroy(file_sink);
		file_sink = NULL;
	}
	rg_api->shutdown();
	sk_app_destroy(ctx);
	return 0;
}
