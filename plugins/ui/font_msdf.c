/**
 * @file font_msdf.c
 * @brief MSDF glyph atlas generation via msdf-atlas-c (APX-265).
 *
 * Builds a scale-independent RGB8 multi-channel SDF atlas for the printable
 * ASCII charset, packs glyphs with a distance range of 2 px, and stores
 * per-glyph UV rects plus em-space plane bounds / advance / kerning. Layout
 * (APX-267) scales those em metrics to any pixel size from the single atlas.
 * FreeType R8 paint path is unchanged unless the text-renderer switch is MSDF.
 * Offline inspection via font_msdf_dump is unchanged.
 *
 * Memory: all msdf-atlas-c bitmaps/layouts are copied into skore-owned
 * storage before generator/font handles are destroyed (library owns those
 * pointers; never free them with free()).
 */

#include "ui_internal.h"

#include "allocator.h"
#include "path.h"

#include <msdf_atlas_c.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Constants (pinned by APX-264 audit / C API smoke)                          */
/* -------------------------------------------------------------------------- */

enum {
	UI_MSDF_CHANNELS = 3u,
	UI_MSDF_SPACING_PX = 2u, /* >= px_range; linear filter bleed margin */
};

#define UI_MSDF_PX_RANGE 2.0
#define UI_MSDF_EDGE_ANGLE 3.0
#define UI_MSDF_MITER 1.0
#define UI_MSDF_FONT_SCALE_EM 1.0
#define UI_MSDF_MIN_GLYPH_SCALE 32.0 /* px per em; 64² atlas crushes ASCII to 2px */
#define UI_MSDF_NOTDEF_ADVANCE_EM 0.60f
#define UI_MSDF_NOTDEF_L 0.08f
#define UI_MSDF_NOTDEF_B (-0.15f)
#define UI_MSDF_NOTDEF_R 0.52f
#define UI_MSDF_NOTDEF_T 0.75f
#define UI_MSDF_KERN_EPS 0.00001f

/* -------------------------------------------------------------------------- */
/* Live atlas                                                                 */
/* -------------------------------------------------------------------------- */

typedef struct ui_msdf_kern_pair_t {
	u32 left_cp;
	u32 right_cp;
	f32 kern_em;
} ui_msdf_kern_pair_t;

struct ui_msdf_atlas_live_t {
	u32 width;
	u32 height;
	u32 channels;
	u32 generation;
	f32 px_range;
	f32 pack_scale;
	f32 em_size;
	f32 ascender_em;
	f32 descender_em;
	f32 line_height_em;
	u8* pixels; /* RGB8, pitch = width * channels */
	sk_ui_msdf_glyph_t* glyphs;
	u32 glyph_count;
	ui_msdf_kern_pair_t* kerns;
	u32 kern_count;
	sk_ui_msdf_glyph_t notdef;
	i32 notdef_synthetic;
};

/* -------------------------------------------------------------------------- */
/* Release                                                                    */
/* -------------------------------------------------------------------------- */

void ui_msdf_atlas_release(const sk_allocator_t* a, ui_msdf_atlas_live_t* atlas) {
	if (atlas == NULL) {
		return;
	}
	if (a == NULL) {
		a = sk_allocator_default();
	}
	if (atlas->pixels != NULL) {
		a->free(a->instance, atlas->pixels);
		atlas->pixels = NULL;
	}
	if (atlas->glyphs != NULL) {
		a->free(a->instance, atlas->glyphs);
		atlas->glyphs = NULL;
	}
	if (atlas->kerns != NULL) {
		a->free(a->instance, atlas->kerns);
		atlas->kerns = NULL;
	}
	atlas->glyph_count = 0u;
	atlas->kern_count = 0u;
	a->free(a->instance, atlas);
}

/* -------------------------------------------------------------------------- */
/* Bake                                                                       */
/* -------------------------------------------------------------------------- */

static void ui_msdf_destroy_handles(msdf_atlas_font_t* font, msdf_atlas_charset_t* charset, msdf_atlas_glyphset_t* set, msdf_atlas_packer_t* packer,
									msdf_atlas_generator_t* generator) {
	/* Destroy accepts NULL; order: generator first (owns bitmap), then packer, set, charset, font. */
	(void)msdf_atlas_generator_destroy(generator);
	(void)msdf_atlas_packer_destroy(packer);
	(void)msdf_atlas_glyphset_destroy(set);
	(void)msdf_atlas_charset_destroy(charset);
	(void)msdf_atlas_font_destroy(font);
}

