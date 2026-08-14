/**
 * @file image_family_tests.c
 * @brief Headless automation for the editor textured-quad image (APX-359, §16).
 *
 * ImGui::Image half: explicit size, sub-rect UV (including flipped V), tint
 * and border colour propagation into the paint draw list, and zero/invalid
 * texture handling. Draw-command/geometry assertions only — no rendering.
 */

#include "ui_test.h"

#include <stdio.h>
#include <string.h>

#ifdef SK_TESTS
#if defined(SK_UI_PLUGIN_BUILD)

static void igf_place(sk_ui_test_t* t, sk_ui_node_t node, f32 x, f32 y, f32 w, f32 h) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	/* style_resolve copies the inline layout into the node layout_style each
	 * frame, so absolute position/offsets must ride the inline style. */
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_WIDTH | SK_UI_SP_MAX_HEIGHT | SK_UI_SP_POSITION | SK_UI_SP_LEFT |
			 SK_UI_SP_TOP;
	p.layout.width = sk_ui_pt(w);
	p.layout.height = sk_ui_pt(h);
	p.layout.min_width = sk_ui_pt(w);
	p.layout.min_height = sk_ui_pt(h);
	p.layout.max_width = sk_ui_pt(w);
	p.layout.max_height = sk_ui_pt(h);
	p.layout.position = SK_UI_POSITION_ABSOLUTE;
	p.layout.left = sk_ui_pt(x);
	p.layout.top = sk_ui_pt(y);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(t->ctx, node, &p));
}

/* First IMAGE mesh command with the given texture id (or NULL). */
static const sk_ui_draw_cmd_t* igf_find_image_cmd(const sk_ui_draw_list_t* dl, u32 tex_id, u32* out_cmd_index) {
	u32 c;
	if (dl == NULL) {
		return NULL;
	}
	for (c = 0u; c < dl->command_count; ++c) {
		const sk_ui_draw_cmd_t* cmd = &dl->commands[c];
		if (cmd->kind == SK_UI_DRAW_CMD_MESH && cmd->texture_kind == SK_UI_DRAW_TEX_IMAGE && cmd->texture_id == tex_id) {
			if (out_cmd_index != NULL) {
				*out_cmd_index = c;
			}
			return cmd;
		}
	}
	return NULL;
}

/* Count mesh vertices painted with exactly @p packed colour. */
static u32 igf_count_color_vertices(const sk_ui_draw_list_t* dl, u32 packed) {
	u32 c;
	u32 count = 0u;
	if (dl == NULL) {
		return 0u;
	}
	for (c = 0u; c < dl->command_count; ++c) {
		const sk_ui_draw_cmd_t* cmd = &dl->commands[c];
		u32 i;
		if (cmd->kind != SK_UI_DRAW_CMD_MESH) {
			continue;
		}
		for (i = 0u; i < cmd->index_count; ++i) {
			u32 idx = dl->indices[cmd->index_offset + i];
			if (dl->vertices[idx].color == packed) {
				count += 1u;
			}
		}
	}
	return count;
}

/**
 * Sized quad with a sub-rect UV and a tint: verify the draw commands and
 * geometry (position, UV rect, packed tint, border strips).
 */
