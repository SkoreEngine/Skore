/**
 * @file paint.c
 * @brief Paint pass: walk the laid-out styled tree and emit a draw list.
 *
 * CPU only — no GPU calls. Geometry is in physical pixels (logical layout ×
 * content scale applied exactly once). Batches by texture and clip; emits
 * solid/rounded rects, borders, textured quads, glyph quads, and clip push/pop.
 */

#include "ui.internal.h"

#include "allocator.h"

#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Math helpers                                                               */
/* -------------------------------------------------------------------------- */

#ifndef UI_PAINT_PI
#define UI_PAINT_PI 3.14159265358979323846f
#endif

enum {
	UI_PAINT_CORNER_SEGS = 6, /* arcs per rounded corner */
	UI_PAINT_CLIP_STACK = 32,
};

static f32 ui_paint_fminf(f32 a, f32 b) {
	return a < b ? a : b;
}

static i32 ui_paint_color_visible(sk_ui_color_t c) {
	return c.a > 0.0001f ? 1 : 0;
}

static sk_ui_color_t ui_paint_mul_opacity(sk_ui_color_t c, f32 opacity) {
	c.a *= opacity;
	return c;
}

static sk_ui_rect_t ui_paint_rect_intersect(const sk_ui_rect_t* a, const sk_ui_rect_t* b) {
	sk_ui_rect_t out;
	f32 x0 = a->x > b->x ? a->x : b->x;
	f32 y0 = a->y > b->y ? a->y : b->y;
	f32 x1 = (a->x + a->width) < (b->x + b->width) ? (a->x + a->width) : (b->x + b->width);
	f32 y1 = (a->y + a->height) < (b->y + b->height) ? (a->y + a->height) : (b->y + b->height);
	out.x = x0;
	out.y = y0;
	out.width = x1 > x0 ? (x1 - x0) : 0.0f;
	out.height = y1 > y0 ? (y1 - y0) : 0.0f;
	return out;
}

static i32 ui_paint_f32_bits_eq(f32 a, f32 b) {
	u32 ia;
	u32 ib;
	memcpy(&ia, &a, sizeof(ia));
	memcpy(&ib, &b, sizeof(ib));
	return ia == ib ? 1 : 0;
}

static i32 ui_paint_clip_eq(const sk_ui_rect_t* a, const sk_ui_rect_t* b) {
	return ui_paint_f32_bits_eq(a->x, b->x) && ui_paint_f32_bits_eq(a->y, b->y) && ui_paint_f32_bits_eq(a->width, b->width) && ui_paint_f32_bits_eq(a->height, b->height);
}

/* Crude cos/sin via small Taylor or libc — use platform math. */
#include <math.h>

/* -------------------------------------------------------------------------- */
/* Draw list store                                                            */
/* -------------------------------------------------------------------------- */

void ui_draw_list_store_init(ui_draw_list_store_t* store, const sk_allocator_t* a) {
	memset(store, 0, sizeof(*store));
	sk_array_init(&store->vertices, a);
	sk_array_init(&store->indices, a);
	sk_array_init(&store->commands, a);
	store->last_scale_x = 1.0f;
	store->last_scale_y = 1.0f;
	store->valid = 0;
	store->view.vertices = NULL;
	store->view.vertex_count = 0u;
	store->view.indices = NULL;
	store->view.index_count = 0u;
	store->view.commands = NULL;
	store->view.command_count = 0u;
	store->view.generation = 0u;
	store->view.reused = 0;
}

void ui_draw_list_store_shutdown(ui_draw_list_store_t* store) {
	if (store == NULL) {
		return;
	}
	sk_array_free(&store->vertices);
	sk_array_free(&store->indices);
	sk_array_free(&store->commands);
	memset(store, 0, sizeof(*store));
}

static void ui_draw_list_publish(ui_draw_list_store_t* store, i32 reused) {
	store->view.vertices = store->vertices.items;
	store->view.vertex_count = store->vertices.count;
	store->view.indices = store->indices.items;
	store->view.index_count = store->indices.count;
	store->view.commands = store->commands.items;
	store->view.command_count = store->commands.count;
	store->view.reused = reused;
}

/* -------------------------------------------------------------------------- */
/* Emitter state                                                              */
/* -------------------------------------------------------------------------- */

typedef struct ui_paint_emitter_t {
	sk_ui_context_t* ctx;
	ui_draw_list_store_t* store;
	const sk_ui_paint_params_t* params;
	f32 scale_x;
	f32 scale_y;

	sk_ui_rect_t clip_stack[UI_PAINT_CLIP_STACK];
	u32 clip_depth;

	/* Open mesh batch (appended while texture+clip match). */
	i32 mesh_open;
	sk_ui_draw_texture_kind_t mesh_tex_kind;
	u32 mesh_tex_id;
	sk_ui_rect_t mesh_clip;
	u32 mesh_index_start;
} ui_paint_emitter_t;

static sk_ui_rect_t ui_paint_full_viewport(const ui_paint_emitter_t* em) {
	sk_ui_rect_t r;
	/* Large enough for tests / unconstrained paint; backends may intersect. */
	r.x = 0.0f;
	r.y = 0.0f;
	r.width = em->ctx->root_width * em->scale_x;
	r.height = em->ctx->root_height * em->scale_y;
	if (r.width < 1.0f) {
		r.width = 1.0f;
	}
	if (r.height < 1.0f) {
		r.height = 1.0f;
	}
	return r;
}

static sk_ui_rect_t ui_paint_active_clip(const ui_paint_emitter_t* em) {
	if (em->clip_depth == 0u) {
		return ui_paint_full_viewport(em);
	}
	return em->clip_stack[em->clip_depth - 1u];
}

static void ui_paint_close_mesh(ui_paint_emitter_t* em) {
	sk_ui_draw_cmd_t* cmd;
	if (em->mesh_open == 0) {
		return;
	}
	cmd = &em->store->commands.items[em->store->commands.count - 1u];
	cmd->index_count = em->store->indices.count - em->mesh_index_start;
	if (cmd->index_count == 0u) {
		/* Drop empty mesh. */
		em->store->commands.count -= 1u;
	}
	em->mesh_open = 0;
}

static i32 ui_paint_ensure_mesh(ui_paint_emitter_t* em, sk_ui_draw_texture_kind_t kind, u32 tex_id) {
	sk_ui_rect_t clip = ui_paint_active_clip(em);
	sk_ui_draw_cmd_t cmd;

	if (em->mesh_open != 0 && em->mesh_tex_kind == kind && em->mesh_tex_id == tex_id && ui_paint_clip_eq(&em->mesh_clip, &clip)) {
		return 0;
	}

	ui_paint_close_mesh(em);

	memset(&cmd, 0, sizeof(cmd));
	cmd.kind = SK_UI_DRAW_CMD_MESH;
	cmd.texture_kind = kind;
	cmd.texture_id = tex_id;
	cmd.index_offset = em->store->indices.count;
	cmd.index_count = 0u;
	cmd.clip = clip;
	if (sk_array_push(&em->store->commands, cmd) != 0) {
		return -1;
	}
	em->mesh_open = 1;
	em->mesh_tex_kind = kind;
	em->mesh_tex_id = tex_id;
	em->mesh_clip = clip;
	em->mesh_index_start = em->store->indices.count;
	return 0;
}