i32 ui_msdf_atlas_bake(const sk_allocator_t* a, const u8* ttf_bytes, u32 ttf_size, ui_msdf_atlas_live_t** out_atlas) {
	msdf_atlas_config_t config;
	msdf_atlas_font_t* msdf_font = NULL;
	msdf_atlas_charset_t* charset = NULL;
	msdf_atlas_glyphset_t* set = NULL;
	msdf_atlas_packer_t* packer = NULL;
	msdf_atlas_generator_t* generator = NULL;
	msdf_atlas_font_metrics_t font_metrics;
	msdf_atlas_bitmap_t bitmap;
	msdf_atlas_range_t packed_range;
	const msdf_atlas_glyph_layout_t* layouts = NULL;
	size_t layout_count = 0;
	int32_t loaded = 0;
	int32_t atlas_w = 0;
	int32_t atlas_h = 0;
	double pack_scale = 0.0;
	ui_msdf_atlas_live_t* atlas = NULL;
	size_t pix_bytes;
	size_t i;
	u32 y;

	if (out_atlas == NULL || ttf_bytes == NULL || ttf_size == 0u) {
		return -1;
	}
	*out_atlas = NULL;
	if (a == NULL) {
		a = sk_allocator_default();
	}

	memset(&config, 0, sizeof(config));
	if (msdf_atlas_config_default(&config) != MSDF_ATLAS_OK) {
		return -1;
	}
	config.image_type = MSDF_ATLAS_IMAGE_MSDF;
	/* Generate float, quantize ourselves so 0.5 stays mid-gray (RGB8 C-API
	 * conversion has been seen to drop interior medians to 0). */
	config.pixel_format = MSDF_ATLAS_PIXEL_RGB32F;
	config.y_direction = MSDF_ATLAS_Y_TOP_DOWN;
	config.packing_style = MSDF_ATLAS_PACKING_TIGHT;
	config.dimensions_constraint = MSDF_ATLAS_DIMENSIONS_POWER_OF_TWO_SQUARE;
	/* C API Range(lower, upper) is endpoints, not C++ Range(width).
	 * {2,2} is zero-width and yields NaN. {0,2} is width 2; we remap so
	 * distance 0 (edge) lands at unorm 0.5. */
	config.px_range.lower = 0.0;
	config.px_range.upper = UI_MSDF_PX_RANGE;
	config.min_glyph_scale = UI_MSDF_MIN_GLYPH_SCALE;
	config.miter_limit = UI_MSDF_MITER;
	/* Spacing between glyph boxes so linear filtering does not sample neighbors. */
	config.spacing = UI_MSDF_SPACING_PX;
	config.thread_count = 1;
	/* Overlap support without error-correction: the correction pass can
	 * write NaN/Inf into RGB32F on some glyphs, which quantize to empty. */
	config.flags = MSDF_ATLAS_CONFIG_ERROR_CORRECTION | MSDF_ATLAS_CONFIG_OVERLAP_SUPPORT;

	if (msdf_atlas_config_validate(&config) != MSDF_ATLAS_OK) {
		return -1;
	}

	if (msdf_atlas_font_open_memory(ttf_bytes, (size_t)ttf_size, &msdf_font) != MSDF_ATLAS_OK || msdf_font == NULL) {
		return -1;
	}
	memset(&font_metrics, 0, sizeof(font_metrics));
	if (msdf_atlas_font_get_metrics(msdf_font, &font_metrics) != MSDF_ATLAS_OK || font_metrics.em_size <= 0.0) {
		ui_msdf_destroy_handles(msdf_font, NULL, NULL, NULL, NULL);
		return -1;
	}

	if (msdf_atlas_charset_create_ascii(&charset) != MSDF_ATLAS_OK || charset == NULL) {
		ui_msdf_destroy_handles(msdf_font, NULL, NULL, NULL, NULL);
		return -1;
	}

	if (msdf_atlas_glyphset_create(&set) != MSDF_ATLAS_OK || set == NULL) {
		ui_msdf_destroy_handles(msdf_font, charset, NULL, NULL, NULL);
		return -1;
	}
	if (msdf_atlas_glyphset_load_charset(set, msdf_font, UI_MSDF_FONT_SCALE_EM, charset, 0u, &loaded) != MSDF_ATLAS_OK || loaded <= 0) {
		ui_msdf_destroy_handles(msdf_font, charset, set, NULL, NULL);
		return -1;
	}
	if (msdf_atlas_glyphset_edge_color(set, MSDF_ATLAS_EDGE_COLORING_INKTRAP, UI_MSDF_EDGE_ANGLE, 0ull) != MSDF_ATLAS_OK) {
		ui_msdf_destroy_handles(msdf_font, charset, set, NULL, NULL);
		return -1;
	}

	if (msdf_atlas_packer_create(&config, &packer) != MSDF_ATLAS_OK || packer == NULL) {
		ui_msdf_destroy_handles(msdf_font, charset, set, NULL, NULL);
		return -1;
	}
	if (msdf_atlas_packer_pack(packer, set) != MSDF_ATLAS_OK) {
		ui_msdf_destroy_handles(msdf_font, charset, set, packer, NULL);
		return -1;
	}
	if (msdf_atlas_packer_get_dimensions(packer, &atlas_w, &atlas_h) != MSDF_ATLAS_OK || atlas_w <= 0 || atlas_h <= 0) {
		ui_msdf_destroy_handles(msdf_font, charset, set, packer, NULL);
		return -1;
	}
	if (msdf_atlas_packer_get_scale(packer, &pack_scale) != MSDF_ATLAS_OK || pack_scale <= 0.0) {
		ui_msdf_destroy_handles(msdf_font, charset, set, packer, NULL);
		return -1;
	}
	memset(&packed_range, 0, sizeof(packed_range));
	if (msdf_atlas_packer_get_pixel_range(packer, &packed_range) != MSDF_ATLAS_OK) {
		ui_msdf_destroy_handles(msdf_font, charset, set, packer, NULL);
		return -1;
	}

	if (msdf_atlas_generator_create(&config, &generator) != MSDF_ATLAS_OK || generator == NULL) {
		ui_msdf_destroy_handles(msdf_font, charset, set, packer, NULL);
		return -1;
	}
	if (msdf_atlas_generator_generate(generator, set) != MSDF_ATLAS_OK) {
		ui_msdf_destroy_handles(msdf_font, charset, set, packer, generator);
		return -1;
	}

	memset(&bitmap, 0, sizeof(bitmap));
	if (msdf_atlas_generator_get_bitmap(generator, &bitmap) != MSDF_ATLAS_OK || bitmap.pixels == NULL || bitmap.width <= 0 || bitmap.height <= 0) {
		ui_msdf_destroy_handles(msdf_font, charset, set, packer, generator);
		return -1;
	}
	if (bitmap.channel_count != UI_MSDF_CHANNELS || bitmap.pixel_format != MSDF_ATLAS_PIXEL_RGB32F) {
		ui_msdf_destroy_handles(msdf_font, charset, set, packer, generator);
		return -1;
	}
	if (msdf_atlas_generator_get_layout_all(generator, &layouts, &layout_count) != MSDF_ATLAS_OK || layouts == NULL || layout_count == 0u) {
		ui_msdf_destroy_handles(msdf_font, charset, set, packer, generator);
		return -1;
	}

	/* Copy into skore-owned storage before destroying generator (invalidates bitmap/layouts). */
	atlas = (ui_msdf_atlas_live_t*)a->alloc(a->instance, sizeof(ui_msdf_atlas_live_t));
	if (atlas == NULL) {
		ui_msdf_destroy_handles(msdf_font, charset, set, packer, generator);
		return -1;
	}
	memset(atlas, 0, sizeof(*atlas));
	atlas->width = (u32)bitmap.width;
	atlas->height = (u32)bitmap.height;
	atlas->channels = UI_MSDF_CHANNELS;
	atlas->generation = 1u;
	atlas->px_range = (f32)(packed_range.upper > 0.0 ? packed_range.upper : UI_MSDF_PX_RANGE);
	atlas->pack_scale = (f32)pack_scale;
	atlas->em_size = (f32)font_metrics.em_size;
	atlas->ascender_em = (f32)(font_metrics.ascender_y / font_metrics.em_size);
	atlas->descender_em = (f32)(font_metrics.descender_y / font_metrics.em_size);
	atlas->line_height_em = (f32)(font_metrics.line_height / font_metrics.em_size);
	atlas->glyph_count = (u32)layout_count;

	pix_bytes = (size_t)atlas->width * (size_t)atlas->height * (size_t)atlas->channels;
	atlas->pixels = (u8*)a->alloc(a->instance, pix_bytes > 0u ? pix_bytes : 1u);
	if (atlas->pixels == NULL) {
		ui_msdf_atlas_release(a, atlas);
		ui_msdf_destroy_handles(msdf_font, charset, set, packer, generator);
		return -1;
	}
	/* Quantize signed pixel distances to RGB8 with edge at 0.5. */
	{
		const float* src = (const float*)bitmap.pixels;
		const size_t tight = (size_t)bitmap.width * (size_t)UI_MSDF_CHANNELS;
		const size_t stride_floats = bitmap.row_stride_bytes > 0 ? (size_t)bitmap.row_stride_bytes / sizeof(float) : tight;
		const float range = (float)UI_MSDF_PX_RANGE;
		for (y = 0u; y < atlas->height; ++y) {
			u32 x;
			const float* row = src + (size_t)y * stride_floats;
			u8* dst = atlas->pixels + (size_t)y * tight;
			for (x = 0u; x < atlas->width; ++x) {
				u32 c;
				for (c = 0u; c < UI_MSDF_CHANNELS; ++c) {
					float v = row[x * UI_MSDF_CHANNELS + c];
					float t;
					if (!isfinite((double)v) || range <= 0.0f) {
						t = 0.0f;
					} else {
						t = v / range;
						if (t < -1.0f) {
							t = -1.0f;
						} else if (t > 1.0f) {
							t = 1.0f;
						}
					}
					dst[x * UI_MSDF_CHANNELS + c] = (u8)((0.5f + 0.5f * t) * 255.0f + 0.5f);
				}
			}
		}
	}

	atlas->glyphs = (sk_ui_msdf_glyph_t*)a->alloc(a->instance, sizeof(sk_ui_msdf_glyph_t) * layout_count);
	if (atlas->glyphs == NULL) {
		ui_msdf_atlas_release(a, atlas);
		ui_msdf_destroy_handles(msdf_font, charset, set, packer, generator);
		return -1;
	}
	memset(atlas->glyphs, 0, sizeof(sk_ui_msdf_glyph_t) * layout_count);

	for (i = 0; i < layout_count; ++i) {
		const msdf_atlas_glyph_layout_t* L = &layouts[i];
		sk_ui_msdf_glyph_t* G = &atlas->glyphs[i];
		const f32 aw = (f32)atlas->width;
		const f32 ah = (f32)atlas->height;

		G->codepoint = L->codepoint;
		G->glyph_index = L->glyph_index >= 0 ? (u32)L->glyph_index : 0u;
		/* font_scale 1.0 load: advance and plane bounds are already em-normalized. */
		G->advance_em = (f32)L->advance;
		G->plane_l = (f32)L->plane_bounds_l;
		G->plane_b = (f32)L->plane_bounds_b;
		G->plane_r = (f32)L->plane_bounds_r;
		G->plane_t = (f32)L->plane_bounds_t;
		G->atlas_x = L->atlas_x;
		G->atlas_y = L->atlas_y;
		G->atlas_w = L->atlas_w;
		G->atlas_h = L->atlas_h;
		G->is_whitespace = L->is_whitespace;

		if (L->atlas_w > 0 && L->atlas_h > 0 && aw > 0.0f && ah > 0.0f) {
			/* Integer box matches packed placement; TOP_DOWN bitmap uses top-left origin. */
			G->u0 = (f32)L->atlas_x / aw;
			G->v0 = (f32)L->atlas_y / ah;
			G->u1 = (f32)(L->atlas_x + L->atlas_w) / aw;
			G->v1 = (f32)(L->atlas_y + L->atlas_h) / ah;
		}
	}

	/* Defined missing-glyph fallback: prefer font .notdef, else a box. */
	atlas->notdef_synthetic = 1;
	memset(&atlas->notdef, 0, sizeof(atlas->notdef));
	atlas->notdef.advance_em = UI_MSDF_NOTDEF_ADVANCE_EM;
	atlas->notdef.plane_l = UI_MSDF_NOTDEF_L;
	atlas->notdef.plane_b = UI_MSDF_NOTDEF_B;
	atlas->notdef.plane_r = UI_MSDF_NOTDEF_R;
	atlas->notdef.plane_t = UI_MSDF_NOTDEF_T;
	{
		i32 found_rank = 0;
		for (i = 0; i < layout_count; ++i) {
			const sk_ui_msdf_glyph_t* G = &atlas->glyphs[i];
			i32 rank = 0;
			if (G->glyph_index == 0u || G->codepoint == 0u) {
				rank = 2;
			} else if (G->codepoint == 0xFFFDu) {
				rank = 1;
			}
			if (rank <= found_rank) {
				continue;
			}
			found_rank = rank;
			atlas->notdef = *G;
			if (G->is_whitespace != 0 || G->atlas_w <= 0 || G->atlas_h <= 0) {
				atlas->notdef_synthetic = 1;
				if (atlas->notdef.advance_em <= 0.0f) {
					atlas->notdef.advance_em = UI_MSDF_NOTDEF_ADVANCE_EM;
				}
				if (!(atlas->notdef.plane_r > atlas->notdef.plane_l && atlas->notdef.plane_t > atlas->notdef.plane_b)) {
					atlas->notdef.plane_l = UI_MSDF_NOTDEF_L;
					atlas->notdef.plane_b = UI_MSDF_NOTDEF_B;
					atlas->notdef.plane_r = UI_MSDF_NOTDEF_R;
					atlas->notdef.plane_t = UI_MSDF_NOTDEF_T;
				}
			} else {
				atlas->notdef_synthetic = 0;
			}
			if (rank == 2) {
				break;
			}
		}
	}

	/* Drop pack/generate handles first (invalidates bitmap). Re-open a kerning
	 * glyphset so the packed atlas stays identical to a flags=0 bake. */
	ui_msdf_destroy_handles(msdf_font, charset, set, packer, generator);
	msdf_font = NULL;
	charset = NULL;
	set = NULL;
	packer = NULL;
	generator = NULL;

	if (msdf_atlas_font_open_memory(ttf_bytes, (size_t)ttf_size, &msdf_font) == MSDF_ATLAS_OK && msdf_font != NULL && msdf_atlas_charset_create_ascii(&charset) == MSDF_ATLAS_OK &&
		charset != NULL && msdf_atlas_glyphset_create(&set) == MSDF_ATLAS_OK && set != NULL &&
		msdf_atlas_glyphset_load_charset(set, msdf_font, UI_MSDF_FONT_SCALE_EM, charset, MSDF_ATLAS_LOAD_KERNING, &loaded) == MSDF_ATLAS_OK && loaded > 0) {
		const u32 n = atlas->glyph_count;
		u32 count = 0u;
		u32 li;
		u32 lj;
		if (n > 0u && n < 512u) {
			for (li = 0u; li < n; ++li) {
				if (atlas->glyphs[li].codepoint == 0u) {
					continue;
				}
				for (lj = 0u; lj < n; ++lj) {
					double adv = 0.0;
					f32 kern;
					if (atlas->glyphs[lj].codepoint == 0u) {
						continue;
					}
					if (msdf_atlas_glyphset_get_advance(set, atlas->glyphs[li].codepoint, atlas->glyphs[lj].codepoint, MSDF_ATLAS_IDENTIFIER_UNICODE_CODEPOINT, &adv) !=
						MSDF_ATLAS_OK) {
						continue;
					}
					kern = (f32)adv - atlas->glyphs[li].advance_em;
					if (kern > UI_MSDF_KERN_EPS || kern < -UI_MSDF_KERN_EPS) {
						count += 1u;
					}
				}
			}
			if (count > 0u) {
				atlas->kerns = (ui_msdf_kern_pair_t*)a->alloc(a->instance, sizeof(ui_msdf_kern_pair_t) * (size_t)count);
				if (atlas->kerns != NULL) {
					u32 w = 0u;
					for (li = 0u; li < n; ++li) {
						if (atlas->glyphs[li].codepoint == 0u) {
							continue;
						}
						for (lj = 0u; lj < n; ++lj) {
							double adv = 0.0;
							f32 kern;
							if (atlas->glyphs[lj].codepoint == 0u) {
								continue;
							}
							if (msdf_atlas_glyphset_get_advance(set, atlas->glyphs[li].codepoint, atlas->glyphs[lj].codepoint, MSDF_ATLAS_IDENTIFIER_UNICODE_CODEPOINT, &adv) !=
								MSDF_ATLAS_OK) {
								continue;
							}
							kern = (f32)adv - atlas->glyphs[li].advance_em;
							if (kern > UI_MSDF_KERN_EPS || kern < -UI_MSDF_KERN_EPS) {
								if (w < count) {
									atlas->kerns[w].left_cp = atlas->glyphs[li].codepoint;
									atlas->kerns[w].right_cp = atlas->glyphs[lj].codepoint;
									atlas->kerns[w].kern_em = kern;
									w += 1u;
								}
							}
						}
					}
					atlas->kern_count = w;
				}
			}
		}
	}
	ui_msdf_destroy_handles(msdf_font, charset, set, NULL, NULL);

	*out_atlas = atlas;
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Query                                                                      */
/* -------------------------------------------------------------------------- */

i32 ui_msdf_atlas_get_public(const ui_msdf_atlas_live_t* atlas, sk_ui_msdf_atlas_t* out) {
	if (atlas == NULL || out == NULL) {
		return -1;
	}
	out->width = atlas->width;
	out->height = atlas->height;
	out->channels = atlas->channels;
	out->glyph_count = atlas->glyph_count;
	out->generation = atlas->generation;
	out->px_range = atlas->px_range;
	out->pack_scale = atlas->pack_scale;
	out->em_size = atlas->em_size;
	out->ascender_em = atlas->ascender_em;
	out->descender_em = atlas->descender_em;
	out->line_height_em = atlas->line_height_em;
	out->pixels = atlas->pixels;
	return 0;
}

i32 ui_msdf_atlas_find_codepoint(const ui_msdf_atlas_live_t* atlas, u32 codepoint, sk_ui_msdf_glyph_t* out) {
	u32 i;
	if (atlas == NULL || out == NULL || atlas->glyphs == NULL) {
		return -1;
	}
	for (i = 0u; i < atlas->glyph_count; ++i) {
		if (atlas->glyphs[i].codepoint == codepoint) {
			*out = atlas->glyphs[i];
			return 0;
		}
	}
	return -1;
}

static f32 ui_msdf_kerning_em(const ui_msdf_atlas_live_t* atlas, u32 left_cp, u32 right_cp) {
	u32 i;
	if (atlas == NULL || atlas->kerns == NULL || left_cp == 0u || right_cp == 0u) {
		return 0.0f;
	}
	for (i = 0u; i < atlas->kern_count; ++i) {
		if (atlas->kerns[i].left_cp == left_cp && atlas->kerns[i].right_cp == right_cp) {
			return atlas->kerns[i].kern_em;
		}
	}
	return 0.0f;
}

static i32 ui_msdf_resolve_glyph(const ui_msdf_atlas_live_t* atlas, u32 codepoint, sk_ui_msdf_glyph_t* out, i32* out_fallback, i32* out_synthetic) {
	if (atlas == NULL || out == NULL) {
		return -1;
	}
	if (ui_msdf_atlas_find_codepoint(atlas, codepoint, out) == 0) {
		if (out_fallback != NULL) {
			*out_fallback = 0;
		}
		if (out_synthetic != NULL) {
			*out_synthetic = 0;
		}
		return 0;
	}
	*out = atlas->notdef;
	if (out_fallback != NULL) {
		*out_fallback = 1;
	}
	if (out_synthetic != NULL) {
		*out_synthetic = atlas->notdef_synthetic;
	}
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Text layout (MSDF atlas metrics, scaled to any pixel size)                 */
/* -------------------------------------------------------------------------- */

i32 ui_text_utf8_next(const u8* s, size_t len, size_t* index, u32* out_cp) {
	size_t i;
	u8 c0;
	if (s == NULL || index == NULL || out_cp == NULL) {
		return 0;
	}
	i = *index;
	if (i >= len) {
		return 0;
	}
	c0 = s[i];
	if (c0 < 0x80u) {
		*out_cp = (u32)c0;
		*index = i + 1u;
		return 1;
	}
	if ((c0 & 0xE0u) == 0xC0u && i + 1u < len && (s[i + 1u] & 0xC0u) == 0x80u) {
		*out_cp = ((u32)(c0 & 0x1Fu) << 6) | (u32)(s[i + 1u] & 0x3Fu);
		*index = i + 2u;
		return 1;
	}
	if ((c0 & 0xF0u) == 0xE0u && i + 2u < len && (s[i + 1u] & 0xC0u) == 0x80u && (s[i + 2u] & 0xC0u) == 0x80u) {
		*out_cp = ((u32)(c0 & 0x0Fu) << 12) | ((u32)(s[i + 1u] & 0x3Fu) << 6) | (u32)(s[i + 2u] & 0x3Fu);
		*index = i + 3u;
		return 1;
	}
	if ((c0 & 0xF8u) == 0xF0u && i + 3u < len && (s[i + 1u] & 0xC0u) == 0x80u && (s[i + 2u] & 0xC0u) == 0x80u && (s[i + 3u] & 0xC0u) == 0x80u) {
		*out_cp = ((u32)(c0 & 0x07u) << 18) | ((u32)(s[i + 1u] & 0x3Fu) << 12) | ((u32)(s[i + 2u] & 0x3Fu) << 6) | (u32)(s[i + 3u] & 0x3Fu);
		*index = i + 4u;
		return 1;
	}
	*out_cp = (u32)c0;
	*index = i + 1u;
	return 0;
}

static i32 ui_text_layout_ensure_msdf(sk_ui_font_t* font) {
	if (ui_font_msdf_ptr(font) != NULL) {
		return 0;
	}
	return ui_font_msdf_bake_impl(font);
}

i32 ui_text_layout_metrics(sk_ui_font_t* font, f32 px, i32 use_msdf, sk_ui_font_metrics_t* out) {
	if (font == NULL || out == NULL || px <= 0.0f) {
		return -1;
	}
	if (use_msdf != 0) {
		const ui_msdf_atlas_live_t* atlas;
		if (ui_text_layout_ensure_msdf(font) != 0) {
			return -1;
		}
		atlas = ui_font_msdf_ptr(font);
		if (atlas == NULL) {
			return -1;
		}
		out->ascent = atlas->ascender_em * px;
		out->descent = atlas->descender_em * px;
		out->line_height = atlas->line_height_em * px;
		out->pixel_size = px;
		return 0;
	}
	{
		const u32 px_u = (u32)(px + 0.5f);
		return ui_font_get_metrics_impl(font, px_u < 1u ? 1u : px_u, out);
	}
}

i32 ui_text_layout_shape(sk_ui_font_system_t* sys, sk_ui_font_t* font, f32 px, u32 prev_cp, u32 cp, i32 use_msdf, ui_text_layout_glyph_t* out) {
	if (font == NULL || out == NULL || px <= 0.0f) {
		return -1;
	}
	memset(out, 0, sizeof(*out));
	out->codepoint = cp;
	if (use_msdf != 0) {
		const ui_msdf_atlas_live_t* atlas;
		sk_ui_msdf_glyph_t mg;
		i32 fallback = 0;
		i32 synthetic = 0;
		f32 kern_em;
		if (ui_text_layout_ensure_msdf(font) != 0) {
			return -1;
		}
		atlas = ui_font_msdf_ptr(font);
		if (atlas == NULL || ui_msdf_resolve_glyph(atlas, cp, &mg, &fallback, &synthetic) != 0) {
			return -1;
		}
		kern_em = (fallback == 0) ? ui_msdf_kerning_em(atlas, prev_cp, cp) : 0.0f;
		out->glyph_index = mg.glyph_index;
		out->kerning_x = kern_em * px;
		out->advance_x = mg.advance_em * px + out->kerning_x;
		out->bearing_x = mg.plane_l * px;
		out->bearing_y = mg.plane_t * px;
		out->quad_l = mg.plane_l * px;
		out->quad_b = mg.plane_b * px;
		out->quad_r = mg.plane_r * px;
		out->quad_t = mg.plane_t * px;
		out->u0 = mg.u0;
		out->v0 = mg.v0;
		out->u1 = mg.u1;
		out->v1 = mg.v1;
		out->is_fallback = fallback;
		out->is_whitespace = mg.is_whitespace;
		if (fallback != 0 && synthetic != 0) {
			out->has_quad = 0;
			out->is_whitespace = 0;
		} else if (mg.is_whitespace == 0 && mg.atlas_w > 0 && mg.atlas_h > 0) {
			out->has_quad = 1;
		}
		return 0;
	}
	{
		u32 gi;
		sk_ui_glyph_t g;
		const u32 px_u = (u32)(px + 0.5f);
		const u32 pixel_size = px_u < 1u ? 1u : px_u;
		(void)prev_cp;
		if (sys == NULL) {
			return -1;
		}
		gi = ui_font_glyph_index_impl(font, cp);
		if (ui_font_get_glyph_impl(sys, font, pixel_size, gi, &g) != 0) {
			return -1;
		}
		out->glyph_index = gi;
		out->advance_x = g.advance_x;
		out->kerning_x = 0.0f;
		out->bearing_x = g.bearing_x;
		out->bearing_y = g.bearing_y;
		out->quad_l = g.bearing_x;
		out->quad_t = g.bearing_y;
		out->quad_r = g.bearing_x + (f32)g.width;
		out->quad_b = g.bearing_y - (f32)g.height;
		out->u0 = g.u0;
		out->v0 = g.v0;
		out->u1 = g.u1;
		out->v1 = g.v1;
		out->has_quad = (g.width > 0u && g.height > 0u) ? 1 : 0;
		out->is_fallback = (gi == 0u && cp != 0u) ? 1 : 0;
		out->is_whitespace = out->has_quad == 0 ? 1 : 0;
		return 0;
	}
}

f32 ui_text_layout_measure_width(sk_ui_font_system_t* sys, sk_ui_font_t* font, f32 px, const u8* begin, const u8* end, i32 use_msdf) {
	f32 w = 0.0f;
	u32 prev = 0u;
	size_t index = 0u;
	size_t len;
	if (begin == NULL || end == NULL || end < begin || px <= 0.0f) {
		return 0.0f;
	}
	len = (size_t)(end - begin);
	while (index < len) {
		u32 cp = 0u;
		ui_text_layout_glyph_t g;
		if (!ui_text_utf8_next(begin, len, &index, &cp)) {
			prev = 0u;
			continue;
		}
		if (cp == (u32)'\n') {
			break;
		}
		if (ui_text_layout_shape(sys, font, px, prev, cp, use_msdf, &g) != 0) {
			prev = 0u;
			continue;
		}
		w += g.advance_x;
		prev = g.is_fallback != 0 ? 0u : cp;
	}
	return w;
}

i32 ui_text_layout_measure_extent(sk_ui_font_system_t* sys, sk_ui_font_t* font, f32 px, const_chr_t utf8, i32 use_msdf, f32* out_advance, f32* out_min_x, f32* out_max_x,
								  f32* out_height) {
	sk_ui_font_metrics_t metrics;
	const u8* s;
	size_t len;
	size_t index = 0u;
	u32 prev = 0u;
	f32 pen = 0.0f;
	f32 min_x = 1.0e9f;
	f32 max_x = -1.0e9f;
	i32 any_quad = 0;
	if (font == NULL || px <= 0.0f) {
		return -1;
	}
	if (ui_text_layout_metrics(font, px, use_msdf, &metrics) != 0) {
		return -1;
	}
	s = (const u8*)(utf8 != NULL ? utf8 : "");
	len = strlen((const char*)s);
	while (index < len) {
		u32 cp = 0u;
		ui_text_layout_glyph_t g;
		if (!ui_text_utf8_next(s, len, &index, &cp)) {
			prev = 0u;
			continue;
		}
		if (cp == (u32)'\n') {
			break;
		}
		if (ui_text_layout_shape(sys, font, px, prev, cp, use_msdf, &g) != 0) {
			prev = 0u;
			continue;
		}
		if (g.has_quad != 0 || (g.is_fallback != 0 && g.is_whitespace == 0)) {
			const f32 l = pen + g.quad_l;
			const f32 r = pen + g.quad_r;
			if (l < min_x) {
				min_x = l;
			}
			if (r > max_x) {
				max_x = r;
			}
			any_quad = 1;
		}
		pen += g.advance_x;
		prev = g.is_fallback != 0 ? 0u : cp;
	}
	if (any_quad == 0) {
		min_x = 0.0f;
		max_x = pen;
	}
	if (out_advance != NULL) {
		*out_advance = pen;
	}
	if (out_min_x != NULL) {
		*out_min_x = min_x;
	}
	if (out_max_x != NULL) {
		*out_max_x = max_x;
	}
	if (out_height != NULL) {
		*out_height = metrics.line_height;
	}
	return 0;
}

i32 ui_text_layout_measure(sk_ui_font_system_t* sys, sk_ui_font_t* font, f32 px, const_chr_t utf8, i32 use_msdf, f32* out_width, f32* out_height) {
	return ui_text_layout_measure_extent(sys, font, px, utf8, use_msdf, out_width, NULL, NULL, out_height);
}

i32 ui_font_measure_text_impl(sk_ui_font_system_t* system, sk_ui_font_t* font, u32 pixel_size, const_chr_t utf8, f32* out_width, f32* out_height) {
	const i32 use_msdf = ui_get_text_renderer_impl() == SK_UI_TEXT_RENDERER_MSDF ? 1 : 0;
	if (system == NULL || font == NULL || pixel_size == 0u) {
		return -1;
	}
	return ui_text_layout_measure(system, font, (f32)pixel_size, utf8 != NULL ? utf8 : "", use_msdf, out_width, out_height);
}

/* -------------------------------------------------------------------------- */
/* Dump (debug / dev)                                                         */
/* -------------------------------------------------------------------------- */

static i32 ui_msdf_ensure_parent_dirs(const sk_filesystem_api_t* fs, const_chr_t path) {
	char parent[SK_FS_PATH_MAX];
	char partial[SK_FS_PATH_MAX];
	u32 i;
	u32 len;
	i32 n;

	if (fs == NULL || path == NULL || path[0] == '\0') {
		return 0;
	}
	n = sk_path_parent(sk_str_view_cstr(path), parent, (u32)sizeof(parent));
	if (n < 0) {
		return -1;
	}
	if (parent[0] == '\0') {
		return 0;
	}
	if (fs->get_file_status(parent) == SK_FILE_STATUS_DIRECTORY) {
		return 0;
	}
	len = 0u;
	for (i = 0u; parent[i] != '\0'; ++i) {
		const char c = parent[i];
		if (len + 1u >= (u32)sizeof(partial)) {
			return -1;
		}
		partial[len++] = c;
		partial[len] = '\0';
		if (c == '/' || c == '\\') {
			if (len > 1u && fs->get_file_status(partial) == SK_FILE_STATUS_NOT_FOUND) {
				if (fs->create_directory(partial) != 0) {
					return -1;
				}
			}
		}
	}
	if (fs->get_file_status(partial) == SK_FILE_STATUS_NOT_FOUND) {
		if (fs->create_directory(partial) != 0) {
			return -1;
		}
	}
	return 0;
}

static i32 ui_msdf_write_bytes(const_chr_t path, const void* data, size_t size) {
	FILE* f;
	size_t written;
	if (path == NULL || data == NULL) {
		return -1;
	}
	f = fopen(path, "wb");
	if (f == NULL) {
		return -1;
	}
	written = fwrite(data, 1, size, f);
	fclose(f);
	return written == size ? 0 : -1;
}

i32 ui_msdf_atlas_dump(const ui_msdf_atlas_live_t* atlas, const sk_filesystem_api_t* fs, const_chr_t path_prefix) {
	char raw_path[SK_FS_PATH_MAX];
	char json_path[SK_FS_PATH_MAX];
	char* json = NULL;
	size_t json_cap;
	size_t json_len = 0u;
	size_t raw_bytes;
	u32 i;
	int n;
	const sk_allocator_t* a;

	if (atlas == NULL || path_prefix == NULL || path_prefix[0] == '\0' || atlas->pixels == NULL) {
		return -1;
	}
	a = sk_allocator_default();

	n = snprintf(raw_path, sizeof(raw_path), "%s.raw", path_prefix);
	if (n <= 0 || (size_t)n >= sizeof(raw_path)) {
		return -1;
	}
	n = snprintf(json_path, sizeof(json_path), "%s.json", path_prefix);
	if (n <= 0 || (size_t)n >= sizeof(json_path)) {
		return -1;
	}

	if (ui_msdf_ensure_parent_dirs(fs, raw_path) != 0) {
		return -1;
	}

	raw_bytes = (size_t)atlas->width * (size_t)atlas->height * (size_t)atlas->channels;
	if (ui_msdf_write_bytes(raw_path, atlas->pixels, raw_bytes) != 0) {
		return -1;
	}

	/* Generous buffer: header + ~200 bytes per glyph. */
	json_cap = 2048u + (size_t)atlas->glyph_count * 256u;
	json = (char*)a->alloc(a->instance, json_cap);
	if (json == NULL) {
		return -1;
	}
	json[0] = '\0';

	n = snprintf(json, json_cap,
				 "{\n"
				 "  \"format\": \"msdf-rgb8\",\n"
				 "  \"width\": %u,\n"
				 "  \"height\": %u,\n"
				 "  \"channels\": %u,\n"
				 "  \"glyph_count\": %u,\n"
				 "  \"generation\": %u,\n"
				 "  \"px_range\": %.6g,\n"
				 "  \"pack_scale\": %.6g,\n"
				 "  \"em_size\": %.6g,\n"
				 "  \"ascender_em\": %.6g,\n"
				 "  \"descender_em\": %.6g,\n"
				 "  \"line_height_em\": %.6g,\n"
				 "  \"sampler\": { \"min\": \"linear\", \"mag\": \"linear\", \"mipmap\": \"none\", \"max_lod\": 0, \"address\": \"clamp_to_edge\" },\n"
				 "  \"glyphs\": [\n",
				 atlas->width, atlas->height, atlas->channels, atlas->glyph_count, atlas->generation, (double)atlas->px_range, (double)atlas->pack_scale, (double)atlas->em_size,
				 (double)atlas->ascender_em, (double)atlas->descender_em, (double)atlas->line_height_em);
	if (n < 0 || (size_t)n >= json_cap) {
		a->free(a->instance, json);
		return -1;
	}
	json_len = (size_t)n;

	for (i = 0u; i < atlas->glyph_count; ++i) {
		const sk_ui_msdf_glyph_t* G = &atlas->glyphs[i];
		const char* comma = (i + 1u < atlas->glyph_count) ? "," : "";
		n = snprintf(json + json_len, json_cap - json_len,
					 "    {\"codepoint\":%u,\"glyph_index\":%u,\"advance_em\":%.6g,"
					 "\"plane\":[%.6g,%.6g,%.6g,%.6g],"
					 "\"uv\":[%.6g,%.6g,%.6g,%.6g],"
					 "\"atlas\":[%d,%d,%d,%d],\"whitespace\":%s}%s\n",
					 G->codepoint, G->glyph_index, (double)G->advance_em, (double)G->plane_l, (double)G->plane_b, (double)G->plane_r, (double)G->plane_t, (double)G->u0,
					 (double)G->v0, (double)G->u1, (double)G->v1, G->atlas_x, G->atlas_y, G->atlas_w, G->atlas_h, G->is_whitespace ? "true" : "false", comma);
		if (n < 0 || (size_t)n >= json_cap - json_len) {
			a->free(a->instance, json);
			return -1;
		}
		json_len += (size_t)n;
	}

	n = snprintf(json + json_len, json_cap - json_len, "  ]\n}\n");
	if (n < 0 || (size_t)n >= json_cap - json_len) {
		a->free(a->instance, json);
		return -1;
	}
	json_len += (size_t)n;

	if (ui_msdf_write_bytes(json_path, json, json_len) != 0) {
		a->free(a->instance, json);
		return -1;
	}
	a->free(a->instance, json);
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Shader-matching coverage (APX-266)                                         */
/* -------------------------------------------------------------------------- */

f32 ui_msdf_median3(f32 r, f32 g, f32 b) {
	/* median(r,g,b) = max(min(r,g), min(max(r,g), b)) */
	const f32 mn = r < g ? r : g;
	const f32 mx = r > g ? r : g;
	const f32 mid = mx < b ? mx : (b < mn ? mn : b);
	return mid;
}

f32 ui_msdf_screen_px_range(f32 px_range, f32 atlas_w, f32 atlas_h, f32 fwidth_u, f32 fwidth_v) {
	f32 unit_u;
	f32 unit_v;
	f32 screen_u;
	f32 screen_v;
	f32 range;
	if (atlas_w <= 0.0f || atlas_h <= 0.0f) {
		return 1.0f;
	}
	unit_u = px_range / atlas_w;
	unit_v = px_range / atlas_h;
	screen_u = fwidth_u > 1.0e-8f ? 1.0f / fwidth_u : 0.0f;
	screen_v = fwidth_v > 1.0e-8f ? 1.0f / fwidth_v : 0.0f;
	/* 0.5 * dot(unitRange, screenTexSize); clamp so tiny text stays visible. */
	range = 0.5f * (unit_u * screen_u + unit_v * screen_v);
	return range < 1.0f ? 1.0f : range;
}

f32 ui_msdf_coverage(f32 median, f32 screen_px_range) {
	/* signed screen-space distance → smoothstep(-0.5, 0.5) clamped to [0,1]. */
	const f32 sd = screen_px_range * (median - 0.5f);
	f32 t = (sd + 0.5f); /* (sd - (-0.5)) / 1.0 */
	if (t < 0.0f) {
		t = 0.0f;
	} else if (t > 1.0f) {
		t = 1.0f;
	}
	return t * t * (3.0f - 2.0f * t);
}

static void ui_msdf_fetch_rgb(const sk_ui_msdf_atlas_t* atlas, i32 x, i32 y, f32 rgb[3]) {
	const u8* p;
	if (x < 0) {
		x = 0;
	} else if (x >= (i32)atlas->width) {
		x = (i32)atlas->width - 1;
	}
	if (y < 0) {
		y = 0;
	} else if (y >= (i32)atlas->height) {
		y = (i32)atlas->height - 1;
	}
	p = atlas->pixels + ((size_t)y * (size_t)atlas->width + (size_t)x) * (size_t)atlas->channels;
	rgb[0] = (f32)p[0] / 255.0f;
	rgb[1] = (f32)p[1] / 255.0f;
	rgb[2] = (f32)p[2] / 255.0f;
}

f32 ui_msdf_sample_median_bilinear(const sk_ui_msdf_atlas_t* atlas, f32 u, f32 v) {
	f32 fx;
	f32 fy;
	i32 x0;
	i32 y0;
	f32 tx;
	f32 ty;
	f32 c00[3];
	f32 c10[3];
	f32 c01[3];
	f32 c11[3];
	f32 r;
	f32 g;
	f32 b;
	if (atlas == NULL || atlas->pixels == NULL || atlas->width == 0u || atlas->height == 0u || atlas->channels < 3u) {
		return 0.0f;
	}
	if (u < 0.0f) {
		u = 0.0f;
	} else if (u > 1.0f) {
		u = 1.0f;
	}
	if (v < 0.0f) {
		v = 0.0f;
	} else if (v > 1.0f) {
		v = 1.0f;
	}
	fx = u * (f32)atlas->width - 0.5f;
	fy = v * (f32)atlas->height - 0.5f;
	x0 = (i32)fx;
	y0 = (i32)fy;
	if (fx < 0.0f) {
		x0 = (i32)(fx - 1.0f);
	}
	if (fy < 0.0f) {
		y0 = (i32)(fy - 1.0f);
	}
	tx = fx - (f32)x0;
	ty = fy - (f32)y0;
	ui_msdf_fetch_rgb(atlas, x0, y0, c00);
	ui_msdf_fetch_rgb(atlas, x0 + 1, y0, c10);
	ui_msdf_fetch_rgb(atlas, x0, y0 + 1, c01);
	ui_msdf_fetch_rgb(atlas, x0 + 1, y0 + 1, c11);
	r = c00[0] * (1.0f - tx) * (1.0f - ty) + c10[0] * tx * (1.0f - ty) + c01[0] * (1.0f - tx) * ty + c11[0] * tx * ty;
	g = c00[1] * (1.0f - tx) * (1.0f - ty) + c10[1] * tx * (1.0f - ty) + c01[1] * (1.0f - tx) * ty + c11[1] * tx * ty;
	b = c00[2] * (1.0f - tx) * (1.0f - ty) + c10[2] * tx * (1.0f - ty) + c01[2] * (1.0f - tx) * ty + c11[2] * tx * ty;
	return ui_msdf_median3(r, g, b);
}

f32 ui_msdf_sample_median_nearest(const sk_ui_msdf_atlas_t* atlas, f32 u, f32 v) {
	u32 x;
	u32 y;
	const u8* p;
	if (atlas == NULL || atlas->pixels == NULL || atlas->width == 0u || atlas->height == 0u || atlas->channels < 3u) {
		return 0.0f;
	}
	if (u < 0.0f) {
		u = 0.0f;
	} else if (u > 1.0f) {
		u = 1.0f;
	}
	if (v < 0.0f) {
		v = 0.0f;
	} else if (v > 1.0f) {
		v = 1.0f;
	}
	x = (u32)(u * (f32)(atlas->width - 1u) + 0.5f);
	y = (u32)(v * (f32)(atlas->height - 1u) + 0.5f);
	if (x >= atlas->width) {
		x = atlas->width - 1u;
	}
	if (y >= atlas->height) {
		y = atlas->height - 1u;
	}
	p = atlas->pixels + ((size_t)y * (size_t)atlas->width + (size_t)x) * (size_t)atlas->channels;
	return ui_msdf_median3((f32)p[0] / 255.0f, (f32)p[1] / 255.0f, (f32)p[2] / 255.0f);
}

/* -------------------------------------------------------------------------- */
/* Font API wrappers                                                          */
/* -------------------------------------------------------------------------- */

i32 ui_font_msdf_bake_impl(sk_ui_font_t* font) {
	const sk_allocator_t* a;
	ui_msdf_atlas_live_t* atlas = NULL;
	ui_msdf_atlas_live_t* prev;

	if (font == NULL) {
		return -1;
	}
	if (ui_font_msdf_ptr(font) != NULL) {
		return 0; /* already baked */
	}
	a = ui_font_allocator(font);
	if (ui_msdf_atlas_bake(a, ui_font_file_bytes(font), ui_font_file_size(font), &atlas) != 0 || atlas == NULL) {
		return -1;
	}
	prev = ui_font_msdf_ptr(font);
	if (prev != NULL) {
		ui_msdf_atlas_release(a, prev);
	}
	ui_font_msdf_set(font, atlas);
	return 0;
}

i32 ui_font_msdf_get_atlas_impl(const sk_ui_font_t* font, sk_ui_msdf_atlas_t* out) {
	return ui_msdf_atlas_get_public(ui_font_msdf_ptr(font), out);
}

i32 ui_font_msdf_get_glyph_impl(const sk_ui_font_t* font, u32 codepoint, sk_ui_msdf_glyph_t* out) {
	return ui_msdf_atlas_find_codepoint(ui_font_msdf_ptr(font), codepoint, out);
}

i32 ui_font_msdf_dump_impl(sk_ui_font_t* font, const sk_filesystem_api_t* fs, const_chr_t path_prefix) {
	if (font == NULL || path_prefix == NULL) {
		return -1;
	}
	if (ui_font_msdf_ptr(font) == NULL) {
		if (ui_font_msdf_bake_impl(font) != 0) {
			return -1;
		}
	}
	return ui_msdf_atlas_dump(ui_font_msdf_ptr(font), fs, path_prefix);
}

/* -------------------------------------------------------------------------- */
/* Tests                                                                      */
/* -------------------------------------------------------------------------- */

#ifdef SK_TESTS
#include "test.h"
#include "testdata/skore_test_font_ttf.h"

#include <errno.h>
#include <stdlib.h>
#if defined(_WIN32)
#include <direct.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#endif

static const sk_ui_api_t* ui_msdf_test_api(void) {
	return ui_get_api_table();
}

static i32 ui_msdf_file_exists(const_chr_t path) {
	FILE* f = fopen(path, "rb");
	if (f == NULL) {
		return 0;
	}
	fclose(f);
	return 1;
}

static size_t ui_msdf_file_byte_size(const_chr_t path) {
	FILE* f = fopen(path, "rb");
	long sz;
	if (f == NULL) {
		return 0u;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return 0u;
	}
	sz = ftell(f);
	fclose(f);
	return sz > 0 ? (size_t)sz : 0u;
}

/* Plugin TU cannot call sk_filesystem_api(); mock create_directory / status. */
static sk_file_status_t ui_msdf_mock_status(const_chr_t path) {
	struct stat st;
	if (path == NULL || path[0] == '\0') {
		return SK_FILE_STATUS_NOT_FOUND;
	}
	if (stat(path, &st) != 0) {
		return SK_FILE_STATUS_NOT_FOUND;
	}
	if (S_ISDIR(st.st_mode)) {
		return SK_FILE_STATUS_DIRECTORY;
	}
	if (S_ISREG(st.st_mode)) {
		return SK_FILE_STATUS_FILE;
	}
	return SK_FILE_STATUS_OTHER;
}

static i32 ui_msdf_mock_mkdir(const_chr_t path) {
#if defined(_WIN32)
	if (_mkdir(path) == 0) {
		return 0;
	}
#else
	if (mkdir(path, 0755) == 0) {
		return 0;
	}
	if (errno == EEXIST && ui_msdf_mock_status(path) == SK_FILE_STATUS_DIRECTORY) {
		return 0;
	}
#endif
	return ui_msdf_mock_status(path) == SK_FILE_STATUS_DIRECTORY ? 0 : -1;
}

static void ui_msdf_fill_mock_fs(sk_filesystem_api_t* fs) {
	memset(fs, 0, sizeof(*fs));
	fs->get_file_status = ui_msdf_mock_status;
	fs->create_directory = ui_msdf_mock_mkdir;
}

SK_TEST(ui_font_msdf_bake_ascii_atlas) {
	const sk_ui_api_t* ui = ui_msdf_test_api();
	sk_ui_font_system_t* sys = ui->font_system_create(NULL, 256u, 256u);
	sk_ui_font_t* font;
	sk_ui_msdf_atlas_t atlas;
	sk_ui_msdf_glyph_t ga;
	sk_ui_msdf_glyph_t gspace;
	u32 ink = 0u;
	u32 i;
	size_t total;

	TEST_ASSERT_NOT_NULL(sys);
	font = ui->font_load_memory(sys, skore_test_font_ttf, (u32)skore_test_font_ttf_size);
	TEST_ASSERT_NOT_NULL(font);

	TEST_ASSERT_EQUAL_INT(0, ui->font_msdf_bake(font));
	/* Idempotent second bake. */
	TEST_ASSERT_EQUAL_INT(0, ui->font_msdf_bake(font));

	TEST_ASSERT_EQUAL_INT(0, ui->font_msdf_get_atlas(font, &atlas));
	TEST_ASSERT_TRUE(atlas.width >= 64u);
	TEST_ASSERT_TRUE(atlas.height >= 64u);
	TEST_ASSERT_EQUAL_UINT(3u, atlas.channels);
	/* Printable ASCII is 95; test font may skip a few missing slots. */
	TEST_ASSERT_TRUE(atlas.glyph_count >= 30u);
	/* Printable ASCII is 95; bake may also include .notdef / U+FFFD. */
	TEST_ASSERT_TRUE(atlas.glyph_count <= 100u);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, atlas.px_range);
	TEST_ASSERT_TRUE(atlas.pack_scale > 0.0f);
	TEST_ASSERT_TRUE(atlas.em_size > 0.0f);
	TEST_ASSERT_TRUE(atlas.ascender_em > 0.0f);
	TEST_ASSERT_TRUE(atlas.line_height_em > 0.0f);
	TEST_ASSERT_NOT_NULL(atlas.pixels);

	total = (size_t)atlas.width * (size_t)atlas.height * (size_t)atlas.channels;
	for (i = 0u; i < (u32)total; ++i) {
		if (atlas.pixels[i] != 0u) {
			ink += 1u;
		}
	}
	/* RGB8 MSDF mid is ~128; any non-zero channel proves generation ran. */
	TEST_ASSERT_TRUE(ink > 0u);

	TEST_ASSERT_EQUAL_INT(0, ui->font_msdf_get_glyph(font, (u32)'A', &ga));
	TEST_ASSERT_EQUAL_UINT((u32)'A', ga.codepoint);
	/* Em-normalized advance should be a sensible fraction of an em. */
	TEST_ASSERT_TRUE(ga.advance_em > 0.05f);
	TEST_ASSERT_TRUE(ga.advance_em < 2.0f);
	TEST_ASSERT_TRUE(ga.is_whitespace == 0);
	TEST_ASSERT_TRUE(ga.atlas_w > 0);
	TEST_ASSERT_TRUE(ga.atlas_h > 0);
	TEST_ASSERT_TRUE(ga.u1 > ga.u0);
	TEST_ASSERT_TRUE(ga.v1 > ga.v0);
	TEST_ASSERT_TRUE(ga.plane_r > ga.plane_l);
	TEST_ASSERT_TRUE(ga.plane_t > ga.plane_b);

	TEST_ASSERT_EQUAL_INT(0, ui->font_msdf_get_glyph(font, 32u, &gspace));
	TEST_ASSERT_TRUE(gspace.is_whitespace != 0);
	TEST_ASSERT_TRUE(gspace.advance_em > 0.0f);

	/* FreeType path still works after MSDF bake. */
	{
		sk_ui_glyph_t ft;
		const u32 px = sk_ui_font_pixel_size(16.0f, 1.0f);
		const u32 gi = ui->font_glyph_index(font, (u32)'A');
		TEST_ASSERT_EQUAL_INT(0, ui->font_get_glyph(sys, font, px, gi, &ft));
		TEST_ASSERT_TRUE(ft.advance_x > 0.0f);
	}

	ui->font_system_destroy(sys);
}

SK_TEST(ui_font_msdf_dump_and_reload_cycle) {
	const sk_ui_api_t* ui = ui_msdf_test_api();
	sk_filesystem_api_t fs;
	sk_ui_font_system_t* sys;
	sk_ui_font_t* font;
	char root[SK_FS_PATH_MAX];
	char prefix[SK_FS_PATH_MAX];
	char raw_path[SK_FS_PATH_MAX];
	char json_path[SK_FS_PATH_MAX];
	u32 cycle;
	sk_ui_msdf_atlas_t atlas;
	u32 dump_w = 0u;
	u32 dump_h = 0u;

	ui_msdf_fill_mock_fs(&fs);
	TEST_ASSERT_EQUAL_INT(0, ui->test_artifact_root(&fs, root, (u32)sizeof(root)));
	if (fs.get_file_status(root) == SK_FILE_STATUS_NOT_FOUND) {
		TEST_ASSERT_EQUAL_INT(0, fs.create_directory(root));
	}

	TEST_ASSERT_TRUE(snprintf(prefix, sizeof(prefix), "%s/ui_msdf_atlas", root) > 0);
	TEST_ASSERT_TRUE(snprintf(raw_path, sizeof(raw_path), "%s.raw", prefix) > 0);
	TEST_ASSERT_TRUE(snprintf(json_path, sizeof(json_path), "%s.json", prefix) > 0);

	/* Repeated load → bake → dump → unload must not assert/leak (ASan when enabled). */
	for (cycle = 0u; cycle < 3u; ++cycle) {
		sys = ui->font_system_create(NULL, 128u, 128u);
		TEST_ASSERT_NOT_NULL(sys);
		font = ui->font_load_memory(sys, skore_test_font_ttf, (u32)skore_test_font_ttf_size);
		TEST_ASSERT_NOT_NULL(font);
		TEST_ASSERT_EQUAL_INT(0, ui->font_msdf_dump(font, &fs, prefix));
		TEST_ASSERT_EQUAL_INT(0, ui->font_msdf_get_atlas(font, &atlas));
		TEST_ASSERT_TRUE(atlas.glyph_count >= 30u);
		TEST_ASSERT_TRUE(atlas.width > 0u && atlas.height > 0u);
		dump_w = atlas.width;
		dump_h = atlas.height;
		ui->font_destroy(font);
		ui->font_system_destroy(sys);
	}

	TEST_ASSERT_TRUE(ui_msdf_file_exists(raw_path) != 0);
	TEST_ASSERT_TRUE(ui_msdf_file_exists(json_path) != 0);
	TEST_ASSERT_TRUE(ui_msdf_file_byte_size(raw_path) == (size_t)dump_w * (size_t)dump_h * 3u);
	TEST_ASSERT_TRUE(ui_msdf_file_byte_size(json_path) > 64u);

	/* JSON mentions glyph_count and px_range for manual inspection. */
	{
		FILE* jf = fopen(json_path, "rb");
		char buf[512];
		size_t nread;
		TEST_ASSERT_NOT_NULL(jf);
		nread = fread(buf, 1, sizeof(buf) - 1u, jf);
		buf[nread] = '\0';
		fclose(jf);
		TEST_ASSERT_NOT_NULL(strstr(buf, "\"glyph_count\""));
		TEST_ASSERT_NOT_NULL(strstr(buf, "\"px_range\""));
		TEST_ASSERT_NOT_NULL(strstr(buf, "msdf-rgb8"));
	}
}

SK_TEST(ui_msdf_coverage_screen_space_and_small_text_clamp) {
	/* Edge (median 0.5) is always ~0.5 coverage after the 1-px smoothstep. */
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, ui_msdf_coverage(0.5f, 1.0f));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, ui_msdf_coverage(0.5f, 8.0f));

	/* Inside / outside of a large glyph: full / empty after clamp to [0,1]. */
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, ui_msdf_coverage(1.0f, 4.0f));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, ui_msdf_coverage(0.0f, 4.0f));

	/* Median of RGB matches the shader helper (channel order independence). */
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.4f, ui_msdf_median3(0.1f, 0.4f, 0.9f));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.4f, ui_msdf_median3(0.9f, 0.1f, 0.4f));

	/* Tiny on-screen quad (large fwidth) would vanish without the 1-px clamp. */
	{
		const f32 unclamped_like = ui_msdf_screen_px_range(2.0f, 256.0f, 256.0f, 0.5f, 0.5f);
		const f32 tiny = ui_msdf_screen_px_range(2.0f, 256.0f, 256.0f, 2.0f, 2.0f);
		TEST_ASSERT_TRUE(unclamped_like >= 1.0f);
		TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, tiny);
		/* With the clamp, a clearly-inside sample stays visible. */
		TEST_ASSERT_TRUE(ui_msdf_coverage(0.8f, tiny) > 0.5f);
	}
}