SK_UI_TEST(image_family_size_uv_tint_border) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t img;
	sk_ui_rect_t r;
	const sk_ui_draw_list_t* dl;
	const sk_ui_draw_cmd_t* cmd;
	u32 base;
	u32 packed_tint;
	u32 packed_border;

	img = ui->widget_image_rect(ctx, root, 7, 96.0f, 64.0f, 0.25f, 0.25f, 0.75f, 1.0f, &(sk_ui_color_t){1.0f, 0.6f, 0.2f, 1.0f}, &(sk_ui_color_t){0.9f, 0.1f, 0.1f, 1.0f},
								"igf-img");
	TEST_ASSERT_TRUE_MESSAGE(sk_ui_node_is_valid(img), "widget_image_rect failed");
	igf_place(t, img, 8.0f, 8.0f, 96.0f, 64.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, img, &r, NULL));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.0f, r.x);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.0f, r.y);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 96.0f, r.width);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 64.0f, r.height);

	dl = ui->get_draw_list(ctx);
	cmd = igf_find_image_cmd(dl, 7u, NULL);
	TEST_ASSERT_NOT_NULL_MESSAGE(cmd, "IMAGE mesh with texture_id 7 missing");
	TEST_ASSERT_TRUE(cmd->index_count >= 6u);
	base = dl->indices[cmd->index_offset];
	packed_tint = sk_ui_pack_color(sk_ui_rgba(1.0f, 0.6f, 0.2f, 1.0f));
	packed_border = sk_ui_pack_color(sk_ui_rgba(0.9f, 0.1f, 0.1f, 1.0f));

	/* Border is 1px: the quad is inset to (9,9)-(103,71). */
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 9.0f, dl->vertices[base + 0u].x);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 9.0f, dl->vertices[base + 0u].y);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 103.0f, dl->vertices[base + 2u].x);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 71.0f, dl->vertices[base + 2u].y);
	/* UV rect propagated: TL (0.25,0.25), TR (0.75,0.25), BR (0.75,1.0), BL (0.25,1.0). */
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.25f, dl->vertices[base + 0u].u);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.25f, dl->vertices[base + 0u].v);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.75f, dl->vertices[base + 1u].u);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.25f, dl->vertices[base + 1u].v);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.75f, dl->vertices[base + 2u].u);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, dl->vertices[base + 2u].v);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.25f, dl->vertices[base + 3u].u);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, dl->vertices[base + 3u].v);
	/* Tint reaches the quad vertices. */
	TEST_ASSERT_EQUAL_UINT(packed_tint, dl->vertices[base + 0u].color);
	TEST_ASSERT_EQUAL_UINT(packed_tint, dl->vertices[base + 2u].color);
	/* Border strips painted as solid quads (4 edges x 2 triangles = 12 indices). */
	TEST_ASSERT_TRUE(igf_count_color_vertices(dl, packed_border) >= 12u);
}

/**
 * Flipped V (uv1 < uv0 on the V axis mirrors) plus a texture-id change
 * across frames: geometry + UV + tint stay identical, only the bound id
 * moves.
 */
SK_UI_TEST(image_family_flipped_v_texture_change) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t img;
	const sk_ui_draw_list_t* dl;
	const sk_ui_draw_cmd_t* cmd;
	u32 base;
	u32 packed_tint;

	img = ui->widget_image_rect(ctx, root, 11, 48.0f, 32.0f, 0.0f, 1.0f, 1.0f, 0.0f, &(sk_ui_color_t){0.2f, 0.6f, 1.0f, 1.0f}, NULL, "igf-flip");
	TEST_ASSERT_TRUE_MESSAGE(sk_ui_node_is_valid(img), "widget_image_rect failed");
	igf_place(t, img, 4.0f, 4.0f, 48.0f, 32.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	dl = ui->get_draw_list(ctx);
	cmd = igf_find_image_cmd(dl, 11u, NULL);
	TEST_ASSERT_NOT_NULL_MESSAGE(cmd, "IMAGE mesh with texture_id 11 missing");
	base = dl->indices[cmd->index_offset];
	/* Flipped V: top row samples v=1.0, bottom row v=0.0. */
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, dl->vertices[base + 0u].v);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, dl->vertices[base + 1u].v);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, dl->vertices[base + 2u].v);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, dl->vertices[base + 3u].v);
	packed_tint = sk_ui_pack_color(sk_ui_rgba(0.2f, 0.6f, 1.0f, 1.0f));
	TEST_ASSERT_EQUAL_UINT(packed_tint, dl->vertices[base + 0u].color);

	/* Change the bound host texture across a frame (no rendering in between). */
	TEST_ASSERT_EQUAL_INT(0, ui->image_set_texture(ctx, img, 99));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	dl = ui->get_draw_list(ctx);
	cmd = igf_find_image_cmd(dl, 99u, NULL);
	TEST_ASSERT_NOT_NULL_MESSAGE(cmd, "IMAGE mesh with texture_id 99 missing after change");
	TEST_ASSERT_NULL_MESSAGE(igf_find_image_cmd(dl, 11u, NULL), "old texture id still bound");
	base = dl->indices[cmd->index_offset];
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, dl->vertices[base + 0u].v);
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, dl->vertices[base + 2u].v);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.0f, dl->vertices[base + 0u].x);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.0f, dl->vertices[base + 0u].y);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 52.0f, dl->vertices[base + 2u].x);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 36.0f, dl->vertices[base + 2u].y);
	TEST_ASSERT_EQUAL_UINT(packed_tint, dl->vertices[base + 0u].color);
}