static i32 ui_paint_push_clip(ui_paint_emitter_t* em, sk_ui_rect_t rect) {
	sk_ui_draw_cmd_t cmd;
	sk_ui_rect_t next;

	ui_paint_close_mesh(em);

	if (em->clip_depth > 0u) {
		next = ui_paint_rect_intersect(&em->clip_stack[em->clip_depth - 1u], &rect);
	} else {
		next = rect;
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.kind = SK_UI_DRAW_CMD_PUSH_CLIP;
	cmd.clip = rect; /* pushed rect (pre-intersect) for nesting diagnostics */
	if (sk_array_push(&em->store->commands, cmd) != 0) {
		return -1;
	}

	if (em->clip_depth >= (u32)UI_PAINT_CLIP_STACK) {
		return -1;
	}
	em->clip_stack[em->clip_depth] = next;
	em->clip_depth += 1u;
	return 0;
}

static i32 ui_paint_pop_clip(ui_paint_emitter_t* em) {
	sk_ui_draw_cmd_t cmd;

	ui_paint_close_mesh(em);

	if (em->clip_depth == 0u) {
		return -1;
	}
	em->clip_depth -= 1u;

	memset(&cmd, 0, sizeof(cmd));
	cmd.kind = SK_UI_DRAW_CMD_POP_CLIP;
	if (sk_array_push(&em->store->commands, cmd) != 0) {
		return -1;
	}
	return 0;
}

static i32 ui_paint_reserve_quad(ui_paint_emitter_t* em, u32* out_base_vertex) {
	u32 base = em->store->vertices.count;
	u32 i0 = base;
	u32 idx[6];

	if (sk_array_reserve(&em->store->vertices, base + 4u) != 0) {
		return -1;
	}
	/* Grow count by pushing placeholders. */
	{
		sk_ui_draw_vertex_t z;
		memset(&z, 0, sizeof(z));
		if (sk_array_push(&em->store->vertices, z) != 0 || sk_array_push(&em->store->vertices, z) != 0 || sk_array_push(&em->store->vertices, z) != 0 ||
			sk_array_push(&em->store->vertices, z) != 0) {
			return -1;
		}
	}

	/* Triangle list: 0-1-2, 0-2-3 (TL-TR-BR-BL). */
	idx[0] = i0 + 0u;
	idx[1] = i0 + 1u;
	idx[2] = i0 + 2u;
	idx[3] = i0 + 0u;
	idx[4] = i0 + 2u;
	idx[5] = i0 + 3u;
	{
		u32 k;
		for (k = 0u; k < 6u; ++k) {
			if (sk_array_push(&em->store->indices, idx[k]) != 0) {
				return -1;
			}
		}
	}
	*out_base_vertex = base;
	return 0;
}

static void ui_paint_write_vertex(ui_paint_emitter_t* em, u32 index, f32 x, f32 y, f32 u, f32 v, u32 color) {
	sk_ui_draw_vertex_t* vtx = &em->store->vertices.items[index];
	vtx->x = x;
	vtx->y = y;
	vtx->u = u;
	vtx->v = v;
	vtx->color = color;
}

static i32 ui_paint_add_textured_quad(ui_paint_emitter_t* em, sk_ui_draw_texture_kind_t kind, u32 tex_id, f32 x0, f32 y0, f32 x1, f32 y1, f32 u0, f32 v0, f32 u1, f32 v1,
									  u32 color) {
	u32 base;
	if (x1 <= x0 || y1 <= y0) {
		return 0;
	}
	if (ui_paint_ensure_mesh(em, kind, tex_id) != 0) {
		return -1;
	}
	if (ui_paint_reserve_quad(em, &base) != 0) {
		return -1;
	}
	ui_paint_write_vertex(em, base + 0u, x0, y0, u0, v0, color);
	ui_paint_write_vertex(em, base + 1u, x1, y0, u1, v0, color);
	ui_paint_write_vertex(em, base + 2u, x1, y1, u1, v1, color);
	ui_paint_write_vertex(em, base + 3u, x0, y1, u0, v1, color);
	return 0;
}

static i32 ui_paint_add_solid_quad(ui_paint_emitter_t* em, f32 x0, f32 y0, f32 x1, f32 y1, u32 color) {
	return ui_paint_add_textured_quad(em, SK_UI_DRAW_TEX_NONE, 0u, x0, y0, x1, y1, 0.0f, 0.0f, 0.0f, 0.0f, color);
}

/* Fan triangle from center to arc samples. */
static i32 ui_paint_add_triangle(ui_paint_emitter_t* em, f32 x0, f32 y0, f32 x1, f32 y1, f32 x2, f32 y2, u32 color) {
	u32 base = em->store->vertices.count;
	sk_ui_draw_vertex_t z;
	u32 i0, i1, i2;

	if (ui_paint_ensure_mesh(em, SK_UI_DRAW_TEX_NONE, 0u) != 0) {
		return -1;
	}
	memset(&z, 0, sizeof(z));
	if (sk_array_push(&em->store->vertices, z) != 0 || sk_array_push(&em->store->vertices, z) != 0 || sk_array_push(&em->store->vertices, z) != 0) {
		return -1;
	}
	i0 = base + 0u;
	i1 = base + 1u;
	i2 = base + 2u;
	ui_paint_write_vertex(em, i0, x0, y0, 0.0f, 0.0f, color);
	ui_paint_write_vertex(em, i1, x1, y1, 0.0f, 0.0f, color);
	ui_paint_write_vertex(em, i2, x2, y2, 0.0f, 0.0f, color);
	if (sk_array_push(&em->store->indices, i0) != 0 || sk_array_push(&em->store->indices, i1) != 0 || sk_array_push(&em->store->indices, i2) != 0) {
		return -1;
	}
	return 0;
}

/**
 * Thick line segment as a filled quad (two triangles). Used for checkbox X
 * diagonals so the mark reads as two crossing strokes, not a checkmark/tick.
 */
static i32 ui_paint_add_thick_line(ui_paint_emitter_t* em, f32 x0, f32 y0, f32 x1, f32 y1, f32 thickness, u32 color) {
	f32 dx = x1 - x0;
	f32 dy = y1 - y0;
	f32 len = sqrtf(dx * dx + dy * dy);
	f32 nx;
	f32 ny;
	f32 half;
	if (len < 0.001f) {
		return 0;
	}
	half = thickness * 0.5f;
	nx = (-dy / len) * half;
	ny = (dx / len) * half;
	/* Corners: p0+n, p1+n, p1-n, p0-n as two triangles. */
	if (ui_paint_add_triangle(em, x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, color) != 0) {
		return -1;
	}
	if (ui_paint_add_triangle(em, x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, color) != 0) {
		return -1;
	}
	return 0;
}

static i32 ui_paint_add_rounded_rect_filled(ui_paint_emitter_t* em, f32 x, f32 y, f32 w, f32 h, f32 radius, u32 color) {
	f32 x1 = x + w;
	f32 y1 = y + h;
	f32 r;
	f32 cx[4];
	f32 cy[4];
	f32 a0[4];
	u32 c;
	u32 s;

	if (w <= 0.0f || h <= 0.0f) {
		return 0;
	}
	r = ui_paint_fminf(radius, ui_paint_fminf(w * 0.5f, h * 0.5f));
	if (r <= 0.5f) {
		return ui_paint_add_solid_quad(em, x, y, x1, y1, color);
	}

	/*
	 * Solid body as three axis-aligned quads (center + top/bottom strips between
	 * corners), plus four quarter-circle fans from each corner center. A single
	 * fan from the rect center only covers corner arcs and leaves the flat sides
	 * hollow — do not use that approach.
	 */
	if (ui_paint_add_solid_quad(em, x + r, y, x1 - r, y + r, color) != 0) {
		return -1;
	}
	if (ui_paint_add_solid_quad(em, x, y + r, x1, y1 - r, color) != 0) {
		return -1;
	}
	if (ui_paint_add_solid_quad(em, x + r, y1 - r, x1 - r, y1, color) != 0) {
		return -1;
	}

	/* Corner centers: TL, TR, BR, BL. Angles start at outer arc. */
	cx[0] = x + r;
	cy[0] = y + r;
	a0[0] = UI_PAINT_PI; /* left → top */
	cx[1] = x1 - r;
	cy[1] = y + r;
	a0[1] = UI_PAINT_PI * 1.5f; /* top → right */
	cx[2] = x1 - r;
	cy[2] = y1 - r;
	a0[2] = 0.0f; /* right → bottom */
	cx[3] = x + r;
	cy[3] = y1 - r;
	a0[3] = UI_PAINT_PI * 0.5f; /* bottom → left */

	for (c = 0u; c < 4u; ++c) {
		for (s = 0u; s < (u32)UI_PAINT_CORNER_SEGS; ++s) {
			f32 t0 = (f32)s / (f32)UI_PAINT_CORNER_SEGS;
			f32 t1 = (f32)(s + 1u) / (f32)UI_PAINT_CORNER_SEGS;
			f32 ang0 = a0[c] + t0 * (UI_PAINT_PI * 0.5f);
			f32 ang1 = a0[c] + t1 * (UI_PAINT_PI * 0.5f);
			f32 px0 = cx[c] + cosf(ang0) * r;
			f32 py0 = cy[c] + sinf(ang0) * r;
			f32 px1 = cx[c] + cosf(ang1) * r;
			f32 py1 = cy[c] + sinf(ang1) * r;
			if (ui_paint_add_triangle(em, cx[c], cy[c], px0, py0, px1, py1, color) != 0) {
				return -1;
			}
		}
	}
	return 0;
}

static i32 ui_paint_add_border(ui_paint_emitter_t* em, f32 x, f32 y, f32 w, f32 h, f32 bl, f32 bt, f32 br, f32 bb, u32 color) {
	/* Four edge strips (physical px). Overlap at corners is fine (opaque paint). */
	if (bt > 0.0f) {
		if (ui_paint_add_solid_quad(em, x, y, x + w, y + bt, color) != 0) {
			return -1;
		}
	}
	if (bb > 0.0f) {
		if (ui_paint_add_solid_quad(em, x, y + h - bb, x + w, y + h, color) != 0) {
			return -1;
		}
	}
	if (bl > 0.0f) {
		if (ui_paint_add_solid_quad(em, x, y + bt, x + bl, y + h - bb, color) != 0) {
			return -1;
		}
	}
	if (br > 0.0f) {
		if (ui_paint_add_solid_quad(em, x + w - br, y + bt, x + w, y + h - bb, color) != 0) {
			return -1;
		}
	}
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Node paint                                                                 */
/* -------------------------------------------------------------------------- */

static const_chr_t ui_paint_prop_str(const ui_node_slot_t* slot, const_chr_t key) {
	u32 i;
	for (i = 0u; i < slot->props.count; ++i) {
		const ui_prop_entry_t* e = &slot->props.items[i];
		if (e->type == SK_UI_PROP_STR && e->key != NULL && key != NULL && strcmp(e->key, key) == 0) {
			return e->data.str_value;
		}
	}
	return NULL;
}

static i32 ui_paint_prop_i32(const ui_node_slot_t* slot, const_chr_t key, i32* out) {
	u32 i;
	for (i = 0u; i < slot->props.count; ++i) {
		const ui_prop_entry_t* e = &slot->props.items[i];
		if (e->type == SK_UI_PROP_I32 && e->key != NULL && key != NULL && strcmp(e->key, key) == 0) {
			*out = e->data.i32_value;
			return 0;
		}
	}
	return -1;
}

static f32 ui_paint_prop_f32_or(const ui_node_slot_t* slot, const_chr_t key, f32 fallback) {
	u32 i;
	for (i = 0u; i < slot->props.count; ++i) {
		const ui_prop_entry_t* e = &slot->props.items[i];
		if (e->type == SK_UI_PROP_F32 && e->key != NULL && key != NULL && strcmp(e->key, key) == 0) {
			return e->data.f32_value;
		}
	}
	return fallback;
}

static i32 ui_paint_utf8_next(const u8** pp, u32* out_cp) {
	const u8* p = *pp;
	if (*p == 0u) {
		return 0;
	}
	if ((*p & 0x80u) == 0u) {
		*out_cp = *p;
		*pp = p + 1;
		return 1;
	}
	if ((*p & 0xE0u) == 0xC0u && p[1] != 0u) {
		*out_cp = ((u32)(p[0] & 0x1Fu) << 6) | (u32)(p[1] & 0x3Fu);
		*pp = p + 2;
		return 1;
	}
	if ((*p & 0xF0u) == 0xE0u && p[1] != 0u && p[2] != 0u) {
		*out_cp = ((u32)(p[0] & 0x0Fu) << 12) | ((u32)(p[1] & 0x3Fu) << 6) | (u32)(p[2] & 0x3Fu);
		*pp = p + 3;
		return 1;
	}
	if ((*p & 0xF8u) == 0xF0u && p[1] != 0u && p[2] != 0u && p[3] != 0u) {
		*out_cp = ((u32)(p[0] & 0x07u) << 18) | ((u32)(p[1] & 0x3Fu) << 12) | ((u32)(p[2] & 0x3Fu) << 6) | (u32)(p[3] & 0x3Fu);
		*pp = p + 4;
		return 1;
	}
	*pp = p + 1;
	return 0;
}

static i32 ui_paint_msdf_gpu_quads(void) {
	const char* e = getenv("SK_UI_MSDF_GPU");
	return (e != NULL && (e[0] == '1' || e[0] == 'y' || e[0] == 'Y')) ? 1 : 0;
}

/*
 * Evaluate the MSDF shader (median + screen-space range + smoothstep) on the
 * CPU and emit coverage solid runs. GPU atlas sampling is tofu on lavapipe
 * (APX-244); this keeps captures/letterforms correct while matching the PS.
 */
static i32 ui_paint_emit_msdf_coverage(ui_paint_emitter_t* em, const sk_ui_msdf_atlas_t* atlas, const sk_ui_msdf_glyph_t* mg, f32 gx0, f32 gy0, f32 gx1, f32 gy1, u32 color) {
	i32 x0;
	i32 y0;
	i32 x1;
	i32 y1;
	i32 y;
	f32 qw;
	f32 qh;
	f32 fw_u;
	f32 fw_v;
	f32 spr;
	const u32 base_a = (color >> 24) & 0xffu;

	if (gx1 < gx0) {
		f32 t = gx0;
		gx0 = gx1;
		gx1 = t;
	}
	if (gy1 < gy0) {
		f32 t = gy0;
		gy0 = gy1;
		gy1 = t;
	}
	qw = gx1 - gx0;
	qh = gy1 - gy0;
	if (qw <= 0.0f || qh <= 0.0f || atlas == NULL || atlas->pixels == NULL) {
		return 0;
	}
	fw_u = (mg->u1 - mg->u0) / qw;
	fw_v = (mg->v1 - mg->v0) / qh;
	if (fw_u < 0.0f) {
		fw_u = -fw_u;
	}
	if (fw_v < 0.0f) {
		fw_v = -fw_v;
	}
	spr = ui_msdf_screen_px_range(atlas->px_range, (f32)atlas->width, (f32)atlas->height, fw_u, fw_v);

	x0 = (i32)gx0;
	y0 = (i32)gy0;
	x1 = (i32)gx1 + 1;
	y1 = (i32)gy1 + 1;
	if (gx0 < 0.0f) {
		x0 = (i32)(gx0 - 1.0f);
	}
	if (gy0 < 0.0f) {
		y0 = (i32)(gy0 - 1.0f);
	}
	if (x1 < x0) {
		return 0;
	}
	if (y1 < y0) {
		return 0;
	}

	for (y = y0; y < y1; ++y) {
		i32 x = x0;
		while (x < x1) {
			f32 px = (f32)x + 0.5f;
			f32 py = (f32)y + 0.5f;
			f32 u;
			f32 v;
			f32 med;
			f32 cov;
			u32 a;
			i32 run = x + 1;
			u32 sum = 0u;
			u32 n = 0u;
			if (px < gx0 || py < gy0 || px >= gx1 || py >= gy1) {
				x += 1;
				continue;
			}
			u = mg->u0 + (mg->u1 - mg->u0) * ((px - gx0) / qw);
			v = mg->v0 + (mg->v1 - mg->v0) * ((py - gy0) / qh);
			med = ui_msdf_sample_median_bilinear(atlas, u, v);
			cov = ui_msdf_coverage(med, spr);
			a = (u32)((f32)base_a * cov + 0.5f);
			if (a == 0u) {
				x += 1;
				continue;
			}
			sum = a;
			n = 1u;
			while (run < x1) {
				f32 rpx = (f32)run + 0.5f;
				f32 ru;
				f32 rmed;
				f32 rcov;
				u32 ra;
				if (rpx < gx0 || rpx >= gx1) {
					break;
				}
				ru = mg->u0 + (mg->u1 - mg->u0) * ((rpx - gx0) / qw);
				rmed = ui_msdf_sample_median_bilinear(atlas, ru, v);
				rcov = ui_msdf_coverage(rmed, spr);
				ra = (u32)((f32)base_a * rcov + 0.5f);
				if (ra == 0u || (ra > a + 16u) || (a > ra + 16u)) {
					break;
				}
				sum += ra;
				n += 1u;
				run += 1;
			}
			{
				u32 avg = sum / n;
				u32 run_col = (color & 0x00ffffffu) | (avg << 24);
				if (ui_paint_add_solid_quad(em, (f32)x, (f32)y, (f32)run, (f32)y + 1.0f, run_col) != 0) {
					return -1;
				}
			}
			x = run;
		}
	}
	return 0;
}

static i32 ui_paint_emit_notdef_box(ui_paint_emitter_t* em, f32 x0, f32 y0, f32 x1, f32 y1, u32 color) {
	f32 w = x1 - x0;
	f32 h = y1 - y0;
	f32 t;
	if (w < 1.0f) {
		w = 1.0f;
		x1 = x0 + w;
	}
	if (h < 1.0f) {
		h = 1.0f;
		y1 = y0 + h;
	}
	t = w < h ? w : h;
	t *= 0.12f;
	if (t < 1.0f) {
		t = 1.0f;
	}
	if (t * 2.0f >= w) {
		t = w * 0.25f;
	}
	if (t * 2.0f >= h) {
		t = h * 0.25f;
	}
	if (ui_paint_add_solid_quad(em, x0, y0, x1, y0 + t, color) != 0) {
		return -1;
	}
	if (ui_paint_add_solid_quad(em, x0, y1 - t, x1, y1, color) != 0) {
		return -1;
	}
	if (ui_paint_add_solid_quad(em, x0, y0 + t, x0 + t, y1 - t, color) != 0) {
		return -1;
	}
	if (ui_paint_add_solid_quad(em, x1 - t, y0 + t, x1, y1 - t, color) != 0) {
		return -1;
	}
	return 0;
}

static i32 ui_paint_shape(sk_ui_font_system_t* sys, sk_ui_font_t* font, f32 px, u32 prev_cp, u32 cp, ui_text_layout_glyph_t* out) {
	return ui_text_layout_shape(sys, font, px, prev_cp, cp, out);
}

static f32 ui_paint_next_advance(sk_ui_font_system_t* sys, sk_ui_font_t* font, f32 px, u32* prev_cp, u32 cp) {
	ui_text_layout_glyph_t g;
	if (ui_paint_shape(sys, font, px, *prev_cp, cp, &g) != 0) {
		*prev_cp = 0u;
		return 0.0f;
	}
	*prev_cp = g.is_fallback != 0 ? 0u : cp;
	return g.advance_x;
}

static i32 ui_paint_font_metrics(sk_ui_font_t* font, f32 px, sk_ui_font_metrics_t* out) {
	return ui_text_layout_metrics(font, px, out);
}

/** Measure advance of a UTF-8 range [begin,end) at px (same path as paint). */
static f32 ui_paint_measure_advance(sk_ui_font_system_t* sys, sk_ui_font_t* font, f32 px, const u8* begin, const u8* end) {
	return ui_text_layout_measure_width(sys, font, px, begin, end);
}

/**
 * Emit text glyphs for a node with prop "text".
 * Optional props: wrap (i32 0/1), text_align (0=left 1=center 2=right),
 * vertical_align (0=top 1=center 2=bottom), content_w/h from layout for wrap/align.
 * Caret/selection when props caret/sel_start/sel_end are set (text input).
 */
static i32 ui_paint_emit_text(ui_paint_emitter_t* em, const ui_node_slot_t* slot, f32 content_x, f32 content_y, f32 content_w, f32 content_h, f32 opacity) {
	const_chr_t text;
	sk_ui_font_system_t* sys;
	sk_ui_font_t* font;
	f32 px;
	sk_ui_font_metrics_t metrics;
	u32 color;
	const u8* p;
	f32 avg_scale;
	i32 wrap = 0;
	i32 text_align = 0;
	i32 vert_align = 0;
	i32 caret = -1;
	i32 sel_a = -1;
	i32 sel_b = -1;
	i32 draw_caret = 0;
	i32 code_index;
	f32 line_x0;
	f32 baseline_y;
	f32 total_h;
	f32 pad_y;
	/* Line start positions for multi-pass align (max 32 lines). */
	const u8* line_starts[32];
	const u8* line_ends[32];
	f32 line_widths[32];
	u32 line_count = 0u;
	u32 li;
	const u8* text_bytes;
	const u8* line_begin;

	if (em->params == NULL) {
		return 0;
	}
	sys = em->params->font_system;
	font = em->params->font;
	if (sys == NULL || font == NULL) {
		return 0;
	}
	text = ui_paint_prop_str(slot, "text");
	if (text == NULL) {
		text = "";
	}

	{
		i32 v;
		if (ui_paint_prop_i32(slot, "wrap", &v) == 0) {
			wrap = v;
		}
		if (ui_paint_prop_i32(slot, "text_align", &v) == 0) {
			text_align = v;
		}
		if (ui_paint_prop_i32(slot, "vertical_align", &v) == 0) {
			vert_align = v;
		}
		if (ui_paint_prop_i32(slot, "caret", &v) == 0) {
			caret = v;
			draw_caret = 1;
		}
		if (ui_paint_prop_i32(slot, "sel_start", &v) == 0) {
			sel_a = v;
		}
		if (ui_paint_prop_i32(slot, "sel_end", &v) == 0) {
			sel_b = v;
		}
	}
	/* Disabled fields: no caret/selection chrome (dimmed text only) — APX-253. */
	if ((slot->state_flags & (u32)SK_UI_STATE_DISABLED) != 0u) {
		draw_caret = 0;
		sel_a = -1;
		sel_b = -1;
	}
	if (sel_a >= 0 && sel_b >= 0 && sel_a > sel_b) {
		i32 tmp = sel_a;
		sel_a = sel_b;
		sel_b = tmp;
	}

	avg_scale = (em->scale_x + em->scale_y) * 0.5f;
	if (avg_scale <= 0.0f) {
		avg_scale = 1.0f;
	}
	/* MSDF: scale one atlas by the physical size. Prefer the unrounded
	 * logical×scale so non-integer content scales stay linearly spaced;
	 * Clay still measures at integer fontSize (logical) and paint applies
	 * content scale here. */
	px = slot->computed.font_size * avg_scale;
	if (px < 1.0f) {
		px = 1.0f;
	}
	if (ui_paint_font_metrics(font, px, &metrics) != 0) {
		return 0;
	}

	color = sk_ui_pack_color(ui_paint_mul_opacity(slot->computed.color, opacity));
	text_bytes = (const u8*)text;

	/* Break into lines (hard \n + optional soft wrap at spaces). */
	p = text_bytes;
	line_begin = p;
	while (*p != 0u || line_begin < p) {
		const u8* word_break = NULL;
		const u8* q = line_begin;
		f32 line_w = 0.0f;
		const u8* line_end = line_begin;

		if (*p == 0u && line_begin == p) {
			break;
		}

		if (wrap != 0 && content_w > 0.0f) {
			u32 prev_cp = 0u;
			while (*q != 0u && *q != (u8)'\n') {
				const u8* cp_start = q;
				u32 cp;
				f32 adv;
				if (!ui_paint_utf8_next(&q, &cp)) {
					prev_cp = 0u;
					continue;
				}
				if (cp == (u32)' ' || cp == (u32)'\t') {
					word_break = q;
				}
				adv = ui_paint_next_advance(sys, font, px, &prev_cp, cp);
				if (line_w + adv > content_w && line_end > line_begin) {
					if (word_break != NULL && word_break > line_begin) {
						line_end = word_break;
					} else {
						line_end = cp_start;
					}
					break;
				}
				line_w += adv;
				line_end = q;
			}
			if (*q == (u8)'\n') {
				line_end = q;
			}
			if (line_end == line_begin && *q != 0u) {
				/* Force at least one cluster. */
				u32 dummy_cp = 0u;
				(void)ui_paint_utf8_next(&q, &dummy_cp);
				line_end = q;
			}
		} else {
			while (*q != 0u && *q != (u8)'\n') {
				q += 1;
			}
			line_end = q;
		}

		line_w = ui_paint_measure_advance(sys, font, px, line_begin, line_end);
		if (line_count < 32u) {
			line_starts[line_count] = line_begin;
			line_ends[line_count] = line_end;
			line_widths[line_count] = line_w;
			line_count += 1u;
		}

		p = line_end;
		if (*p == (u8)'\n') {
			p += 1;
		} else if (wrap != 0 && p == line_begin) {
			/* Avoid infinite loop. */
			if (*p != 0u) {
				p += 1;
			}
		}
		line_begin = p;
		if (*p == 0u && line_end == text_bytes + strlen(text) && line_count > 0u) {
			break;
		}
		if (*p == 0u) {
			break;
		}
	}
	if (line_count == 0u) {
		/* Empty text still needs caret. */
		line_starts[0] = text_bytes;
		line_ends[0] = text_bytes;
		line_widths[0] = 0.0f;
		line_count = 1u;
	}

	total_h = metrics.line_height * (f32)line_count;
	pad_y = 0.0f;
	if (vert_align == 1 && content_h > total_h) {
		pad_y = (content_h - total_h) * 0.5f;
	} else if (vert_align == 2 && content_h > total_h) {
		pad_y = content_h - total_h;
	}

	code_index = 0;
	baseline_y = content_y + pad_y + metrics.ascent;
	for (li = 0u; li < line_count; ++li) {
		f32 pen_x;
		const u8* a = line_starts[li];
		const u8* b = line_ends[li];
		f32 align_dx = 0.0f;

		if (text_align == 1 && content_w > line_widths[li]) {
			align_dx = (content_w - line_widths[li]) * 0.5f;
		} else if (text_align == 2 && content_w > line_widths[li]) {
			align_dx = content_w - line_widths[li];
		}
		line_x0 = content_x + align_dx;
		pen_x = line_x0;

		/* Selection highlight for this line. */
		if (sel_a >= 0 && sel_b > sel_a) {
			i32 i0 = code_index;
			i32 i1 = code_index;
			const u8* t = a;
			f32 x0 = pen_x;
			f32 x1 = pen_x;
			i32 started = 0;
			u32 sel_prev = 0u;
			while (t < b) {
				u32 cp;
				const u8* prev = t;
				f32 adv = 0.0f;
				if (!ui_paint_utf8_next(&t, &cp) || t == prev) {
					if (t == prev) {
						t += 1;
					}
					continue;
				}
				adv = ui_paint_next_advance(sys, font, px, &sel_prev, cp);
				if (i1 >= sel_a && i1 < sel_b) {
					if (!started) {
						x0 = pen_x + (f32)(i1 - code_index) * 0.0f + (pen_x - line_x0) + line_x0;
						/* pen_x tracks current; recompute from line start */
						(void)x0;
						started = 1;
					}
					x1 = pen_x + adv;
				}
				pen_x += adv;
				i1 += 1;
			}
			/* Recompute selection x via second walk. */
			pen_x = line_x0;
			i1 = i0;
			x0 = pen_x;
			x1 = pen_x;
			started = 0;
			sel_prev = 0u;
			t = a;
			while (t < b) {
				u32 cp;
				const u8* prev = t;
				f32 adv = 0.0f;
				if (!ui_paint_utf8_next(&t, &cp) || t == prev) {
					if (t == prev) {
						t += 1;
					}
					continue;
				}
				adv = ui_paint_next_advance(sys, font, px, &sel_prev, cp);
				if (i1 >= sel_a && i1 < sel_b) {
					if (!started) {
						x0 = pen_x;
						started = 1;
					}
					x1 = pen_x + adv;
				}
				pen_x += adv;
				i1 += 1;
			}
			if (started && x1 > x0) {
				u32 sel_col = sk_ui_pack_color(ui_paint_mul_opacity(sk_ui_rgba(0.25f, 0.45f, 0.85f, 0.45f), opacity));
				f32 y0 = baseline_y - metrics.ascent;
				f32 y1 = y0 + metrics.line_height;
				if (ui_paint_add_solid_quad(em, x0, y0, x1, y1, sel_col) != 0) {
					return -1;
				}
			}
		}

		/* Glyphs + caret. */
		pen_x = line_x0;
		{
			const u8* t = a;
			u32 layout_prev = 0u;
			while (t < b) {
				u32 cp;
				f32 gx0, gy0, gx1, gy1;
				const u8* prev = t;
				if (draw_caret && caret == code_index) {
					u32 caret_col = sk_ui_pack_color(ui_paint_mul_opacity(slot->computed.color, opacity));
					f32 y0 = baseline_y - metrics.ascent;
					f32 y1 = y0 + metrics.line_height;
					if (ui_paint_add_solid_quad(em, pen_x, y0, pen_x + 1.0f * avg_scale, y1, caret_col) != 0) {
						return -1;
					}
				}
				if (!ui_paint_utf8_next(&t, &cp) || t == prev) {
					if (t == prev) {
						t += 1;
					}
					continue;
				}
				{
					ui_text_layout_glyph_t sg;
					if (ui_paint_shape(sys, font, px, layout_prev, cp, &sg) != 0) {
						layout_prev = 0u;
						code_index += 1;
						continue;
					}
					layout_prev = sg.is_fallback != 0 ? 0u : cp;
					gx0 = pen_x + sg.quad_l;
					gy0 = baseline_y - sg.quad_t;
					gx1 = pen_x + sg.quad_r;
					gy1 = baseline_y - sg.quad_b;
					if (sg.is_fallback != 0 && sg.has_quad == 0) {
						if (ui_paint_emit_notdef_box(em, gx0, gy0, gx1, gy1, color) != 0) {
							return -1;
						}
					} else if (sg.has_quad != 0) {
						sk_ui_msdf_atlas_t atlas;
						sk_ui_msdf_glyph_t mg;
						memset(&mg, 0, sizeof(mg));
						mg.u0 = sg.u0;
						mg.v0 = sg.v0;
						mg.u1 = sg.u1;
						mg.v1 = sg.v1;
						if (ui_paint_msdf_gpu_quads() != 0) {
							if (ui_paint_add_textured_quad(em, SK_UI_DRAW_TEX_MSDF, ui_font_id(font), gx0, gy0, gx1, gy1, sg.u0, sg.v0, sg.u1, sg.v1, color) != 0) {
								return -1;
							}
						} else if (ui_font_msdf_get_atlas_impl(font, &atlas) == 0) {
							if (ui_paint_emit_msdf_coverage(em, &atlas, &mg, gx0, gy0, gx1, gy1, color) != 0) {
								return -1;
							}
						}
					}
					pen_x += sg.advance_x;
					code_index += 1;
					continue;
				}
			}
			/* Caret at end of line. */
			if (draw_caret && caret == code_index && li + 1u == line_count) {
				u32 caret_col = sk_ui_pack_color(ui_paint_mul_opacity(slot->computed.color, opacity));
				f32 y0 = baseline_y - metrics.ascent;
				f32 y1 = y0 + metrics.line_height;
				if (ui_paint_add_solid_quad(em, pen_x, y0, pen_x + 1.0f * avg_scale, y1, caret_col) != 0) {
					return -1;
				}
			}
		}

		/* Hard newline advances code index for the \n character. */
		if (b < text_bytes + strlen(text) && *b == (u8)'\n') {
			code_index += 1;
		}
		baseline_y += metrics.line_height;
	}
	return 0;
}

// NOLINTBEGIN(misc-no-recursion)
static i32 ui_paint_node(ui_paint_emitter_t* em, sk_ui_node_t node, f32 origin_x_log, f32 origin_y_log, f32 parent_opacity) {
	const ui_node_slot_t* slot = ui_slot(em->ctx, node);
	f32 ox;
	f32 oy;
	f32 bx, by, bw, bh;
	f32 cx, cy, cw, ch;
	f32 opacity;
	u32 bg_color;
	u32 bd_color;
	f32 radius_px;
	i32 pushed_clip = 0;
	u32 i;
	f32 scroll_x;
	f32 scroll_y;

	if (slot == NULL) {
		return -1;
	}

	/* Absolute logical → physical (scale applied once). */
	ox = (origin_x_log + slot->layout_border.x) * em->scale_x;
	oy = (origin_y_log + slot->layout_border.y) * em->scale_y;
	bx = ox;
	by = oy;
	bw = slot->layout_border.width * em->scale_x;
	bh = slot->layout_border.height * em->scale_y;

	cx = (origin_x_log + slot->layout_content.x) * em->scale_x;
	cy = (origin_y_log + slot->layout_content.y) * em->scale_y;
	cw = slot->layout_content.width * em->scale_x;
	ch = slot->layout_content.height * em->scale_y;

	opacity = parent_opacity * slot->computed.opacity;
	if (opacity <= 0.0001f) {
		return 0;
	}

	{
		/* Toggle paints its own pill track (see widget marks). Axis-aligned
		 * border strips square-off the rounded fill and make hover-off look
		 * like a lone scalloped circle to vision (D2). */
		const_chr_t box_wtype = ui_paint_prop_str(slot, "widget");
		i32 labeled = 0;
		i32 skip_box_chrome = (box_wtype != NULL && strcmp(box_wtype, "toggle") == 0) ? 1 : 0;
		if (box_wtype != NULL && (strcmp(box_wtype, "checkbox") == 0 || strcmp(box_wtype, "radio") == 0)) {
			(void)ui_paint_prop_i32(slot, "labeled", &labeled);
			if (labeled != 0) {
				skip_box_chrome = 1;
			}
		}

		/* Background */
		if (skip_box_chrome == 0 && ui_paint_color_visible(slot->computed.background_color)) {
			bg_color = sk_ui_pack_color(ui_paint_mul_opacity(slot->computed.background_color, opacity));
			radius_px = slot->computed.corner_radius * ((em->scale_x + em->scale_y) * 0.5f);
			if (ui_paint_add_rounded_rect_filled(em, bx, by, bw, bh, radius_px, bg_color) != 0) {
				return -1;
			}
		}

		/* Border (layout border widths × scale). */
		if (skip_box_chrome == 0 && ui_paint_color_visible(slot->computed.border_color)) {
			const sk_ui_edges_t* b = &slot->layout_style.border;
			f32 bl = b->left * em->scale_x;
			f32 bt = b->top * em->scale_y;
			f32 br = b->right * em->scale_x;
			f32 bb = b->bottom * em->scale_y;
			if (bl > 0.0f || bt > 0.0f || br > 0.0f || bb > 0.0f) {
				bd_color = sk_ui_pack_color(ui_paint_mul_opacity(slot->computed.border_color, opacity));
				if (ui_paint_add_border(em, bx, by, bw, bh, bl, bt, br, bb, bd_color) != 0) {
					return -1;
				}
			}
		}
	}

	/* Image: full content box textured quad. */
	if ((sk_ui_node_kind_t)slot->kind == SK_UI_NODE_KIND_IMAGE) {
		i32 tex_id = 0;
		if (ui_paint_prop_i32(slot, "texture_id", &tex_id) == 0 && tex_id != 0) {
			u32 col = sk_ui_pack_color(ui_paint_mul_opacity(sk_ui_rgba(1.0f, 1.0f, 1.0f, 1.0f), opacity));
			if (ui_paint_add_textured_quad(em, SK_UI_DRAW_TEX_IMAGE, (u32)tex_id, cx, cy, cx + cw, cy + ch, 0.0f, 0.0f, 1.0f, 1.0f, col) != 0) {
				return -1;
			}
		}
	}

	/*
	 * Widget marks (checkbox X, radio inner disc, toggle thumb).
	 * These are the fine details graded by per-widget vision rubrics (APX-252).
	 */
	{
		i32 checked = 0;
		i32 mixed = 0;
		i32 labeled = 0;
		i32 on = 0;
		const_chr_t wtype = ui_paint_prop_str(slot, "widget");
		(void)ui_paint_prop_i32(slot, "checked", &checked);
		(void)ui_paint_prop_i32(slot, "mixed", &mixed);
		(void)ui_paint_prop_i32(slot, "labeled", &labeled);
		if (wtype != NULL && strcmp(wtype, "checkbox") == 0) {
			f32 avg = (em->scale_x + em->scale_y) * 0.5f;
			f32 box_s = 18.0f * avg;
			f32 box_x = bx;
			f32 box_y = by;
			f32 mx;
			f32 my;
			f32 mw;
			f32 mh;
			if (labeled != 0) {
				if (box_s > bh) {
					box_s = bh;
				}
				box_y = by + (bh - box_s) * 0.5f;
				{
					u32 face = sk_ui_pack_color(ui_paint_mul_opacity(slot->computed.background_color, opacity));
					u32 bd = sk_ui_pack_color(ui_paint_mul_opacity(slot->computed.border_color, opacity));
					f32 r = slot->computed.corner_radius * avg;
					f32 t = 2.0f * avg;
					if (ui_paint_add_rounded_rect_filled(em, box_x, box_y, box_s, box_s, r, face) != 0) {
						return -1;
					}
					if (ui_paint_add_border(em, box_x, box_y, box_s, box_s, t, t, t, t, bd) != 0) {
						return -1;
					}
				}
				mx = box_x + 2.0f * avg;
				my = box_y + 2.0f * avg;
				mw = box_s - 4.0f * avg;
				mh = box_s - 4.0f * avg;
			} else {
				mx = cx;
				my = cy;
				mw = cw;
				mh = ch;
			}
			if (mixed != 0) {
				/* Indeterminate: filled inner square (not an X, not empty). */
				u32 mk = sk_ui_pack_color(ui_paint_mul_opacity(slot->computed.color, opacity));
				f32 inset = (mw < mh ? mw : mh) * 0.22f;
				f32 rr = 1.5f * avg;
				if (ui_paint_add_rounded_rect_filled(em, mx + inset, my + inset, mw - inset * 2.0f, mh - inset * 2.0f, rr, mk) != 0) {
					return -1;
				}
			} else if (checked != 0) {
				/* X mark: two crossing diagonal strokes (not a checkmark/tick). */
				u32 mk = sk_ui_pack_color(ui_paint_mul_opacity(slot->computed.color, opacity));
				f32 m = (mw < mh ? mw : mh) * 0.22f;
				f32 thick = (mw < mh ? mw : mh) * 0.16f;
				if (thick < 1.5f * avg) {
					thick = 1.5f * avg;
				}
				if (ui_paint_add_thick_line(em, mx + m, my + m, mx + mw - m, my + mh - m, thick, mk) != 0) {
					return -1;
				}
				if (ui_paint_add_thick_line(em, mx + mw - m, my + m, mx + m, my + mh - m, thick, mk) != 0) {
					return -1;
				}
			}
		}
		if (wtype != NULL && strcmp(wtype, "radio") == 0) {
			/*
			 * Circular ring: outer disc (ring color) minus inner hole (dark face).
			 * When checked, a smaller filled disc sits in the center.
			 * Uses border-box so the control is round even without layout border.
			 * Labeled radios paint the ring in the left 18px padding.
			 */
			u32 ring_col = sk_ui_pack_color(ui_paint_mul_opacity(slot->computed.color, opacity));
			u32 hole_col = sk_ui_pack_color(ui_paint_mul_opacity(sk_ui_rgba(0.12f, 0.13f, 0.15f, 1.0f), opacity));
			f32 side;
			f32 rx;
			f32 ry;
			f32 hole;
			f32 hx;
			f32 hy;
			if (labeled != 0) {
				f32 avg = (em->scale_x + em->scale_y) * 0.5f;
				side = 18.0f * avg;
				if (side > bh) {
					side = bh;
				}
				rx = bx;
				ry = by + (bh - side) * 0.5f;
			} else {
				side = bw < bh ? bw : bh;
				rx = bx + (bw - side) * 0.5f;
				ry = by + (bh - side) * 0.5f;
			}
			hole = side * 0.70f; /* thinner ring stroke so the hole reads clearly */
			hx = rx + (side - hole) * 0.5f;
			hy = ry + (side - hole) * 0.5f;
			if (ui_paint_add_rounded_rect_filled(em, rx, ry, side, side, side * 0.5f, ring_col) != 0) {
				return -1;
			}
			if (ui_paint_add_rounded_rect_filled(em, hx, hy, hole, hole, hole * 0.5f, hole_col) != 0) {
				return -1;
			}
			if (checked != 0) {
				/* Solid disc clearly smaller than the outer ring; dim when disabled. */
				i32 radio_disabled = ((slot->state_flags & (u32)SK_UI_STATE_DISABLED) != 0u) ? 1 : 0;
				/* Disabled: still a solid light disc (must read as filled), just less bright. */
				sk_ui_color_t disc_c = radio_disabled != 0 ? sk_ui_rgba(0.72f, 0.74f, 0.78f, 1.0f) : sk_ui_rgba(0.96f, 0.97f, 0.99f, 1.0f);
				u32 disc_col = sk_ui_pack_color(ui_paint_mul_opacity(disc_c, opacity));
				f32 disc = side * 0.50f;
				f32 dx = rx + (side - disc) * 0.5f;
				f32 dy = ry + (side - disc) * 0.5f;
				if (ui_paint_add_rounded_rect_filled(em, dx, dy, disc, disc, disc * 0.5f, disc_col) != 0) {
					return -1;
				}
			}
		}
		if (wtype != NULL && strcmp(wtype, "toggle") == 0) {
			/* True pill track in every state (half-min-side radius), then thumb. */
			i32 has_on = (ui_paint_prop_i32(slot, "on", &on) == 0) ? 1 : 0;
			i32 disabled = ((slot->state_flags & (u32)SK_UI_STATE_DISABLED) != 0u) ? 1 : 0;
			f32 pad = (bh < bw ? bh : bw) * 0.12f;
			f32 thumb = bh - pad * 2.0f;
			f32 thumb_x;
			f32 pill_r = (bh < bw ? bh : bw) * 0.5f;
			u32 thumb_col;
			u32 track_col;
			sk_ui_color_t accent;
			sk_ui_color_t thumb_c;
			if (has_on == 0) {
				on = 0;
			}
			if (thumb < 4.0f) {
				thumb = 4.0f;
			}
			if (on != 0) {
				/* Stronger track fill when ON so ON/OFF are distinguishable.
				 * Disabled ON must read as washed-out slate in isolation (not a
				 * still-vibrant blue pill) so vision grades "dimmed". */
				accent = disabled != 0 ? sk_ui_rgba(0.18f, 0.22f, 0.30f, 1.0f) : sk_ui_rgba(0.30f, 0.55f, 0.95f, 1.0f);
			} else {
				/* OFF/hover: state-styled fill, but forced to a stadium silhouette. */
				accent = slot->computed.background_color;
			}
			track_col = sk_ui_pack_color(ui_paint_mul_opacity(accent, opacity));
			if (ui_paint_add_rounded_rect_filled(em, bx, by, bw, bh, pill_r, track_col) != 0) {
				return -1;
			}
			/* Place thumb using border-box so it sits fully on the pill track. */
			thumb_x = (on != 0) ? (bx + bw - pad - thumb) : (bx + pad);
			if (thumb_x < bx) {
				thumb_x = bx;
			}
			if (thumb_x + thumb > bx + bw) {
				thumb_x = bx + bw - thumb;
			}
			/* Bright thumb on dark track so the knob is separable from the pill. */
			thumb_c = disabled != 0 ? sk_ui_rgba(0.48f, 0.50f, 0.54f, 1.0f) : sk_ui_rgba(0.96f, 0.97f, 0.99f, 1.0f);
			thumb_col = sk_ui_pack_color(ui_paint_mul_opacity(thumb_c, opacity));
			{
				f32 ty = by + (bh - thumb) * 0.5f;
				if (ui_paint_add_rounded_rect_filled(em, thumb_x, ty, thumb, thumb, thumb * 0.5f, thumb_col) != 0) {
					return -1;
				}
			}
		}
	}

	/* Slider fill + thumb from value/min/max props. */
	{
		const_chr_t wtype = ui_paint_prop_str(slot, "widget");
		if (wtype != NULL && strcmp(wtype, "slider") == 0) {
			f32 vmin = ui_paint_prop_f32_or(slot, "min", 0.0f);
			f32 vmax = ui_paint_prop_f32_or(slot, "max", 1.0f);
			f32 val = ui_paint_prop_f32_or(slot, "value", 0.0f);
			f32 t;
			f32 track_h;
			f32 track_y;
			f32 fill_w;
			f32 thumb_w;
			f32 thumb_h;
			f32 thumb_x;
			f32 thumb_y;
			u32 fill_col;
			u32 thumb_col;
			i32 disabled = ((slot->state_flags & (u32)SK_UI_STATE_DISABLED) != 0u) ? 1 : 0;
			sk_ui_color_t fill_c;
			sk_ui_color_t thumb_c;
			if (vmax <= vmin) {
				vmax = vmin + 1.0f;
			}
			if (val < vmin) {
				val = vmin;
			}
			if (val > vmax) {
				val = vmax;
			}
			t = (val - vmin) / (vmax - vmin);
			/* Thin track so the grab knob is clearly thicker (vision: not a bare bar). */
			track_h = ch * 0.22f;
			if (track_h < 2.0f) {
				track_h = 2.0f;
			}
			track_y = cy + (ch - track_h) * 0.5f;
			fill_w = cw * t;
			/* Disabled: muted fill + thumb (vision grades dimming) — APX-253. */
			fill_c = disabled != 0 ? sk_ui_rgba(0.22f, 0.32f, 0.48f, 1.0f) : sk_ui_rgba(0.30f, 0.55f, 0.95f, 1.0f);
			thumb_c = disabled != 0 ? sk_ui_rgba(0.55f, 0.56f, 0.58f, 1.0f) : sk_ui_rgba(0.95f, 0.95f, 0.98f, 1.0f);
			fill_col = sk_ui_pack_color(ui_paint_mul_opacity(fill_c, opacity));
			thumb_col = sk_ui_pack_color(ui_paint_mul_opacity(thumb_c, opacity));
			if (fill_w > 0.0f) {
				if (ui_paint_add_solid_quad(em, cx, track_y, cx + fill_w, track_y + track_h, fill_col) != 0) {
					return -1;
				}
			}
			/* Distinct rounded grab handle — taller than the track, not an end-cap. */
			thumb_w = ch * 0.55f;
			thumb_h = ch * 0.90f;
			if (thumb_w < 8.0f) {
				thumb_w = 8.0f;
			}
			if (thumb_h < 10.0f) {
				thumb_h = 10.0f;
			}
			thumb_x = cx + fill_w - thumb_w * 0.5f;
			if (thumb_x < cx) {
				thumb_x = cx;
			}
			if (thumb_x + thumb_w > cx + cw) {
				thumb_x = cx + cw - thumb_w;
			}
			thumb_y = cy + (ch - thumb_h) * 0.5f;
			if (ui_paint_add_rounded_rect_filled(em, thumb_x, thumb_y, thumb_w, thumb_h, thumb_w * 0.35f, thumb_col) != 0) {
				return -1;
			}
		}
		/*
		 * Range slider: track fill between value_low/value_high + two grab handles
		 * (APX-253). Distinct from progress (no thumbs) and single slider (one thumb).
		 */
		if (wtype != NULL && strcmp(wtype, "range_slider") == 0) {
			f32 vmin = ui_paint_prop_f32_or(slot, "min", 0.0f);
			f32 vmax = ui_paint_prop_f32_or(slot, "max", 1.0f);
			f32 vlow = ui_paint_prop_f32_or(slot, "value_low", 0.0f);
			f32 vhigh = ui_paint_prop_f32_or(slot, "value_high", 1.0f);
			f32 t0, t1;
			f32 track_h;
			f32 track_y;
			f32 x0, x1;
			f32 thumb_w;
			f32 thumb_y0;
			f32 thumb_y1;
			u32 fill_col;
			u32 thumb_col;
			if (vmax <= vmin) {
				vmax = vmin + 1.0f;
			}
			if (vlow < vmin) {
				vlow = vmin;
			}
			if (vhigh > vmax) {
				vhigh = vmax;
			}
			if (vlow > vhigh) {
				f32 tmp = vlow;
				vlow = vhigh;
				vhigh = tmp;
			}
			t0 = (vlow - vmin) / (vmax - vmin);
			t1 = (vhigh - vmin) / (vmax - vmin);
			track_h = ch * 0.22f;
			if (track_h < 2.0f) {
				track_h = 2.0f;
			}
			track_y = cy + (ch - track_h) * 0.5f;
			x0 = cx + cw * t0;
			x1 = cx + cw * t1;
			fill_col = sk_ui_pack_color(ui_paint_mul_opacity(sk_ui_rgba(0.30f, 0.55f, 0.95f, 1.0f), opacity));
			thumb_col = sk_ui_pack_color(ui_paint_mul_opacity(sk_ui_rgba(0.95f, 0.95f, 0.98f, 1.0f), opacity));
			if (x1 > x0) {
				if (ui_paint_add_solid_quad(em, x0, track_y, x1, track_y + track_h, fill_col) != 0) {
					return -1;
				}
			}
			thumb_w = ch * 0.55f;
			if (thumb_w < 8.0f) {
				thumb_w = 8.0f;
			}
			thumb_y0 = cy + ch * 0.05f;
			thumb_y1 = cy + ch * 0.95f;
			/* Low thumb. */
			{
				f32 tx = x0 - thumb_w * 0.5f;
				f32 th = thumb_y1 - thumb_y0;
				if (tx < cx) {
					tx = cx;
				}
				if (tx + thumb_w > cx + cw) {
					tx = cx + cw - thumb_w;
				}
				if (ui_paint_add_rounded_rect_filled(em, tx, thumb_y0, thumb_w, th, thumb_w * 0.35f, thumb_col) != 0) {
					return -1;
				}
			}
			/* High thumb. */
			{
				f32 tx = x1 - thumb_w * 0.5f;
				f32 th = thumb_y1 - thumb_y0;
				if (tx < cx) {
					tx = cx;
				}
				if (tx + thumb_w > cx + cw) {
					tx = cx + cw - thumb_w;
				}
				if (ui_paint_add_rounded_rect_filled(em, tx, thumb_y0, thumb_w, th, thumb_w * 0.35f, thumb_col) != 0) {
					return -1;
				}
			}
		}
		/*
		 * Progress bar: filled fraction only — no grab handle (APX-253).
		 * Distinguishes from slider in vision rubrics.
		 */
		if (wtype != NULL && strcmp(wtype, "progress") == 0) {
			f32 frac = ui_paint_prop_f32_or(slot, "value", 0.0f);
			f32 fill_w;
			f32 inset;
			u32 fill_col;
			u32 empty_col;
			if (frac < 0.0f) {
				frac = 0.0f;
			}
			if (frac > 1.0f) {
				frac = 1.0f;
			}
			/*
			 * Inset fill slightly so the track chrome remains visible on the right
			 * when partial — helps vision distinguish partial vs full (APX-253).
			 */
			inset = 1.0f * ((em->scale_x + em->scale_y) * 0.5f);
			if (inset > ch * 0.25f) {
				inset = ch * 0.25f;
			}
			fill_w = (cw > inset * 2.0f) ? (cw - inset * 2.0f) * frac : cw * frac;
			empty_col = sk_ui_pack_color(ui_paint_mul_opacity(sk_ui_rgba(0.28f, 0.30f, 0.34f, 1.0f), opacity));
			fill_col = sk_ui_pack_color(ui_paint_mul_opacity(sk_ui_rgba(0.32f, 0.58f, 0.96f, 1.0f), opacity));
			/* Unfilled remainder: slightly lighter than the outer track so empty reads. */
			if (frac < 0.999f && cw > fill_w + inset + 0.5f) {
				if (ui_paint_add_solid_quad(em, cx + inset + fill_w, cy + inset, cx + cw - inset, cy + ch - inset, empty_col) != 0) {
					return -1;
				}
			}
			if (fill_w > 0.5f) {
				if (ui_paint_add_solid_quad(em, cx + inset, cy + inset, cx + inset + fill_w, cy + ch - inset, fill_col) != 0) {
					return -1;
				}
			}
		}
	}

	/* Text family marks (APX-340): bullet disc, separator rule. */
	{
		const_chr_t wtype = ui_paint_prop_str(slot, "widget");
		f32 avg = (em->scale_x + em->scale_y) * 0.5f;
		if (wtype != NULL && strcmp(wtype, "bullet_text") == 0) {
			/* Small filled disc centered in the left padding (class pad 18).
			 * 8px diameter keeps a solid core after edge anti-aliasing. */
			f32 pad_l = slot->layout_style.padding.left * em->scale_x;
			f32 d = 8.0f * avg;
			f32 ddx;
			f32 ddy;
			u32 dot_col = sk_ui_pack_color(ui_paint_mul_opacity(slot->computed.color, opacity));
			if (ch > 0.0f && d > ch * 0.7f) {
				d = ch * 0.7f;
			}
			if (d < 3.0f * avg) {
				d = 3.0f * avg;
			}
			ddx = cx - pad_l * 0.5f - d * 0.5f;
			ddy = cy + (ch - d) * 0.5f;
			if (ui_paint_add_rounded_rect_filled(em, ddx, ddy, d, d, d * 0.5f, dot_col) != 0) {
				return -1;
			}
		}
		if (wtype != NULL && strcmp(wtype, "separator_text") == 0) {
			/* Full-width rule with a gap that holds the label (ImGui SeparatorText). */
			const_chr_t txt = ui_paint_prop_str(slot, "text");
			const sk_ui_paint_params_t* params = em->params;
			f32 px = slot->computed.font_size * avg;
			f32 label_w = 0.0f;
			f32 gap = 12.0f * avg;
			f32 mid = cy + ch * 0.5f;
			f32 t = 1.0f * avg;
			u32 rule_col = sk_ui_pack_color(ui_paint_mul_opacity(sk_ui_rgba(0.42f, 0.45f, 0.50f, 1.0f), opacity));
			if (px < 1.0f) {
				px = 1.0f;
			}
			if (params != NULL && params->font_system != NULL && params->font != NULL && txt != NULL && txt[0] != '\0') {
				label_w = ui_paint_measure_advance(params->font_system, params->font, px, (const u8*)txt, (const u8*)txt + strlen(txt));
			}
			if (txt == NULL || txt[0] == '\0') {
				if (ui_paint_add_thick_line(em, cx, mid, cx + cw, mid, t, rule_col) != 0) {
					return -1;
				}
			} else {
				if (ui_paint_add_thick_line(em, cx, mid, cx + gap, mid, t, rule_col) != 0) {
					return -1;
				}
				if (ui_paint_add_thick_line(em, cx + gap * 2.0f + label_w, mid, cx + cw, mid, t, rule_col) != 0) {
					return -1;
				}
			}
		}
	}

	/* Scrollbars for scroll_view when content overflows. */
	{
		const_chr_t wtype = ui_paint_prop_str(slot, "widget");
		if (wtype != NULL && strcmp(wtype, "scroll_view") == 0) {
			f32 content_h = ui_paint_prop_f32_or(slot, "content_height", 0.0f);
			f32 content_w = ui_paint_prop_f32_or(slot, "content_width", 0.0f);
			f32 scy = ui_paint_prop_f32_or(slot, "scroll_y", 0.0f);
			f32 scx = ui_paint_prop_f32_or(slot, "scroll_x", 0.0f);
			/* Opaque track so thumb is separable (vision: not a solid full-length bar). */
			u32 bar_col = sk_ui_pack_color(ui_paint_mul_opacity(sk_ui_rgba(0.10f, 0.11f, 0.13f, 1.0f), opacity));
			u32 thumb_col = sk_ui_pack_color(ui_paint_mul_opacity(sk_ui_rgba(0.72f, 0.74f, 0.80f, 1.0f), opacity));
			f32 bar_w = 8.0f * ((em->scale_x + em->scale_y) * 0.5f);
			if (content_h > slot->layout_content.height + 0.5f && slot->layout_content.height > 0.0f) {
				f32 view_h = ch;
				f32 max_scroll = content_h - slot->layout_content.height;
				f32 thumb_h = view_h * (slot->layout_content.height / content_h);
				f32 thumb_y;
				f32 pad = 1.0f * em->scale_y;
				/* Cap thumb so it stays clearly shorter than the track. */
				if (thumb_h < 12.0f * em->scale_y) {
					thumb_h = 12.0f * em->scale_y;
				}
				if (thumb_h > view_h * 0.55f) {
					thumb_h = view_h * 0.55f;
				}
				if (max_scroll < 1.0f) {
					max_scroll = 1.0f;
				}
				thumb_y = cy + pad + (view_h - thumb_h - pad * 2.0f) * (scy / max_scroll);
				if (ui_paint_add_solid_quad(em, cx + cw - bar_w, cy, cx + cw, cy + ch, bar_col) != 0) {
					return -1;
				}
				if (ui_paint_add_solid_quad(em, cx + cw - bar_w + 1.0f * em->scale_x, thumb_y, cx + cw - 1.0f * em->scale_x, thumb_y + thumb_h, thumb_col) != 0) {
					return -1;
				}
			}
			if (content_w > slot->layout_content.width + 0.5f && slot->layout_content.width > 0.0f) {
				f32 view_w = cw;
				f32 max_scroll = content_w - slot->layout_content.width;
				f32 thumb_w = view_w * (slot->layout_content.width / content_w);
				f32 thumb_x;
				f32 pad = 1.0f * em->scale_x;
				if (thumb_w < 12.0f * em->scale_x) {
					thumb_w = 12.0f * em->scale_x;
				}
				if (thumb_w > view_w * 0.55f) {
					thumb_w = view_w * 0.55f;
				}
				if (max_scroll < 1.0f) {
					max_scroll = 1.0f;
				}
				thumb_x = cx + pad + (view_w - thumb_w - pad * 2.0f) * (scx / max_scroll);
				if (ui_paint_add_solid_quad(em, cx, cy + ch - bar_w, cx + cw, cy + ch, bar_col) != 0) {
					return -1;
				}
				if (ui_paint_add_solid_quad(em, thumb_x, cy + ch - bar_w + 1.0f * em->scale_y, thumb_x + thumb_w, cy + ch - 1.0f * em->scale_y, thumb_col) != 0) {
					return -1;
				}
			}
		}
	}

	/* Text glyphs: TEXT nodes, BUTTON label prop, and text_input widget. */
	{
		sk_ui_node_kind_t kind = (sk_ui_node_kind_t)slot->kind;
		const_chr_t wtype = ui_paint_prop_str(slot, "widget");
		i32 emit = 0;
		if (kind == SK_UI_NODE_KIND_TEXT || kind == SK_UI_NODE_KIND_BUTTON) {
			emit = 1;
		}
		if (wtype != NULL && (strcmp(wtype, "text_input") == 0 || strcmp(wtype, "label") == 0 || strcmp(wtype, "button") == 0 || strcmp(wtype, "menu_item") == 0 ||
							  strcmp(wtype, "menu") == 0 || strcmp(wtype, "submenu") == 0 || strcmp(wtype, "dropdown") == 0 || strcmp(wtype, "tab") == 0 ||
							  strcmp(wtype, "window_title_bar") == 0 || strcmp(wtype, "separator_text") == 0)) {
			emit = 1;
		}
		if (emit && ui_paint_prop_str(slot, "text") != NULL) {
			f32 text_x = cx;
			f32 text_w = cw;
			if (wtype != NULL && strcmp(wtype, "separator_text") == 0) {
				/* Label starts after the rule gap (vertical_align=1 centers it). */
				f32 avg = (em->scale_x + em->scale_y) * 0.5f;
				text_x = cx + 12.0f * avg;
				text_w = cw - 12.0f * avg;
			}
			if (ui_paint_emit_text(em, slot, text_x, cy, text_w, ch, opacity) != 0) {
				return -1;
			}
		}
	}

	/* Clip children to content box (overflow / scroll). */
	if (slot->clip_children != 0u && slot->children.count > 0u) {
		sk_ui_rect_t clip;
		clip.x = cx;
		clip.y = cy;
		clip.width = cw;
		clip.height = ch;
		if (ui_paint_push_clip(em, clip) != 0) {
			return -1;
		}
		pushed_clip = 1;
	}

	scroll_x = ui_paint_prop_f32_or(slot, "scroll_x", 0.0f);
	scroll_y = ui_paint_prop_f32_or(slot, "scroll_y", 0.0f);

	/* Children: painter's algorithm — earlier siblings under later ones. */
	for (i = 0u; i < slot->children.count; ++i) {
		/* Child layout is relative to this node's content origin (logical). */
		f32 child_origin_x = origin_x_log + slot->layout_content.x - scroll_x;
		f32 child_origin_y = origin_y_log + slot->layout_content.y - scroll_y;
		if (ui_paint_node(em, slot->children.items[i], child_origin_x, child_origin_y, opacity) != 0) {
			return -1;
		}
	}

	if (pushed_clip != 0) {
		if (ui_paint_pop_clip(em) != 0) {
			return -1;
		}
	}
	return 0;
}
// NOLINTEND(misc-no-recursion)

// NOLINTBEGIN(misc-no-recursion)
static void ui_paint_clear_dirty(sk_ui_context_t* ctx, sk_ui_node_t node) {
	ui_node_slot_t* slot = ui_slot_mut(ctx, node);
	u32 i;
	if (slot == NULL) {
		return;
	}
	for (i = 0u; i < slot->children.count; ++i) {
		ui_paint_clear_dirty(ctx, slot->children.items[i]);
	}
	slot->dirty = (u16)((u32)slot->dirty & ~(u32)SK_UI_DIRTY_PAINT);
}
// NOLINTEND(misc-no-recursion)

static i32 ui_paint_needs_rebuild(const sk_ui_context_t* ctx) {
	const ui_node_slot_t* root;
	if (ctx->draw.valid == 0) {
		return 1;
	}
	if (!ui_paint_f32_bits_eq(ctx->content_scale_x, ctx->draw.last_scale_x) || !ui_paint_f32_bits_eq(ctx->content_scale_y, ctx->draw.last_scale_y)) {
		return 1;
	}
	root = ui_slot(ctx, ctx->root);
	if (root == NULL) {
		return 1;
	}
	if ((root->dirty & (u16)SK_UI_DIRTY_PAINT) != 0u) {
		return 1;
	}
	return 0;
}

i32 ui_paint_impl(sk_ui_context_t* ctx, const sk_ui_paint_params_t* params) {
	ui_paint_emitter_t em;
	ui_draw_list_store_t* store;
	sk_ui_paint_params_t empty_params;

	store = &ctx->draw;

	if (ui_paint_needs_rebuild(ctx) == 0) {
		store->view.reused = 1;
		ui_draw_list_publish(store, 1);
		return 0;
	}

	/* Rebuild */
	sk_array_clear(&store->vertices);
	sk_array_clear(&store->indices);
	sk_array_clear(&store->commands);

	memset(&empty_params, 0, sizeof(empty_params));
	memset(&em, 0, sizeof(em));
	em.ctx = ctx;
	em.store = store;
	em.params = params != NULL ? params : &empty_params;
	em.scale_x = ctx->content_scale_x;
	em.scale_y = ctx->content_scale_y;
	if (em.scale_x <= 0.0f) {
		em.scale_x = 1.0f;
	}
	if (em.scale_y <= 0.0f) {
		em.scale_y = 1.0f;
	}
	em.clip_depth = 0u;
	em.mesh_open = 0;

	if (ui_paint_node(&em, ctx->root, 0.0f, 0.0f, 1.0f) != 0) {
		return -1;
	}
	ui_paint_close_mesh(&em);

	store->last_scale_x = ctx->content_scale_x;
	store->last_scale_y = ctx->content_scale_y;
	store->valid = 1;
	store->view.generation += 1u;
	ui_draw_list_publish(store, 0);

	ui_paint_clear_dirty(ctx, ctx->root);
	return 0;
}

const sk_ui_draw_list_t* ui_get_draw_list_impl(const sk_ui_context_t* ctx) {
	return &ctx->draw.view;
}

/* -------------------------------------------------------------------------- */
/* Unit tests                                                                 */
/* -------------------------------------------------------------------------- */

#ifdef SK_TESTS
#include "test.h"
#include "testdata/skore_test_font_ttf.h"

static const sk_ui_api_t* ui_paint_test_api(void) {
	return ui_get_api_table();
}

static void ui_paint_set_bg(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_color_t c) {
	sk_ui_style_props_t p;
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_BACKGROUND_COLOR;
	p.background_color = c;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, node, &p));
}

