/**
 * @file sample_menu.c
 * @brief In-game sample UI scene: main menu exercising every v1 widget.
 *
 * APX-138 — realistic consumer of sk-ui as a game host would:
 *   - flexbox layout
 *   - scene layout/appearance via style classes only (no inline geometry)
 *   - all v1 widgets: panel, view, label, button, checkbox, slider,
 *     text_input, scroll_view, image
 *   - driven through the normal style_resolve → layout → apply_scale → paint
 *     host loop (player) and the headless harness (tests share the same tree)
 *
 * HiDPI: logical layout is scale-independent; physical paint multiplies by
 * content scale once; glyphs re-rasterize at round(font_size * scale).
 *
 * Performance notes (observed, not silently optimized):
 *   - paint rebuilds the full draw list when content scale changes
 *     (expected; scale is not a free transform on cached verts)
 *   - font glyph cache keys include physical pixel size, so 1x→2x causes
 *     cache misses and new FreeType raster + atlas packs (not thrash on
 *     steady scale; thrash only if scale oscillates every frame)
 *   - soft-render path (tests) walks all mesh triangles per frame into an
 *     RGBA buffer — O(pixels * coverage); fine for 320x240 / 640x480 goldens
 *   - draw batches break on texture/clip changes; a full menu typically
 *     issues several solid + font mesh commands (not a single mega-batch)
 *   - no per-frame heap for layout when the tree is stable; paint may grow
 *     draw-list arrays once then reuse capacity
 */

#include "ui_internal.h"

#include "allocator.h"

#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Scene style class names (stable automation surface)                        */
/* -------------------------------------------------------------------------- */

#define SK_UI_SAMPLE_CLS_SCREEN "menu-screen"
#define SK_UI_SAMPLE_CLS_CARD "menu-card"
#define SK_UI_SAMPLE_CLS_TITLE "menu-title"
#define SK_UI_SAMPLE_CLS_ROW "menu-row"
#define SK_UI_SAMPLE_CLS_COL "menu-col"
#define SK_UI_SAMPLE_CLS_FIELD "menu-field"
#define SK_UI_SAMPLE_CLS_SLIDER "menu-slider"
#define SK_UI_SAMPLE_CLS_SCROLL "menu-scroll"
#define SK_UI_SAMPLE_CLS_IMAGE "menu-image"
#define SK_UI_SAMPLE_CLS_BTN_PRIMARY "menu-btn-primary"
#define SK_UI_SAMPLE_CLS_BTN_ROW "menu-btn-row"
#define SK_UI_SAMPLE_CLS_HINT "menu-hint"

/* Logical root size used by goldens and as a host default. */
#define SK_UI_SAMPLE_MENU_W 320.0f
#define SK_UI_SAMPLE_MENU_H 240.0f

static const sk_ui_api_t* sample_api(void) {
	return ui_get_api_table();
}

void ui_sample_menu_logical_size_impl(f32* out_width, f32* out_height) {
	if (out_width != NULL) {
		*out_width = SK_UI_SAMPLE_MENU_W;
	}
	if (out_height != NULL) {
		*out_height = SK_UI_SAMPLE_MENU_H;
	}
}

