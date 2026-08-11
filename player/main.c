/* Entry point for the exported game (player host).
 *
 * Runs the normal application loop and hosts the sk-ui sample main menu as an
 * in-game UI consumer (APX-138):
 *   poll_events → content scale → style_resolve → layout → apply_scale → paint
 *
 * Layout uses logical window size; HiDPI content scale is applied after layout
 * and drives glyph re-rasterization. GPU present is not required for this
 * sample — the CPU pipeline matches what a full renderer would encode.
 */

#include "app.h"
#include "platform_window.h"
#include "ui.h"

#include <stddef.h>
#include <string.h>

typedef struct player_ui_state_t {
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_ui_font_system_t* fonts;
	sk_ui_font_t* font;
	sk_ui_node_t menu;
	f32 last_scale_x;
	f32 last_scale_y;
} player_ui_state_t;

static void player_on_content_scale(sk_window_t window, sk_content_scale_t scale, void_ptr_t user_data) {
	player_ui_state_t* st = (player_ui_state_t*)user_data;
	(void)window;
	if (st == NULL || st->ui == NULL || st->ctx == NULL) {
		return;
	}
	/* Mark paint dirty so the next frame re-applies scale and re-rasterizes. */
	(void)st->ui->node_mark_dirty(st->ctx, st->ui->context_root(st->ctx), (u32)SK_UI_DIRTY_PAINT);
	st->last_scale_x = scale.x;
	st->last_scale_y = scale.y;
}

static i32 player_ui_init(sk_app_context_t* app_ctx, player_ui_state_t* st) {
	const sk_app_api_t* app_api = sk_app_api();
	sk_ui_node_t root;

	memset(st, 0, sizeof(*st));
	st->last_scale_x = 1.0f;
	st->last_scale_y = 1.0f;

	/* sk-ui is auto-loaded from {app_folder}/plugins when present. */
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

	/* Optional embedded fixture font for crisp HiDPI text in the sample. */
	st->fonts = st->ui->font_system_create(NULL, 512u, 512u);
	if (st->fonts != NULL) {
		/* Hosts normally load a project TTF; sample runs without a file. */
	}

	root = st->ui->context_root(st->ctx);
	st->menu = st->ui->sample_menu_build(st->ctx, root);
	if (!sk_ui_node_is_valid(st->menu)) {
		return -1;
	}
	return 0;
}

static void player_ui_shutdown(player_ui_state_t* st) {
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

static void player_ui_frame(player_ui_state_t* st, const sk_platform_window_api_t* win_api, sk_window_t window) {
	sk_extent_t logical;
	sk_content_scale_t scale;
	sk_ui_paint_params_t paint;
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

	/* Normal host pipeline (same order as harness_step, with real window size). */
	(void)st->ui->style_resolve(st->ctx);
	(void)st->ui->layout(st->ctx, (f32)logical.width, (f32)logical.height);
	(void)st->ui->layout_apply_scale(st->ctx, sx, sy);

	memset(&paint, 0, sizeof(paint));
	paint.font_system = st->fonts;
	paint.font = st->font;
	(void)st->ui->paint(st->ctx, &paint);
	/* GPU encode/present would consume st->ui->get_draw_list(st->ctx) here. */
}

int main(int argc, char* argv[]) {
	sk_app_context_t* ctx = sk_app_init(argc, argv);
	const sk_app_api_t* app_api;
	const sk_platform_window_api_t* win_api;
	sk_window_t window;
	player_ui_state_t ui_state;

	if (ctx == NULL) {
		return 1;
	}

	/* Platform window plugin is auto-loaded from {app_folder}/plugins. */
	app_api = sk_app_api();
	win_api = app_api->get_api(ctx, SK_PLATFORM_WINDOW_API_TYPE_ID);

	if (win_api->init() != 0) {
		sk_app_destroy(ctx);
		return 1;
	}

	window = win_api->create_window("Skore", 1280u, 720u, (u32)(SK_WINDOW_FLAG_RESIZABLE | SK_WINDOW_FLAG_MAXIMIZED));
	if (window == NULL) {
		sk_app_destroy(ctx);
		return 1;
	}

	if (player_ui_init(ctx, &ui_state) == 0) {
		sk_content_scale_t sc = win_api->get_window_content_scale(window);
		ui_state.last_scale_x = sc.x > 0.0f ? sc.x : 1.0f;
		ui_state.last_scale_y = sc.y > 0.0f ? sc.y : 1.0f;
		win_api->set_window_content_scale_callback(window, player_on_content_scale, &ui_state);
	}

	while (sk_app_tick(ctx)) {
		win_api->poll_events();
		player_ui_frame(&ui_state, win_api, window);

		if (win_api->window_should_close(window)) {
			app_api->request_shutdown(ctx);
		}
	}

	player_ui_shutdown(&ui_state);
	sk_app_destroy(ctx);
	return 0;
}