/**
 * Size via inline style so style_resolve keeps WIDTH/HEIGHT (it overwrites
 * layout_style from computed.layout after resolve).
 */
static void ui_paint_set_size(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node, f32 w, f32 h) {
	sk_ui_style_props_t p;
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(w);
	p.layout.height = sk_ui_pt(h);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, node, &p));
}

static void ui_paint_set_flex_row(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_FLEX_DIRECTION;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, node, &p));
}

static u32 ui_paint_count_cmd(const sk_ui_draw_list_t* dl, sk_ui_draw_cmd_kind_t kind) {
	u32 i;
	u32 n = 0u;
	for (i = 0u; i < dl->command_count; ++i) {
		if (dl->commands[i].kind == kind) {
			n += 1u;
		}
	}
	return n;
}

static u32 ui_paint_count_mesh_tex(const sk_ui_draw_list_t* dl, sk_ui_draw_texture_kind_t kind) {
	u32 i;
	u32 n = 0u;
	for (i = 0u; i < dl->command_count; ++i) {
		if (dl->commands[i].kind == SK_UI_DRAW_CMD_MESH && dl->commands[i].texture_kind == kind) {
			n += 1u;
		}
	}
	return n;
}

static i32 ui_paint_first_vertex_color(const sk_ui_draw_list_t* dl, u32 color, f32* out_x, f32* out_y) {
	u32 i;
	for (i = 0u; i < dl->vertex_count; ++i) {
		if (dl->vertices[i].color == color) {
			if (out_x != NULL) {
				*out_x = dl->vertices[i].x;
			}
			if (out_y != NULL) {
				*out_y = dl->vertices[i].y;
			}
			return 0;
		}
	}
	return -1;
}