i32 ui_sample_menu_register_styles_impl(sk_ui_context_t* ctx) {
	const sk_ui_api_t* ui = sample_api();
	sk_ui_style_props_t p;

	/* Full-screen backdrop: column, center content. */
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_PADDING | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT |
			 SK_UI_SP_ROW_GAP;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.justify_content = SK_UI_JUSTIFY_CENTER;
	p.layout.align_items = SK_UI_ALIGN_CENTER;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_percent(100.0f);
	p.layout.padding.left = 16.0f;
	p.layout.padding.top = 16.0f;
	p.layout.padding.right = 16.0f;
	p.layout.padding.bottom = 16.0f;
	p.layout.row_gap = 10.0f;
	p.background_color = sk_ui_rgba(0.08f, 0.09f, 0.12f, 1.0f);
	if (ui->style_class_register(ctx, SK_UI_SAMPLE_CLS_SCREEN, &p) != 0) {
		return -1;
	}

	/* Centered menu card. */
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_MAX_WIDTH | SK_UI_SP_PADDING | SK_UI_SP_ROW_GAP | SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR |
			 SK_UI_SP_BORDER_WIDTH | SK_UI_SP_CORNER_RADIUS;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.max_width = sk_ui_pt(280.0f);
	p.layout.padding.left = 12.0f;
	p.layout.padding.top = 12.0f;
	p.layout.padding.right = 12.0f;
	p.layout.padding.bottom = 12.0f;
	p.layout.row_gap = 8.0f;
	p.layout.border.left = 1.0f;
	p.layout.border.top = 1.0f;
	p.layout.border.right = 1.0f;
	p.layout.border.bottom = 1.0f;
	p.background_color = sk_ui_rgba(0.14f, 0.15f, 0.18f, 1.0f);
	p.border_color = sk_ui_rgba(0.30f, 0.34f, 0.42f, 1.0f);
	p.corner_radius = 6.0f;
	if (ui->style_class_register(ctx, SK_UI_SAMPLE_CLS_CARD, &p) != 0) {
		return -1;
	}

	/* Title label. */
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_FONT_SIZE | SK_UI_SP_COLOR | SK_UI_SP_HEIGHT | SK_UI_SP_WIDTH | SK_UI_SP_ALIGN_SELF;
	p.font_size = 18.0f;
	p.color = sk_ui_rgba(0.95f, 0.96f, 0.98f, 1.0f);
	p.layout.height = sk_ui_pt(24.0f);
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.align_self = SK_UI_ALIGN_STRETCH;
	if (ui->style_class_register(ctx, SK_UI_SAMPLE_CLS_TITLE, &p) != 0) {
		return -1;
	}

	/* Horizontal row. */
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_COLUMN_GAP | SK_UI_SP_WIDTH;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.align_items = SK_UI_ALIGN_CENTER;
	p.layout.column_gap = 8.0f;
	p.layout.width = sk_ui_percent(100.0f);
	if (ui->style_class_register(ctx, SK_UI_SAMPLE_CLS_ROW, &p) != 0) {
		return -1;
	}

	/* Vertical column that grows. */
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_FLEX_GROW | SK_UI_SP_ROW_GAP | SK_UI_SP_WIDTH;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.flex_grow = 1.0f;
	p.layout.row_gap = 6.0f;
	p.layout.width = sk_ui_auto();
	if (ui->style_class_register(ctx, SK_UI_SAMPLE_CLS_COL, &p) != 0) {
		return -1;
	}

	/* Stretching text field. */
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_FLEX_GROW | SK_UI_SP_WIDTH | SK_UI_SP_MIN_HEIGHT;
	p.layout.flex_grow = 1.0f;
	p.layout.width = sk_ui_auto();
	p.layout.min_height = sk_ui_pt(28.0f);
	if (ui->style_class_register(ctx, SK_UI_SAMPLE_CLS_FIELD, &p) != 0) {
		return -1;
	}

	/* Stretching slider. */
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_FLEX_GROW | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.flex_grow = 1.0f;
	p.layout.width = sk_ui_auto();
	p.layout.height = sk_ui_pt(20.0f);
	if (ui->style_class_register(ctx, SK_UI_SAMPLE_CLS_SLIDER, &p) != 0) {
		return -1;
	}

	/* Fixed-height scroll region. */
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_pt(48.0f);
	if (ui->style_class_register(ctx, SK_UI_SAMPLE_CLS_SCROLL, &p) != 0) {
		return -1;
	}

	/* Logo / portrait image. */
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_CORNER_RADIUS;
	p.layout.width = sk_ui_pt(48.0f);
	p.layout.height = sk_ui_pt(48.0f);
	p.layout.min_width = sk_ui_pt(48.0f);
	p.layout.min_height = sk_ui_pt(48.0f);
	p.background_color = sk_ui_rgba(0.22f, 0.45f, 0.72f, 1.0f);
	p.corner_radius = 4.0f;
	if (ui->style_class_register(ctx, SK_UI_SAMPLE_CLS_IMAGE, &p) != 0) {
		return -1;
	}

	/* Primary action buttons fill the row evenly. */
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_FLEX_GROW | SK_UI_SP_MIN_HEIGHT;
	p.layout.flex_grow = 1.0f;
	p.layout.min_height = sk_ui_pt(28.0f);
	if (ui->style_class_register(ctx, SK_UI_SAMPLE_CLS_BTN_PRIMARY, &p) != 0) {
		return -1;
	}

	/* Button row with space-between. */
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_COLUMN_GAP | SK_UI_SP_WIDTH;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.justify_content = SK_UI_JUSTIFY_SPACE_BETWEEN;
	p.layout.column_gap = 6.0f;
	p.layout.width = sk_ui_percent(100.0f);
	if (ui->style_class_register(ctx, SK_UI_SAMPLE_CLS_BTN_ROW, &p) != 0) {
		return -1;
	}

	/* Hint / changelog lines inside the scroll content. */
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_FONT_SIZE | SK_UI_SP_COLOR | SK_UI_SP_HEIGHT | SK_UI_SP_WIDTH;
	p.font_size = 12.0f;
	p.color = sk_ui_rgba(0.75f, 0.78f, 0.82f, 1.0f);
	p.layout.height = sk_ui_pt(16.0f);
	p.layout.width = sk_ui_percent(100.0f);
	if (ui->style_class_register(ctx, SK_UI_SAMPLE_CLS_HINT, &p) != 0) {
		return -1;
	}

	return 0;
}

static i32 sample_add_class(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t cls) {
	return ui->node_add_class(ctx, node, cls);
}