SK_TEST(ui_msdf_atlas_letter_has_inside_and_outside) {
	const sk_ui_api_t* ui = ui_msdf_test_api();
	sk_ui_font_system_t* sys = ui->font_system_create(NULL, 256u, 256u);
	sk_ui_font_t* font;
	sk_ui_msdf_atlas_t atlas;
	sk_ui_msdf_glyph_t ga;
	f32 inside;
	f32 outside;

	TEST_ASSERT_NOT_NULL(sys);
	font = ui->font_load_memory(sys, skore_test_font_ttf, (u32)skore_test_font_ttf_size);
	TEST_ASSERT_NOT_NULL(font);
	TEST_ASSERT_EQUAL_INT(0, ui->font_msdf_bake(font));
	TEST_ASSERT_EQUAL_INT(0, ui->font_msdf_get_atlas(font, &atlas));
	TEST_ASSERT_EQUAL_INT(0, ui->font_msdf_get_glyph(font, (u32)'A', &ga));

	/* Capital A has a counter; the UV box center is often empty. Scan the
	 * packed rect for any interior median instead of assuming the midpoint. */
	inside = 0.0f;
	{
		f32 u;
		f32 v;
		const f32 du = (ga.u1 - ga.u0) / 8.0f;
		const f32 dv = (ga.v1 - ga.v0) / 8.0f;
		for (v = ga.v0; v <= ga.v1 + 1.0e-6f; v += dv > 0.0f ? dv : 1.0f) {
			for (u = ga.u0; u <= ga.u1 + 1.0e-6f; u += du > 0.0f ? du : 1.0f) {
				const f32 m = ui_msdf_sample_median_nearest(&atlas, u, v);
				if (m > inside) {
					inside = m;
				}
			}
		}
	}
	/* Just outside the packed box, toward atlas origin. */
	outside = ui_msdf_sample_median_nearest(&atlas, ga.u0 > 0.01f ? ga.u0 - 0.01f : 0.0f, ga.v0 > 0.01f ? ga.v0 - 0.01f : 0.0f);
	TEST_ASSERT_TRUE(inside > 0.5f);
	TEST_ASSERT_TRUE(outside < 0.55f);

	ui->font_system_destroy(sys);
}