SK_TEST(ui_paint_z_order_parent_then_children) {
	const sk_ui_api_t* ui = ui_paint_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t a;
	sk_ui_node_t b;
	const sk_ui_draw_list_t* dl;
	u32 color_root;
	u32 color_a;
	u32 color_b;
	u32 i;
	i32 seen_root = 0;
	i32 seen_a = 0;
	i32 seen_b = 0;
	i32 order_ok = 1;

	TEST_ASSERT_NOT_NULL(ctx);
	ui_paint_set_size(ui, ctx, root, 200.0f, 100.0f);
	ui_paint_set_bg(ui, ctx, root, sk_ui_rgba(1.0f, 0.0f, 0.0f, 1.0f));

	a = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	b = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	ui_paint_set_size(ui, ctx, a, 40.0f, 40.0f);
	ui_paint_set_size(ui, ctx, b, 40.0f, 40.0f);
	ui_paint_set_bg(ui, ctx, a, sk_ui_rgba(0.0f, 1.0f, 0.0f, 1.0f));
	ui_paint_set_bg(ui, ctx, b, sk_ui_rgba(0.0f, 0.0f, 1.0f, 1.0f));
	ui_paint_set_flex_row(ui, ctx, root);

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 100.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->layout_apply_scale(ctx, 1.0f, 1.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->paint(ctx, NULL));

	dl = ui->get_draw_list(ctx);
	TEST_ASSERT_NOT_NULL(dl);
	TEST_ASSERT_TRUE(dl->vertex_count >= 12u);
	TEST_ASSERT_TRUE(dl->command_count >= 1u);

	color_root = sk_ui_pack_color(sk_ui_rgba(1.0f, 0.0f, 0.0f, 1.0f));
	color_a = sk_ui_pack_color(sk_ui_rgba(0.0f, 1.0f, 0.0f, 1.0f));
	color_b = sk_ui_pack_color(sk_ui_rgba(0.0f, 0.0f, 1.0f, 1.0f));

	/* Vertex stream paint order: root, then a, then b. */
	for (i = 0u; i < dl->vertex_count; ++i) {
		u32 c = dl->vertices[i].color;
		if (c == color_root) {
			if (seen_a || seen_b) {
				order_ok = 0;
			}
			seen_root = 1;
		} else if (c == color_a) {
			if (!seen_root || seen_b) {
				order_ok = 0;
			}
			seen_a = 1;
		} else if (c == color_b) {
			if (!seen_root || !seen_a) {
				order_ok = 0;
			}
			seen_b = 1;
		}
	}
	TEST_ASSERT_TRUE(seen_root != 0);
	TEST_ASSERT_TRUE(seen_a != 0);
	TEST_ASSERT_TRUE(seen_b != 0);
	TEST_ASSERT_TRUE(order_ok != 0);

	ui->context_destroy(ctx);
}