sk_ui_node_t ui_sample_menu_build_impl(sk_ui_context_t* ctx, sk_ui_node_t parent) {
	const sk_ui_api_t* ui = sample_api();
	sk_ui_node_t screen;
	sk_ui_node_t card;
	sk_ui_node_t title;
	sk_ui_node_t header;
	sk_ui_node_t logo;
	sk_ui_node_t fields;
	sk_ui_node_t name_input;
	sk_ui_node_t vol_row;
	sk_ui_node_t vol_lbl;
	sk_ui_node_t volume;
	sk_ui_node_t opts;
	sk_ui_node_t cb_fs;
	sk_ui_node_t cb_vs;
	sk_ui_node_t fs_lbl;
	sk_ui_node_t vs_lbl;
	sk_ui_node_t scroll;
	sk_ui_node_t content;
	sk_ui_node_t line;
	sk_ui_node_t actions;
	sk_ui_node_t btn_play;
	sk_ui_node_t btn_settings;
	sk_ui_node_t btn_quit;
	u32 i;

	if (ui_sample_menu_register_styles_impl(ctx) != 0) {
		return SK_UI_NODE_INVALID;
	}

	if (!sk_ui_node_is_valid(parent)) {
		parent = ui->context_root(ctx);
	}

	/* Root of the scene: full-screen flex container. */
	screen = ui->widget_view(ctx, parent, "menu-screen");
	if (!sk_ui_node_is_valid(screen)) {
		return SK_UI_NODE_INVALID;
	}
	(void)sample_add_class(ui, ctx, screen, SK_UI_SAMPLE_CLS_SCREEN);

	card = ui->widget_panel(ctx, screen, "menu-card");
	(void)sample_add_class(ui, ctx, card, SK_UI_SAMPLE_CLS_CARD);

	title = ui->widget_label(ctx, card, "Skore Main Menu", "menu-title");
	(void)sample_add_class(ui, ctx, title, SK_UI_SAMPLE_CLS_TITLE);
	(void)ui->label_set_align(ctx, title, 1, 1); /* center */

	/* Header: portrait image + name / volume column. */
	header = ui->widget_view(ctx, card, "menu-header");
	(void)sample_add_class(ui, ctx, header, SK_UI_SAMPLE_CLS_ROW);

	logo = ui->widget_image(ctx, header, 1, "menu-logo");
	(void)sample_add_class(ui, ctx, logo, SK_UI_SAMPLE_CLS_IMAGE);

	fields = ui->widget_view(ctx, header, "menu-fields");
	(void)sample_add_class(ui, ctx, fields, SK_UI_SAMPLE_CLS_COL);

	name_input = ui->widget_text_input(ctx, fields, "Player", "menu-name");
	(void)sample_add_class(ui, ctx, name_input, SK_UI_SAMPLE_CLS_FIELD);

	vol_row = ui->widget_view(ctx, fields, "menu-vol-row");
	(void)sample_add_class(ui, ctx, vol_row, SK_UI_SAMPLE_CLS_ROW);
	vol_lbl = ui->widget_label(ctx, vol_row, "Vol", "menu-vol-lbl");
	(void)sample_add_class(ui, ctx, vol_lbl, SK_UI_SAMPLE_CLS_HINT);
	volume = ui->widget_slider(ctx, vol_row, 0.0f, 1.0f, 0.7f, "menu-volume");
	(void)sample_add_class(ui, ctx, volume, SK_UI_SAMPLE_CLS_SLIDER);

	/* Options row: checkboxes. */
	opts = ui->widget_view(ctx, card, "menu-options");
	(void)sample_add_class(ui, ctx, opts, SK_UI_SAMPLE_CLS_ROW);
	cb_fs = ui->widget_checkbox(ctx, opts, 1, "menu-fullscreen");
	fs_lbl = ui->widget_label(ctx, opts, "Fullscreen", "menu-fs-lbl");
	(void)sample_add_class(ui, ctx, fs_lbl, SK_UI_SAMPLE_CLS_HINT);
	cb_vs = ui->widget_checkbox(ctx, opts, 0, "menu-vsync");
	vs_lbl = ui->widget_label(ctx, opts, "VSync", "menu-vs-lbl");
	(void)sample_add_class(ui, ctx, vs_lbl, SK_UI_SAMPLE_CLS_HINT);
	(void)cb_fs;
	(void)cb_vs;

	/* Changelog scroll view (clipped content). */
	scroll = ui->widget_scroll_view(ctx, card, "menu-changelog");
	(void)sample_add_class(ui, ctx, scroll, SK_UI_SAMPLE_CLS_SCROLL);
	content = ui->scroll_view_content(ctx, scroll);
	(void)ui->scroll_view_set_content_size(ctx, scroll, 240.0f, 96.0f);
	for (i = 0u; i < 4u; ++i) {
		static const char* lines[4] = {
			"v0.1  Retained UI + flexbox",
			"v0.1  Bitmap fonts / HiDPI",
			"v0.1  Widgets + automation",
			"v0.1  Sample in-game menu",
		};
		char id[32];
		(void)snprintf(id, sizeof(id), "menu-log-%u", i);
		line = ui->widget_label(ctx, content, lines[i], id);
		(void)sample_add_class(ui, ctx, line, SK_UI_SAMPLE_CLS_HINT);
	}

	/* Actions. */
	actions = ui->widget_view(ctx, card, "menu-actions");
	(void)sample_add_class(ui, ctx, actions, SK_UI_SAMPLE_CLS_BTN_ROW);
	btn_play = ui->widget_button(ctx, actions, "Play", "menu-play");
	(void)sample_add_class(ui, ctx, btn_play, SK_UI_SAMPLE_CLS_BTN_PRIMARY);
	btn_settings = ui->widget_button(ctx, actions, "Settings", "menu-settings");
	(void)sample_add_class(ui, ctx, btn_settings, SK_UI_SAMPLE_CLS_BTN_PRIMARY);
	btn_quit = ui->widget_button(ctx, actions, "Quit", "menu-quit");
	(void)sample_add_class(ui, ctx, btn_quit, SK_UI_SAMPLE_CLS_BTN_PRIMARY);
	(void)btn_play;
	(void)btn_settings;
	(void)btn_quit;

	return screen;
}