/**
 * Zero / invalid texture: no textured quad is emitted, but a requested
 * border still paints (ImGui AddRect does not sample the texture).
 */
SK_UI_TEST(image_family_zero_invalid_texture) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t zero;
	sk_ui_node_t legacy;
	const sk_ui_draw_list_t* dl;
	u32 packed_border;

	/* texture_id 0 + border: border strips only, no IMAGE mesh. */
	zero = ui->widget_image_rect(ctx, root, 0, 40.0f, 40.0f, 0.0f, 0.0f, 1.0f, 1.0f, NULL, &(sk_ui_color_t){0.1f, 0.9f, 0.1f, 1.0f}, "igf-zero");
	TEST_ASSERT_TRUE_MESSAGE(sk_ui_node_is_valid(zero), "widget_image_rect(0) failed");
	igf_place(t, zero, 2.0f, 2.0f, 40.0f, 40.0f);

	/* Legacy factory keeps full-UV + white tint + no border defaults. */
	legacy = ui->widget_image(ctx, root, 21, "igf-legacy");
	TEST_ASSERT_TRUE_MESSAGE(sk_ui_node_is_valid(legacy), "widget_image failed");
	igf_place(t, legacy, 60.0f, 2.0f, 24.0f, 24.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	dl = ui->get_draw_list(ctx);
	TEST_ASSERT_NULL_MESSAGE(igf_find_image_cmd(dl, 0u, NULL), "zero texture must not emit a quad");
	packed_border = sk_ui_pack_color(sk_ui_rgba(0.1f, 0.9f, 0.1f, 1.0f));
	TEST_ASSERT_TRUE_MESSAGE(igf_count_color_vertices(dl, packed_border) >= 12u, "border not painted with zero texture");

	/* Legacy widget_image: full UV (0..1), white tint, no border. */
	{
		const sk_ui_draw_cmd_t* cmd = igf_find_image_cmd(dl, 21u, NULL);
		u32 base;
		TEST_ASSERT_NOT_NULL_MESSAGE(cmd, "legacy IMAGE mesh missing");
		base = dl->indices[cmd->index_offset];
		TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, dl->vertices[base + 0u].u);
		TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, dl->vertices[base + 0u].v);
		TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, dl->vertices[base + 2u].u);
		TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, dl->vertices[base + 2u].v);
		TEST_ASSERT_EQUAL_UINT(sk_ui_pack_color(sk_ui_rgba(1.0f, 1.0f, 1.0f, 1.0f)), dl->vertices[base + 0u].color);
		TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, dl->vertices[base + 0u].x);
		TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, dl->vertices[base + 0u].y);
	}
}

/**
 * Explicit size / aspect: portrait and landscape quads keep their requested
 * layout box (the editor passes image_size directly).
 */
SK_UI_TEST(image_family_size_aspect) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t land;
	sk_ui_node_t port;
	sk_ui_rect_t r;

	land = ui->widget_image_rect(ctx, root, 5, 128.0f, 48.0f, 0.0f, 0.0f, 1.0f, 1.0f, NULL, NULL, "igf-land");
	port = ui->widget_image_rect(ctx, root, 6, 40.0f, 120.0f, 0.0f, 0.0f, 1.0f, 1.0f, NULL, NULL, "igf-port");
	TEST_ASSERT_TRUE_MESSAGE(sk_ui_node_is_valid(land) && sk_ui_node_is_valid(port), "widget_image_rect failed");
	igf_place(t, land, 4.0f, 4.0f, 128.0f, 48.0f);
	igf_place(t, port, 4.0f, 60.0f, 40.0f, 120.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, land, &r, NULL));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 128.0f, r.width);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 48.0f, r.height);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, port, &r, NULL));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, r.width);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, r.height);
}

#endif /* SK_UI_PLUGIN_BUILD */
#endif /* SK_TESTS */