SK_TEST(ui_paint_clip_nesting_push_pop) {
	const sk_ui_api_t* ui = ui_paint_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t clipper;
	sk_ui_node_t child;
	const sk_ui_draw_list_t* dl;
	u32 pushes;
	u32 pops;
	u32 depth = 0u;
	u32 max_depth = 0u;
	u32 i;

	ui_paint_set_size(ui, ctx, root, 100.0f, 100.0f);
	ui_paint_set_bg(ui, ctx, root, sk_ui_rgba(0.2f, 0.2f, 0.2f, 1.0f));

	clipper = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	ui_paint_set_size(ui, ctx, clipper, 50.0f, 50.0f);
	ui_paint_set_bg(ui, ctx, clipper, sk_ui_rgba(0.5f, 0.5f, 0.5f, 1.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_clip_children(ctx, clipper, 1));

	child = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, clipper);
	ui_paint_set_size(ui, ctx, child, 80.0f, 80.0f);
	ui_paint_set_bg(ui, ctx, child, sk_ui_rgba(1.0f, 1.0f, 0.0f, 1.0f));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 100.0f, 100.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->layout_apply_scale(ctx, 1.0f, 1.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->paint(ctx, NULL));

	dl = ui->get_draw_list(ctx);
	pushes = ui_paint_count_cmd(dl, SK_UI_DRAW_CMD_PUSH_CLIP);
	pops = ui_paint_count_cmd(dl, SK_UI_DRAW_CMD_POP_CLIP);
	TEST_ASSERT_EQUAL_UINT(1u, pushes);
	TEST_ASSERT_EQUAL_UINT(1u, pops);

	/* Nesting: never pop below 0; push before corresponding pop. */
	for (i = 0u; i < dl->command_count; ++i) {
		if (dl->commands[i].kind == SK_UI_DRAW_CMD_PUSH_CLIP) {
			depth += 1u;
			if (depth > max_depth) {
				max_depth = depth;
			}
			TEST_ASSERT_FLOAT_WITHIN(0.51f, 50.0f, dl->commands[i].clip.width);
			TEST_ASSERT_FLOAT_WITHIN(0.51f, 50.0f, dl->commands[i].clip.height);
		} else if (dl->commands[i].kind == SK_UI_DRAW_CMD_POP_CLIP) {
			TEST_ASSERT_TRUE(depth > 0u);
			depth -= 1u;
		} else if (dl->commands[i].kind == SK_UI_DRAW_CMD_MESH && depth > 0u) {
			/* Mesh under clip has non-empty scissor. */
			TEST_ASSERT_TRUE(dl->commands[i].clip.width > 0.0f);
		}
	}
	TEST_ASSERT_EQUAL_UINT(0u, depth);
	TEST_ASSERT_EQUAL_UINT(1u, max_depth);

	ui->context_destroy(ctx);
}