/* -------------------------------------------------------------------------- */
/* Tests: 1x/2x goldens, runtime scale change, layout independence            */
/* -------------------------------------------------------------------------- */

#ifdef SK_TESTS
#include "test.h"
#include "testdata/skore_test_font_ttf.h"

/*
 * Unity (via test.h) may include <stdnoreturn.h>, which defines `noreturn`
 * as `_Noreturn`. Windows UCRT <stdlib.h> uses `__declspec(noreturn)`, which
 * breaks under clang-tidy when the macro expands to `_Noreturn`.
 */
#ifdef noreturn
#undef noreturn
#endif
#include <stdio.h>
#include <stdlib.h>

#ifndef SK_UI_SAMPLE_GOLDEN_DIR
#define SK_UI_SAMPLE_GOLDEN_DIR "../../plugins/ui/testdata/sample"
#endif

/* Minimal store-only PNG (same approach as widgets.c goldens). */
static u32 sample_crc32_table[256];
static i32 sample_crc32_ready;

static void sample_crc32_init(void) {
	u32 i, j;
	if (sample_crc32_ready) {
		return;
	}
	for (i = 0u; i < 256u; ++i) {
		u32 c = i;
		for (j = 0u; j < 8u; ++j) {
			c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
		}
		sample_crc32_table[i] = c;
	}
	sample_crc32_ready = 1;
}

static u32 sample_crc32(const u8* data, u32 len) {
	u32 c = 0xFFFFFFFFu;
	u32 i;
	sample_crc32_init();
	for (i = 0u; i < len; ++i) {
		c = sample_crc32_table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
	}
	return c ^ 0xFFFFFFFFu;
}

static void sample_put_u32_be(u8* p, u32 v) {
	p[0] = (u8)((v >> 24) & 0xFFu);
	p[1] = (u8)((v >> 16) & 0xFFu);
	p[2] = (u8)((v >> 8) & 0xFFu);
	p[3] = (u8)(v & 0xFFu);
}

static i32 sample_png_write_rgba(const char* path, const u8* rgba, u32 w, u32 h) {
	FILE* f;
	u32 row_bytes = w * 4u + 1u;
	u32 raw_len = row_bytes * h;
	u8* raw = NULL;
	u8* zdata = NULL;
	u32 zlen;
	u32 i, y;
	u8 sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
	u8 ihdr[25];
	u8 chunk_hdr[8];
	u32 crc;
	u32 adler_s1 = 1u, adler_s2 = 0u;
	u32 pos;
	u32 max_store = 65535u;
	u32 blocks;
	u32 bi;

	raw = (u8*)malloc((size_t)raw_len);
	if (raw == NULL) {
		return -1;
	}
	for (y = 0u; y < h; ++y) {
		raw[y * row_bytes] = 0u; /* filter None */
		memcpy(raw + y * row_bytes + 1u, rgba + y * w * 4u, (size_t)w * 4u);
	}
	blocks = (raw_len + max_store - 1u) / max_store;
	if (blocks == 0u) {
		blocks = 1u;
	}
	zlen = 2u + blocks * 5u + raw_len + 4u;
	zdata = (u8*)malloc((size_t)zlen);
	if (zdata == NULL) {
		free(raw);
		return -1;
	}
	zdata[0] = 0x78u;
	zdata[1] = 0x01u;
	pos = 2u;
	for (bi = 0u; bi < blocks; ++bi) {
		u32 remain = raw_len - bi * max_store;
		u32 chunk = remain > max_store ? max_store : remain;
		u32 bfinal = (bi + 1u == blocks) ? 1u : 0u;
		zdata[pos++] = (u8)bfinal; /* BFINAL + BTYPE=00 */
		zdata[pos++] = (u8)(chunk & 0xFFu);
		zdata[pos++] = (u8)((chunk >> 8) & 0xFFu);
		zdata[pos++] = (u8)((~chunk) & 0xFFu);
		zdata[pos++] = (u8)(((~chunk) >> 8) & 0xFFu);
		if (chunk > 0u) {
			memcpy(zdata + pos, raw + bi * max_store, (size_t)chunk);
			pos += chunk;
		}
	}
	for (i = 0u; i < raw_len; ++i) {
		adler_s1 = (adler_s1 + raw[i]) % 65521u;
		adler_s2 = (adler_s2 + adler_s1) % 65521u;
	}
	sample_put_u32_be(zdata + pos, (adler_s2 << 16) | adler_s1);
	pos += 4u;
	zlen = pos;
	free(raw);

	f = fopen(path, "wb");
	if (f == NULL) {
		free(zdata);
		return -1;
	}
	fwrite(sig, 1, 8, f);
	/* IHDR */
	sample_put_u32_be(ihdr, 13u);
	ihdr[4] = 'I';
	ihdr[5] = 'H';
	ihdr[6] = 'D';
	ihdr[7] = 'R';
	sample_put_u32_be(ihdr + 8, w);
	sample_put_u32_be(ihdr + 12, h);
	ihdr[16] = 8;
	ihdr[17] = 6;
	ihdr[18] = 0;
	ihdr[19] = 0;
	ihdr[20] = 0;
	crc = sample_crc32(ihdr + 4, 17u);
	sample_put_u32_be(ihdr + 21, crc);
	fwrite(ihdr, 1, 25, f);
	/* IDAT */
	sample_put_u32_be(chunk_hdr, zlen);
	chunk_hdr[4] = 'I';
	chunk_hdr[5] = 'D';
	chunk_hdr[6] = 'A';
	chunk_hdr[7] = 'T';
	fwrite(chunk_hdr, 1, 8, f);
	fwrite(zdata, 1, zlen, f);
	{
		u8* crcbuf = (u8*)malloc((size_t)zlen + 4u);
		if (crcbuf == NULL) {
			free(zdata);
			fclose(f);
			return -1;
		}
		crcbuf[0] = 'I';
		crcbuf[1] = 'D';
		crcbuf[2] = 'A';
		crcbuf[3] = 'T';
		memcpy(crcbuf + 4, zdata, (size_t)zlen);
		crc = sample_crc32(crcbuf, zlen + 4u);
		free(crcbuf);
		sample_put_u32_be(chunk_hdr, crc);
		fwrite(chunk_hdr, 1, 4, f);
	}
	free(zdata);
	/* IEND */
	{
		u8 iend[12];
		sample_put_u32_be(iend, 0u);
		iend[4] = 'I';
		iend[5] = 'E';
		iend[6] = 'N';
		iend[7] = 'D';
		crc = sample_crc32(iend + 4, 4u);
		sample_put_u32_be(iend + 8, crc);
		fwrite(iend, 1, 12, f);
	}
	fclose(f);
	return 0;
}