SK_TEST(ui_text_renderer_switch_api) {
	const sk_ui_api_t* ui = ui_msdf_test_api();
	sk_ui_text_renderer_t prev = ui->get_text_renderer();

	ui->set_text_renderer(SK_UI_TEXT_RENDERER_MSDF);
	TEST_ASSERT_EQUAL_INT((i32)SK_UI_TEXT_RENDERER_MSDF, (i32)ui->get_text_renderer());
	ui->set_text_renderer(SK_UI_TEXT_RENDERER_FREETYPE);
	TEST_ASSERT_EQUAL_INT((i32)SK_UI_TEXT_RENDERER_FREETYPE, (i32)ui->get_text_renderer());
	ui->set_text_renderer(prev);
}

static u32 ui_msdf_rgb_bounds(const sk_ui_draw_list_t* dl, u32 rgb, f32* out_min_x, f32* out_min_y, f32* out_max_x, f32* out_max_y) {
	u32 i;
	u32 found = 0u;
	f32 min_x = 1.0e9f;
	f32 min_y = 1.0e9f;
	f32 max_x = -1.0e9f;
	f32 max_y = -1.0e9f;
	for (i = 0u; i < dl->vertex_count; ++i) {
		if ((dl->vertices[i].color & 0x00ffffffu) != rgb) {
			continue;
		}
		if (((dl->vertices[i].color >> 24) & 0xffu) == 0u) {
			continue;
		}
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

SK_TEST(ui_text_layout_msdf_measure_matches_rendered_extent) {
	const sk_ui_api_t* ui = ui_msdf_test_api();
	sk_ui_text_renderer_t prev = ui->get_text_renderer();
	sk_ui_font_system_t* sys = ui->font_system_create(NULL, 256u, 256u);
	sk_ui_font_t* font;
	const char* sample = "Hello AV";
	const f32 sizes[] = {12.0f, 16.0f, 24.0f, 20.0f};
	u32 si;

	TEST_ASSERT_NOT_NULL(sys);
	font = ui->font_load_memory(sys, skore_test_font_ttf, (u32)skore_test_font_ttf_size);
	TEST_ASSERT_NOT_NULL(font);
	ui->set_text_renderer(SK_UI_TEXT_RENDERER_MSDF);
	TEST_ASSERT_EQUAL_INT(0, ui->font_msdf_bake(font));

	for (si = 0u; si < (u32)(sizeof(sizes) / sizeof(sizes[0])); ++si) {
		const f32 px = sizes[si];
		f32 advance = 0.0f;
		f32 ext_min = 0.0f;
		f32 ext_max = 0.0f;
		f32 height = 0.0f;
		f32 api_w = 0.0f;
		f32 api_h = 0.0f;
		sk_ui_context_t* ctx;
		sk_ui_node_t root;
		sk_ui_node_t text;
		sk_ui_paint_params_t params;
		const sk_ui_draw_list_t* dl;
		sk_ui_rect_t content;
		u32 ink;
		f32 ink_min_x = 0.0f;
		f32 ink_min_y = 0.0f;
		f32 ink_max_x = 0.0f;
		f32 ink_max_y = 0.0f;
		const u32 rgb = sk_ui_pack_color(sk_ui_rgba(1.0f, 1.0f, 1.0f, 1.0f)) & 0x00ffffffu;
		const f32 tol = px * 0.20f + 2.0f;

		TEST_ASSERT_EQUAL_INT(0, ui_text_layout_measure_extent(sys, font, px, sample, 1, &advance, &ext_min, &ext_max, &height));
		TEST_ASSERT_TRUE(advance > 0.0f);
		TEST_ASSERT_TRUE(height > 0.0f);
		TEST_ASSERT_TRUE(ext_max > ext_min);
		TEST_ASSERT_EQUAL_INT(0, ui->font_measure_text(sys, font, (u32)(px + 0.5f), sample, &api_w, &api_h));
		/* Public API takes integer pixel_size; these cases are whole pixels. */
		TEST_ASSERT_FLOAT_WITHIN(0.05f, advance, api_w);

		ctx = ui->context_create(NULL);
		TEST_ASSERT_NOT_NULL(ctx);
		root = ui->context_root(ctx);
		{
			sk_ui_style_props_t p;
			ui_style_props_clear(&p);
			p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
			p.layout.width = sk_ui_pt(400.0f);
			p.layout.height = sk_ui_pt(80.0f);
			TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, root, &p));
		}
		text = ui->node_create(ctx, SK_UI_NODE_KIND_TEXT, root);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_str(ctx, text, "text", sample));
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_i32(ctx, text, "wrap", 0));
		{
			sk_ui_style_props_t p;
			ui_style_props_clear(&p);
			p.mask = SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
			p.color = sk_ui_rgba(1.0f, 1.0f, 1.0f, 1.0f);
			p.font_size = px;
			p.layout.width = sk_ui_pt(380.0f);
			p.layout.height = sk_ui_pt(60.0f);
			TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, text, &p));
		}
		TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
		TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 400.0f, 80.0f));
		TEST_ASSERT_EQUAL_INT(0, ui->layout_apply_scale(ctx, 1.0f, 1.0f));
		memset(&params, 0, sizeof(params));
		params.font_system = sys;
		params.font = font;
		params.text_renderer = SK_UI_TEXT_RENDERER_MSDF;
		TEST_ASSERT_EQUAL_INT(0, ui->paint(ctx, &params));
		dl = ui->get_draw_list(ctx);
		TEST_ASSERT_NOT_NULL(dl);
		ink = ui_msdf_rgb_bounds(dl, rgb, &ink_min_x, &ink_min_y, &ink_max_x, &ink_max_y);
		TEST_ASSERT_TRUE(ink >= 4u);
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, text, NULL, &content));
		/* Drawn ink sits inside the layout plane-bound span. */
		TEST_ASSERT_TRUE(ink_min_x + 0.51f >= content.x + ext_min - 1.0f);
		TEST_ASSERT_TRUE(ink_max_x <= content.x + ext_max + 1.0f);
		/* Measured advance matches the painted typographic width (pen / span). */
		TEST_ASSERT_FLOAT_WITHIN(tol, advance, ink_max_x - content.x);
		TEST_ASSERT_TRUE((ink_max_x - ink_min_x) > advance * 0.45f);
		ui->context_destroy(ctx);
	}

	ui->set_text_renderer(prev);
	ui->font_system_destroy(sys);
}