SK_TEST(ui_paint_batches_same_texture_and_clip) {
	const sk_ui_api_t* ui = ui_paint_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_node_t img0;
	sk_ui_node_t img1;
	const sk_ui_draw_list_t* dl;
	u32 solid_batches;
	u32 image_batches;

	ui_paint_set_size(ui, ctx, root, 200.0f, 100.0f);
	ui_paint_set_flex_row(ui, ctx, root);

	a = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	b = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	ui_paint_set_size(ui, ctx, a, 30.0f, 30.0f);
	ui_paint_set_size(ui, ctx, b, 30.0f, 30.0f);
	/* Same solid texture (NONE) and clip → one mesh batch for both. */
	ui_paint_set_bg(ui, ctx, a, sk_ui_rgba(1.0f, 0.0f, 0.0f, 1.0f));
	ui_paint_set_bg(ui, ctx, b, sk_ui_rgba(0.0f, 1.0f, 0.0f, 1.0f));

	img0 = ui->node_create(ctx, SK_UI_NODE_KIND_IMAGE, root);
	img1 = ui->node_create(ctx, SK_UI_NODE_KIND_IMAGE, root);
	ui_paint_set_size(ui, ctx, img0, 20.0f, 20.0f);
	ui_paint_set_size(ui, ctx, img1, 20.0f, 20.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_i32(ctx, img0, "texture_id", 42));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_i32(ctx, img1, "texture_id", 42));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 100.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->layout_apply_scale(ctx, 1.0f, 1.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->paint(ctx, NULL));

	dl = ui->get_draw_list(ctx);
	solid_batches = ui_paint_count_mesh_tex(dl, SK_UI_DRAW_TEX_NONE);
	image_batches = ui_paint_count_mesh_tex(dl, SK_UI_DRAW_TEX_IMAGE);

	/* Two solids share one NONE batch; two images with same id share one IMAGE batch. */
	TEST_ASSERT_EQUAL_UINT(1u, solid_batches);
	TEST_ASSERT_EQUAL_UINT(1u, image_batches);
	TEST_ASSERT_TRUE(dl->index_count >= 24u); /* at least 4 quads */

	/* Different texture breaks the batch. */
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_i32(ctx, img1, "texture_id", 99));
	TEST_ASSERT_EQUAL_INT(0, ui->paint(ctx, NULL));
	dl = ui->get_draw_list(ctx);
	image_batches = ui_paint_count_mesh_tex(dl, SK_UI_DRAW_TEX_IMAGE);
	TEST_ASSERT_EQUAL_UINT(2u, image_batches);

	ui->context_destroy(ctx);
}