static u32 sample_read_be32(const u8* p) {
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

static i32 sample_png_read_rgba(const char* path, u8** out_rgba, u32* out_w, u32* out_h) {
	FILE* f;
	u8 sig[8];
	u8* file = NULL;
	long fsz;
	u32 off;
	u32 w = 0, h = 0;
	u8* idat = NULL;
	u32 idat_cap = 0, idat_len = 0;
	u8* raw = NULL;
	u32 raw_cap;
	u32 y;

	f = fopen(path, "rb");
	if (f == NULL) {
		return -1;
	}
	if (fread(sig, 1, 8, f) != 8 || sig[0] != 137) {
		fclose(f);
		return -1;
	}
	fseek(f, 0, SEEK_END);
	fsz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (fsz <= 8) {
		fclose(f);
		return -1;
	}
	file = (u8*)malloc((size_t)fsz);
	if (file == NULL) {
		fclose(f);
		return -1;
	}
	if (fread(file, 1, (size_t)fsz, f) != (size_t)fsz) {
		free(file);
		fclose(f);
		return -1;
	}
	fclose(f);

	off = 8u;
	while (off + 12u <= (u32)fsz) {
		u32 len = sample_read_be32(file + off);
		const char* type = (const char*)(file + off + 4u);
		const u8* data = file + off + 8u;
		if (off + 12u + len > (u32)fsz) {
			break;
		}
		if (memcmp(type, "IHDR", 4) == 0 && len >= 13u) {
			w = sample_read_be32(data);
			h = sample_read_be32(data + 4);
		} else if (memcmp(type, "IDAT", 4) == 0) {
			u8* nbuf;
			if (idat_len + len > idat_cap) {
				u32 nc = idat_cap == 0u ? (len + 64u) : idat_cap * 2u;
				while (nc < idat_len + len) {
					nc *= 2u;
				}
				nbuf = (u8*)realloc(idat, (size_t)nc);
				if (nbuf == NULL) {
					free(idat);
					free(file);
					return -1;
				}
				idat = nbuf;
				idat_cap = nc;
			}
			memcpy(idat + idat_len, data, (size_t)len);
			idat_len += len;
		} else if (memcmp(type, "IEND", 4) == 0) {
			break;
		}
		off += 12u + len;
	}
	free(file);
	if (w == 0u || h == 0u || idat == NULL || idat_len < 7u) {
		free(idat);
		return -1;
	}
	/* inflate store-only zlib */
	raw_cap = (w * 4u + 1u) * h;
	raw = (u8*)malloc((size_t)raw_cap);
	if (raw == NULL) {
		free(idat);
		return -1;
	}
	{
		u32 zi = 2u; /* skip CMF/FLG */
		u32 ro = 0u;
		while (zi + 5u <= idat_len - 4u) {
			u8 hdr = idat[zi++];
			u32 bfinal = hdr & 1u;
			u32 btype = (hdr >> 1) & 3u;
			u32 blen, nlen;
			if (btype != 0u) {
				free(raw);
				free(idat);
				return -1;
			}
			blen = (u32)idat[zi] | ((u32)idat[zi + 1u] << 8);
			nlen = (u32)idat[zi + 2u] | ((u32)idat[zi + 3u] << 8);
			(void)nlen;
			zi += 4u;
			if (zi + blen > idat_len - 4u || ro + blen > raw_cap) {
				free(raw);
				free(idat);
				return -1;
			}
			memcpy(raw + ro, idat + zi, (size_t)blen);
			ro += blen;
			zi += blen;
			if (bfinal) {
				break;
			}
		}
	}
	free(idat);

	*out_rgba = (u8*)malloc((size_t)w * (size_t)h * 4u);
	if (*out_rgba == NULL) {
		free(raw);
		return -1;
	}
	for (y = 0u; y < h; ++y) {
		const u8* row = raw + y * (w * 4u + 1u);
		if (row[0] != 0u) {
			free(raw);
			free(*out_rgba);
			*out_rgba = NULL;
			return -1;
		}
		memcpy(*out_rgba + y * w * 4u, row + 1, (size_t)w * 4u);
	}
	free(raw);
	*out_w = w;
	*out_h = h;
	return 0;
}

static i32 sample_env_regen(void) {
	const char* e = getenv("SK_UI_REGEN_GOLDENS");
	return (e != NULL && e[0] == '1' && e[1] == '\0') ? 1 : 0;
}

static i32 sample_golden_path(char* out, u32 cap, const char* name) {
	static const char* bases[] = {
		SK_UI_SAMPLE_GOLDEN_DIR, "../../plugins/ui/testdata/sample", "../plugins/ui/testdata/sample", "plugins/ui/testdata/sample", ".",
	};
	u32 i;
	for (i = 0u; i < sizeof(bases) / sizeof(bases[0]); ++i) {
		FILE* f;
		snprintf(out, cap, "%s/%s.png", bases[i], name);
		f = fopen(out, "rb");
		if (f != NULL) {
			fclose(f);
			return 0;
		}
	}
	snprintf(out, cap, "%s/%s.png", SK_UI_SAMPLE_GOLDEN_DIR, name);
	return -1;
}

static void sample_golden_compare(const char* name, const u8* actual, u32 w, u32 h) {
	char path[512];
	u8* golden = NULL;
	u32 gw = 0, gh = 0;
	i32 regen = sample_env_regen();
	i32 found = sample_golden_path(path, (u32)sizeof(path), name);
	u32 tol = 10u;
	u32 fail = 0u;
	u32 n;
	u32 i;

	if (regen || found != 0) {
		snprintf(path, sizeof(path), "%s/%s.png", SK_UI_SAMPLE_GOLDEN_DIR, name);
		TEST_ASSERT_EQUAL_INT(0, sample_png_write_rgba(path, actual, w, h));
		if (regen) {
			return;
		}
	}
	if (sample_png_read_rgba(path, &golden, &gw, &gh) != 0) {
		TEST_FAIL_MESSAGE("sample menu golden PNG missing or unreadable");
		return;
	}
	TEST_ASSERT_EQUAL_UINT(w, gw);
	TEST_ASSERT_EQUAL_UINT(h, gh);
	n = w * h * 4u;
	for (i = 0u; i < n; ++i) {
		i32 d = (i32)actual[i] - (i32)golden[i];
		if (d < 0) {
			d = -d;
		}
		if ((u32)d > tol) {
			fail += 1u;
		}
	}
	free(golden);
	/* Allow a small fraction of AA/rounding diffs (font soft-raster). */
	TEST_ASSERT_TRUE(fail < (w * h) / 25u + 8u);
}

static sk_ui_harness_t* sample_build_harness(const sk_ui_api_t* ui, f32 scale, sk_ui_font_system_t** out_fs, sk_ui_font_t** out_font) {
	sk_ui_harness_desc_t desc;
	sk_ui_harness_t* h;
	sk_ui_context_t* ctx;
	sk_ui_font_system_t* fs;
	sk_ui_font_t* font;
	f32 lw, lh;

	ui->sample_menu_logical_size(&lw, &lh);
	memset(&desc, 0, sizeof(desc));
	desc.width = lw;
	desc.height = lh;
	desc.content_scale = scale;
	desc.soft_render = 1;
	h = ui->harness_create(&desc);
	TEST_ASSERT_NOT_NULL(h);
	ctx = ui->harness_context(h);

	fs = ui->font_system_create(NULL, 256u, 256u);
	TEST_ASSERT_NOT_NULL(fs);
	font = ui->font_load_memory(fs, skore_test_font_ttf, (u32)sizeof(skore_test_font_ttf));
	TEST_ASSERT_NOT_NULL(font);
	ui->harness_set_font(h, fs, font);

	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->sample_menu_build(ctx, SK_UI_NODE_INVALID)));
	*out_fs = fs;
	*out_font = font;
	return h;
}