SK_TEST(ui_text_layout_msdf_scales_linearly_including_fractional) {
	const sk_ui_api_t* ui = ui_msdf_test_api();
	sk_ui_text_renderer_t prev = ui->get_text_renderer();
	sk_ui_font_system_t* sys = ui->font_system_create(NULL, 256u, 256u);
	sk_ui_font_t* font;
	const char* sample = "Spacing";
	f32 w16 = 0.0f;
	f32 w24 = 0.0f;
	f32 w125 = 0.0f;
	f32 w32 = 0.0f;
	f32 h16 = 0.0f;

	TEST_ASSERT_NOT_NULL(sys);
	font = ui->font_load_memory(sys, skore_test_font_ttf, (u32)skore_test_font_ttf_size);
	TEST_ASSERT_NOT_NULL(font);
	ui->set_text_renderer(SK_UI_TEXT_RENDERER_MSDF);
	TEST_ASSERT_EQUAL_INT(0, ui->font_msdf_bake(font));

	TEST_ASSERT_EQUAL_INT(0, ui_text_layout_measure(sys, font, 16.0f, sample, 1, &w16, &h16));
	TEST_ASSERT_EQUAL_INT(0, ui_text_layout_measure(sys, font, 24.0f, sample, 1, &w24, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui_text_layout_measure(sys, font, 12.5f, sample, 1, &w125, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui_text_layout_measure(sys, font, 32.0f, sample, 1, &w32, NULL));
	TEST_ASSERT_TRUE(w16 > 1.0f);
	/* Same atlas: widths scale with requested pixel size. */
	TEST_ASSERT_FLOAT_WITHIN(0.05f, w16 * 1.5f, w24);
	TEST_ASSERT_FLOAT_WITHIN(0.05f, w16 * 2.0f, w32);
	TEST_ASSERT_FLOAT_WITHIN(0.05f, w16 * (12.5f / 16.0f), w125);

	ui->set_text_renderer(prev);
	ui->font_system_destroy(sys);
}

SK_TEST(ui_text_layout_msdf_missing_glyph_uses_notdef) {
	const sk_ui_api_t* ui = ui_msdf_test_api();
	sk_ui_text_renderer_t prev = ui->get_text_renderer();
	sk_ui_font_system_t* sys = ui->font_system_create(NULL, 256u, 256u);
	sk_ui_font_t* font;
	f32 w_ab = 0.0f;
	f32 w_missing = 0.0f;
	f32 w_snow = 0.0f;
	ui_text_layout_glyph_t g;

	TEST_ASSERT_NOT_NULL(sys);
	font = ui->font_load_memory(sys, skore_test_font_ttf, (u32)skore_test_font_ttf_size);
	TEST_ASSERT_NOT_NULL(font);
	ui->set_text_renderer(SK_UI_TEXT_RENDERER_MSDF);
	TEST_ASSERT_EQUAL_INT(0, ui->font_msdf_bake(font));

	TEST_ASSERT_EQUAL_INT(0, ui_text_layout_measure(sys, font, 16.0f, "AB", 1, &w_ab, NULL));
	/* U+2603 SNOWMAN is outside the ASCII atlas. */
	TEST_ASSERT_EQUAL_INT(0, ui_text_layout_measure(sys, font, 16.0f,
													"A\xE2\x98\x83"
													"B",
													1, &w_missing, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui_text_layout_measure(sys, font, 16.0f, "\xE2\x98\x83", 1, &w_snow, NULL));
	TEST_ASSERT_TRUE(w_snow > 1.0f);
	TEST_ASSERT_TRUE(w_missing > w_ab);
	TEST_ASSERT_FLOAT_WITHIN(0.05f, w_ab + w_snow, w_missing);

	TEST_ASSERT_EQUAL_INT(0, ui_text_layout_shape(sys, font, 16.0f, 0u, 0x2603u, 1, &g));
	TEST_ASSERT_EQUAL_INT(1, g.is_fallback);
	TEST_ASSERT_TRUE(g.advance_x > 0.0f);
	TEST_ASSERT_TRUE(g.quad_r > g.quad_l);

	/* Exact lookup still reports missing; resolve/shape does not skip. */
	{
		sk_ui_msdf_glyph_t miss;
		TEST_ASSERT_TRUE(ui->font_msdf_get_glyph(font, 0x2603u, &miss) != 0);
	}

	ui->set_text_renderer(prev);
	ui->font_system_destroy(sys);
}

#endif /* SK_TESTS */