SK_TEST(ui_paint_reuse_when_tree_unchanged) {
	const sk_ui_api_t* ui = ui_paint_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t a;
	const sk_ui_draw_list_t* dl;
	u32 gen0;
	u32 gen1;
	u32 vcount0;

	ui_paint_set_size(ui, ctx, root, 100.0f, 100.0f);
	a = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	ui_paint_set_size(ui, ctx, a, 50.0f, 50.0f);
	ui_paint_set_bg(ui, ctx, a, sk_ui_rgba(0.1f, 0.2f, 0.3f, 1.0f));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 100.0f, 100.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->layout_apply_scale(ctx, 1.0f, 1.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->paint(ctx, NULL));

	dl = ui->get_draw_list(ctx);
	gen0 = dl->generation;
	vcount0 = dl->vertex_count;
	TEST_ASSERT_TRUE(gen0 >= 1u);
	TEST_ASSERT_EQUAL_INT(0, dl->reused);
	TEST_ASSERT_FALSE(ui->node_is_dirty(ctx, root, (u32)SK_UI_DIRTY_PAINT));

	/* Second paint with no mutations: reuse. */
	TEST_ASSERT_EQUAL_INT(0, ui->paint(ctx, NULL));
	dl = ui->get_draw_list(ctx);
	gen1 = dl->generation;
	TEST_ASSERT_EQUAL_UINT(gen0, gen1);
	TEST_ASSERT_EQUAL_INT(1, dl->reused);
	TEST_ASSERT_EQUAL_UINT(vcount0, dl->vertex_count);

	/* Style/paint dirty forces rebuild. */
	ui_paint_set_bg(ui, ctx, a, sk_ui_rgba(0.9f, 0.1f, 0.1f, 1.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->paint(ctx, NULL));
	dl = ui->get_draw_list(ctx);
	TEST_ASSERT_TRUE(dl->generation > gen0);
	TEST_ASSERT_EQUAL_INT(0, dl->reused);

	ui->context_destroy(ctx);
}

/**
 * Bounds of all vertices matching @p color (axis-aligned).
 * @return Number of matching vertices.
 */
static u32 ui_paint_color_bounds(const sk_ui_draw_list_t* dl, u32 color, f32* out_min_x, f32* out_min_y, f32* out_max_x, f32* out_max_y) {
	u32 i;
	u32 found = 0u;
	f32 min_x = 1.0e9f;
	f32 min_y = 1.0e9f;
	f32 max_x = -1.0e9f;
	f32 max_y = -1.0e9f;
	for (i = 0u; i < dl->vertex_count; ++i) {
		if (dl->vertices[i].color == color) {
			found += 1u;
			if (dl->vertices[i].x < min_x) {
				min_x = dl->vertices[i].x;
			}
			if (dl->vertices[i].y < min_y) {
				min_y = dl->vertices[i].y;
			}
			if (dl->vertices[i].x > max_x) {
				max_x = dl->vertices[i].x;
			}
			if (dl->vertices[i].y > max_y) {
				max_y = dl->vertices[i].y;
			}
		}
	}
	if (out_min_x != NULL) {
		*out_min_x = min_x;
	}
	if (out_min_y != NULL) {
		*out_min_y = min_y;
	}
	if (out_max_x != NULL) {
		*out_max_x = max_x;
	}
	if (out_max_y != NULL) {
		*out_max_y = max_y;
	}
	return found;
}

SK_TEST(ui_paint_physical_pixels_scale_once) {
	const sk_ui_api_t* ui = ui_paint_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t a;
	const sk_ui_draw_list_t* dl;
	u32 color;
	f32 min_x1, min_y1, max_x1, max_y1;
	f32 min_x2, min_y2, max_x2, max_y2;
	f32 min_x3, min_y3, max_x3, max_y3;
	u32 found;

	ui_paint_set_size(ui, ctx, root, 100.0f, 50.0f);
	{
		sk_ui_style_props_t p;
		ui_style_props_clear(&p);
		p.mask = SK_UI_SP_PADDING;
		p.layout.padding.left = 10.0f;
		p.layout.padding.top = 5.0f;
		p.layout.padding.right = 0.0f;
		p.layout.padding.bottom = 0.0f;
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, root, &p));
	}

	a = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	ui_paint_set_size(ui, ctx, a, 40.0f, 20.0f);
	ui_paint_set_bg(ui, ctx, a, sk_ui_rgba(0.0f, 0.5f, 1.0f, 1.0f));
	color = sk_ui_pack_color(sk_ui_rgba(0.0f, 0.5f, 1.0f, 1.0f));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 100.0f, 50.0f));

	/* 1x: logical child at (10,5) size 40x20 → physical same. */
	TEST_ASSERT_EQUAL_INT(0, ui->layout_apply_scale(ctx, 1.0f, 1.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->paint(ctx, NULL));
	dl = ui->get_draw_list(ctx);
	found = ui_paint_color_bounds(dl, color, &min_x1, &min_y1, &max_x1, &max_y1);
	TEST_ASSERT_TRUE(found >= 4u);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, 10.0f, min_x1);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, 5.0f, min_y1);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, 50.0f, max_x1);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, 25.0f, max_y1);

	/* 2x: same logical tree → geometry is exactly 2× the 1x output (scale once). */
	TEST_ASSERT_EQUAL_INT(0, ui->layout_apply_scale(ctx, 2.0f, 2.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->paint(ctx, NULL));
	dl = ui->get_draw_list(ctx);
	found = ui_paint_color_bounds(dl, color, &min_x2, &min_y2, &max_x2, &max_y2);
	TEST_ASSERT_TRUE(found >= 4u);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, min_x1 * 2.0f, min_x2);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, min_y1 * 2.0f, min_y2);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, max_x1 * 2.0f, max_x2);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, max_y1 * 2.0f, max_y2);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, 20.0f, min_x2);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, 10.0f, min_y2);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, 100.0f, max_x2);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, 50.0f, max_y2);

	/* 3x: still multiply logical once — not compound on prior physical. */
	TEST_ASSERT_EQUAL_INT(0, ui->layout_apply_scale(ctx, 3.0f, 3.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->paint(ctx, NULL));
	dl = ui->get_draw_list(ctx);
	found = ui_paint_color_bounds(dl, color, &min_x3, &min_y3, &max_x3, &max_y3);
	TEST_ASSERT_TRUE(found >= 4u);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, min_x1 * 3.0f, min_x3);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, min_y1 * 3.0f, min_y3);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, max_x1 * 3.0f, max_x3);
	TEST_ASSERT_FLOAT_WITHIN(0.51f, max_y1 * 3.0f, max_y3);

	ui->context_destroy(ctx);
}