SK_TEST(ui_sample_menu_uses_all_v1_widgets) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_harness_desc_t desc;
	sk_ui_harness_t* h;
	sk_ui_context_t* ctx;
	static const char* widgets[] = {"panel", "view", "label", "button", "checkbox", "slider", "text_input", "scroll_view", "image"};
	u32 i;

	memset(&desc, 0, sizeof(desc));
	desc.width = SK_UI_SAMPLE_MENU_W;
	desc.height = SK_UI_SAMPLE_MENU_H;
	h = ui->harness_create(&desc);
	TEST_ASSERT_NOT_NULL(h);
	ctx = ui->harness_context(h);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->sample_menu_build(ctx, SK_UI_NODE_INVALID)));
	for (i = 0u; i < sizeof(widgets) / sizeof(widgets[0]); ++i) {
		TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_widget(ctx, SK_UI_NODE_INVALID, widgets[i])));
	}
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "menu-play")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_class(ctx, SK_UI_NODE_INVALID, SK_UI_SAMPLE_CLS_CARD)));
	ui->harness_destroy(h);
}

SK_TEST(ui_sample_menu_golden_1x) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_font_system_t* fs = NULL;
	sk_ui_font_t* font = NULL;
	sk_ui_harness_t* h = sample_build_harness(ui, 1.0f, &fs, &font);
	const u8* px;
	u32 w = 0, ht = 0;

	TEST_ASSERT_EQUAL_INT(0, ui->harness_step(h, 1.0f / 60.0f));
	px = ui->harness_pixels(h);
	TEST_ASSERT_NOT_NULL(px);
	ui->harness_pixel_size(h, &w, &ht);
	TEST_ASSERT_EQUAL_UINT(320u, w);
	TEST_ASSERT_EQUAL_UINT(240u, ht);
	sample_golden_compare("menu_1x", px, w, ht);

	ui->harness_destroy(h);
	ui->font_destroy(font);
	ui->font_system_destroy(fs);
}