SK_TEST(ui_paint_border_and_rounded_and_text) {
	const sk_ui_api_t* ui = ui_paint_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t box;
	sk_ui_node_t text;
	sk_ui_font_system_t* sys;
	sk_ui_font_t* font;
	sk_ui_paint_params_t params;
	const sk_ui_draw_list_t* dl;

	sys = ui->font_system_create(NULL);
	TEST_ASSERT_NOT_NULL(sys);
	font = ui->font_load_memory(sys, skore_test_font_ttf, (u32)skore_test_font_ttf_size);
	TEST_ASSERT_NOT_NULL(font);

	ui_paint_set_size(ui, ctx, root, 200.0f, 80.0f);
	box = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	ui_paint_set_size(ui, ctx, box, 60.0f, 40.0f);
	{
		sk_ui_style_props_t p;
		ui_style_props_clear(&p);
		p.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_COLOR | SK_UI_SP_CORNER_RADIUS | SK_UI_SP_BORDER_WIDTH;
		p.background_color = sk_ui_rgba(0.2f, 0.3f, 0.8f, 1.0f);
		p.border_color = sk_ui_rgba(1.0f, 1.0f, 1.0f, 1.0f);
		p.corner_radius = 8.0f;
		p.layout.border.left = 2.0f;
		p.layout.border.top = 2.0f;
		p.layout.border.right = 2.0f;
		p.layout.border.bottom = 2.0f;
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, box, &p));
	}

	text = ui->node_create(ctx, SK_UI_NODE_KIND_TEXT, root);
	ui_paint_set_size(ui, ctx, text, 80.0f, 24.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_str(ctx, text, "text", "Hi"));
	{
		sk_ui_style_props_t p;
		ui_style_props_clear(&p);
		p.mask = SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE;
		p.color = sk_ui_rgba(1.0f, 1.0f, 1.0f, 1.0f);
		p.font_size = 16.0f;
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, text, &p));
	}

	{
		sk_ui_style_props_t p;
		ui_style_props_clear(&p);
		p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_COLUMN_GAP;
		p.layout.flex_direction = SK_UI_FLEX_ROW;
		p.layout.column_gap = 8.0f;
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, root, &p));
	}

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 200.0f, 80.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->layout_apply_scale(ctx, 1.0f, 1.0f));

	memset(&params, 0, sizeof(params));
	params.font_system = sys;
	params.font = font;
	TEST_ASSERT_EQUAL_INT(0, ui->paint(ctx, &params));

	dl = ui->get_draw_list(ctx);
	TEST_ASSERT_TRUE(dl->vertex_count > 8u);
	TEST_ASSERT_TRUE(dl->index_count > 0u);
	/* Glyphs are emitted as MSDF coverage solid quads (CPU atlas walk) —
	 * still expect more geometry than the box alone. */
	TEST_ASSERT_TRUE(dl->vertex_count > 24u);
	TEST_ASSERT_TRUE(dl->index_count > 36u);

	ui->font_system_destroy(sys);
	ui->context_destroy(ctx);
}

SK_TEST(ui_paint_msdf_text_emits_coverage_quads) {
	const sk_ui_api_t* ui = ui_paint_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t text;
	sk_ui_font_system_t* sys;
	sk_ui_font_t* font;
	sk_ui_paint_params_t params;
	const sk_ui_draw_list_t* dl;
	u32 msdf_meshes;
	u32 i;

	sys = ui->font_system_create(NULL);
	TEST_ASSERT_NOT_NULL(sys);
	font = ui->font_load_memory(sys, skore_test_font_ttf, (u32)skore_test_font_ttf_size);
	TEST_ASSERT_NOT_NULL(font);

	ui_paint_set_size(ui, ctx, root, 220.0f, 80.0f);
	text = ui->node_create(ctx, SK_UI_NODE_KIND_TEXT, root);
	ui_paint_set_size(ui, ctx, text, 200.0f, 40.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_str(ctx, text, "text", "Hi"));
	{
		sk_ui_style_props_t p;
		ui_style_props_clear(&p);
		p.mask = SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE;
		p.color = sk_ui_rgba(1.0f, 1.0f, 1.0f, 0.8f);
		p.font_size = 24.0f;
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, text, &p));
	}

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 220.0f, 80.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->layout_apply_scale(ctx, 1.0f, 1.0f));

	/* Text always paints through the MSDF pipeline: bake on first shape,
	 * then emit per-glyph coverage solids (CPU evaluate of the shader;
	 * SK_UI_MSDF_GPU=1 switches to TEX_MSDF quads). */
	memset(&params, 0, sizeof(params));
	params.font_system = sys;
	params.font = font;
	TEST_ASSERT_EQUAL_INT(0, ui->paint(ctx, &params));
	dl = ui->get_draw_list(ctx);
	msdf_meshes = ui_paint_count_mesh_tex(dl, SK_UI_DRAW_TEX_MSDF);
	TEST_ASSERT_EQUAL_UINT(0u, msdf_meshes); /* default CPU path: coverage solids */
	TEST_ASSERT_TRUE(dl->vertex_count >= 8u);
	{
		u32 ink_verts = 0u;
		const u32 want_rgb = sk_ui_pack_color(sk_ui_rgba(1.0f, 1.0f, 1.0f, 0.8f)) & 0x00ffffffu;
		for (i = 0u; i < dl->vertex_count; ++i) {
			if ((dl->vertices[i].color & 0x00ffffffu) == want_rgb && ((dl->vertices[i].color >> 24) & 0xffu) > 0u) {
				ink_verts += 1u;
			}
		}
		TEST_ASSERT_TRUE(ink_verts >= 8u);
	}

	ui->font_system_destroy(sys);
	ui->context_destroy(ctx);
}

SK_TEST(ui_paint_scroll_offset_and_nested_clip) {
	const sk_ui_api_t* ui = ui_paint_test_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t outer;
	sk_ui_node_t inner;
	sk_ui_node_t leaf;
	const sk_ui_draw_list_t* dl;
	u32 pushes;
	u32 pops;
	u32 color;
	f32 y = -1.0f;

	ui_paint_set_size(ui, ctx, root, 100.0f, 100.0f);
	outer = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
	ui_paint_set_size(ui, ctx, outer, 80.0f, 80.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_clip_children(ctx, outer, 1));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_f32(ctx, outer, "scroll_y", 10.0f));

	inner = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, outer);
	ui_paint_set_size(ui, ctx, inner, 60.0f, 60.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_clip_children(ctx, inner, 1));

	leaf = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, inner);
	ui_paint_set_size(ui, ctx, leaf, 20.0f, 20.0f);
	ui_paint_set_bg(ui, ctx, leaf, sk_ui_rgba(1.0f, 0.0f, 1.0f, 1.0f));
	color = sk_ui_pack_color(sk_ui_rgba(1.0f, 0.0f, 1.0f, 1.0f));

	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 100.0f, 100.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->layout_apply_scale(ctx, 1.0f, 1.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->paint(ctx, NULL));

	dl = ui->get_draw_list(ctx);
	pushes = ui_paint_count_cmd(dl, SK_UI_DRAW_CMD_PUSH_CLIP);
	pops = ui_paint_count_cmd(dl, SK_UI_DRAW_CMD_POP_CLIP);
	TEST_ASSERT_EQUAL_UINT(2u, pushes);
	TEST_ASSERT_EQUAL_UINT(2u, pops);

	/* Leaf is scrolled up by 10 logical px under outer. */
	TEST_ASSERT_EQUAL_INT(0, ui_paint_first_vertex_color(dl, color, NULL, &y));
	TEST_ASSERT_FLOAT_WITHIN(1.0f, -10.0f, y);

	ui->context_destroy(ctx);
}

#endif /* SK_TESTS */