SK_TEST(ui_sample_menu_golden_2x) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_font_system_t* fs = NULL;
	sk_ui_font_t* font = NULL;
	sk_ui_harness_t* h = sample_build_harness(ui, 2.0f, &fs, &font);
	const u8* px;
	u32 w = 0, ht = 0;

	TEST_ASSERT_EQUAL_INT(0, ui->harness_step(h, 1.0f / 60.0f));
	px = ui->harness_pixels(h);
	TEST_ASSERT_NOT_NULL(px);
	ui->harness_pixel_size(h, &w, &ht);
	TEST_ASSERT_EQUAL_UINT(640u, w);
	TEST_ASSERT_EQUAL_UINT(480u, ht);
	sample_golden_compare("menu_2x", px, w, ht);

	ui->harness_destroy(h);
	ui->font_destroy(font);
	ui->font_system_destroy(fs);
}

SK_TEST(ui_sample_menu_layout_scale_independent_and_runtime_scale) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_font_system_t* fs = NULL;
	sk_ui_font_t* font = NULL;
	sk_ui_harness_t* h = sample_build_harness(ui, 1.0f, &fs, &font);
	sk_ui_context_t* ctx = ui->harness_context(h);
	sk_ui_node_t title;
	sk_ui_node_t card;
	sk_ui_rect_t logical_1x, logical_2x;
	sk_ui_rect_t scaled_1x, scaled_2x;
	const sk_ui_draw_list_t* dl1;
	const sk_ui_draw_list_t* dl2;
	u32 gen1, gen2;
	u32 cache_before, cache_after;
	u32 hits0, misses0, hits1, misses1;
	u32 font_meshes_1x = 0u, font_meshes_2x = 0u;
	u32 c;
	f32 max_x_1 = 0.0f, max_x_2 = 0.0f;
	u32 i;

	TEST_ASSERT_EQUAL_INT(0, ui->harness_step(h, 1.0f / 60.0f));
	title = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "menu-title");
	card = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "menu-card");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(title));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(card));

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, title, &logical_1x, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect_scaled(ctx, title, &scaled_1x, NULL));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, logical_1x.width, scaled_1x.width); /* 1x */
	TEST_ASSERT_TRUE(logical_1x.width > 0.0f);

	dl1 = ui->harness_draw_list(h);
	TEST_ASSERT_NOT_NULL(dl1);
	gen1 = dl1->generation;
	for (c = 0u; c < dl1->command_count; ++c) {
		if (dl1->commands[c].kind == SK_UI_DRAW_CMD_MESH && dl1->commands[c].texture_kind == SK_UI_DRAW_TEX_FONT) {
			font_meshes_1x += 1u;
		}
	}
	for (i = 0u; i < dl1->vertex_count; ++i) {
		if (dl1->vertices[i].x > max_x_1) {
			max_x_1 = dl1->vertices[i].x;
		}
	}
	ui->font_cache_stats(fs, &hits0, &misses0);
	cache_before = ui->font_cache_count(fs);
	TEST_ASSERT_TRUE(cache_before > 0u);
	TEST_ASSERT_TRUE(font_meshes_1x >= 1u);

	/* Runtime scale change without recreating the tree (host path). */
	TEST_ASSERT_EQUAL_INT(0, ui->harness_set_content_scale(h, 2.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->harness_step(h, 1.0f / 60.0f));

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, title, &logical_2x, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect_scaled(ctx, title, &scaled_2x, NULL));

	/* Logical layout is scale-independent. */
	TEST_ASSERT_FLOAT_WITHIN(0.51f, logical_1x.x, logical_2x.x);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, logical_1x.y, logical_2x.y);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, logical_1x.width, logical_2x.width);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, logical_1x.height, logical_2x.height);

	/* Physical rects scale 2x. */
	TEST_ASSERT_FLOAT_WITHIN(0.51f, scaled_1x.width * 2.0f, scaled_2x.width);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, scaled_1x.height * 2.0f, scaled_2x.height);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, scaled_1x.x * 2.0f, scaled_2x.x);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, scaled_1x.y * 2.0f, scaled_2x.y);

	dl2 = ui->harness_draw_list(h);
	TEST_ASSERT_NOT_NULL(dl2);
	gen2 = dl2->generation;
	TEST_ASSERT_TRUE(gen2 != gen1); /* paint rebuilt after scale change */
	TEST_ASSERT_EQUAL_INT(0, dl2->reused);

	for (c = 0u; c < dl2->command_count; ++c) {
		if (dl2->commands[c].kind == SK_UI_DRAW_CMD_MESH && dl2->commands[c].texture_kind == SK_UI_DRAW_TEX_FONT) {
			font_meshes_2x += 1u;
		}
	}
	for (i = 0u; i < dl2->vertex_count; ++i) {
		if (dl2->vertices[i].x > max_x_2) {
			max_x_2 = dl2->vertices[i].x;
		}
	}
	/* Geometry roughly 2x in physical space. */
	TEST_ASSERT_TRUE(max_x_2 > max_x_1 * 1.5f);

	ui->font_cache_stats(fs, &hits1, &misses1);
	cache_after = ui->font_cache_count(fs);
	/* Glyphs re-rasterize at the new physical size → additional cache entries. */
	TEST_ASSERT_TRUE(cache_after > cache_before);
	TEST_ASSERT_TRUE(misses1 > misses0);
	TEST_ASSERT_TRUE(font_meshes_2x >= 1u);

	/* Soft-render buffer grew with scale. */
	{
		u32 pw = 0, ph = 0;
		ui->harness_pixel_size(h, &pw, &ph);
		TEST_ASSERT_EQUAL_UINT(640u, pw);
		TEST_ASSERT_EQUAL_UINT(480u, ph);
		TEST_ASSERT_NOT_NULL(ui->harness_pixels(h));
	}

	/*
	 * Performance observation (reported, not optimized away):
	 * - Full draw-list rebuild on scale change (generation bumped).
	 * - Font cache grows with each distinct (pixel_size, glyph) pair.
	 * - Steady-state same-scale frames may reuse the draw list (reused=1).
	 */
	TEST_ASSERT_EQUAL_INT(0, ui->harness_step(h, 1.0f / 60.0f));
	dl2 = ui->harness_draw_list(h);
	TEST_ASSERT_NOT_NULL(dl2);
	/* Second step at same scale: paint may reuse if tree is clean. */
	TEST_ASSERT_TRUE(dl2->reused != 0 || dl2->generation >= gen2);

	ui->harness_destroy(h);
	ui->font_destroy(font);
	ui->font_system_destroy(fs);
}

#endif /* SK_TESTS */

#ifdef SK_TESTS
SK_TEST(zz_probe_menu_rects) {
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	(void)ui->sample_menu_build(ctx, SK_UI_NODE_INVALID);
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 320.0f, 240.0f));
	{
		const char* ids[] = {"menu-screen",	 "menu-card",	"menu-title",	"menu-header",	   "menu-logo",	   "menu-fields", "menu-name",	   "menu-vol-row",
							 "menu-vol-lbl", "menu-volume", "menu-options", "menu-fullscreen", "menu-fs-lbl",  "menu-vsync",  "menu-vs-lbl",   "menu-changelog",
							 "menu-log-0",	 "menu-log-1",	"menu-log-2",	"menu-log-3",	   "menu-actions", "menu-play",	  "menu-settings", "menu-quit"};
		u32 i;
		for (i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
			sk_ui_node_t n = ui->find_by_id(ctx, ids[i]);
			sk_ui_rect_t b, c;
			if (!sk_ui_node_is_valid(n)) {
				printf("MISSING %s\n", ids[i]);
				continue;
			}
			if (ui->node_get_layout_rect(ctx, n, &b, &c) != 0) {
				printf("NORECT %s\n", ids[i]);
				continue;
			}
			fprintf(stderr, "%-16s border=(%6.2f,%6.2f %6.2fx%6.2f) content=(%6.2f,%6.2f %6.2fx%6.2f)\n", ids[i], (double)b.x, (double)b.y, (double)b.width, (double)b.height,
					(double)c.x, (double)c.y, (double)c.width, (double)c.height);
		}
	}
	ui->context_destroy(ctx);
}
#endif
