/**
 * @file font_msdf.c
 * @brief MSDF glyph atlas generation via msdf-atlas-c (APX-265).
 *
 * Builds a scale-independent RGB8 multi-channel SDF atlas for the printable
 * ASCII charset, packs glyphs with a distance range of 2 px, and stores
 * per-glyph UV rects plus em-space plane bounds / advance. FreeType R8 paint
 * path is unchanged; this data is for the upcoming MSDF render path and for
 * offline inspection via font_msdf_dump.
 *
 * Memory: all msdf-atlas-c bitmaps/layouts are copied into skore-owned
 * storage before generator/font handles are destroyed (library owns those
 * pointers; never free them with free()).
 */

#include "ui_internal.h"

#include "allocator.h"
#include "path.h"

#include <msdf_atlas_c.h>

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

/* -------------------------------------------------------------------------- */
/* Live atlas                                                                 */
/* -------------------------------------------------------------------------- */

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
	atlas->glyph_count = 0u;
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
	config.pixel_format = MSDF_ATLAS_PIXEL_RGB8;
	config.y_direction = MSDF_ATLAS_Y_TOP_DOWN;
	config.packing_style = MSDF_ATLAS_PACKING_TIGHT;
	config.dimensions_constraint = MSDF_ATLAS_DIMENSIONS_POWER_OF_TWO_SQUARE;
	config.px_range.lower = UI_MSDF_PX_RANGE;
	config.px_range.upper = UI_MSDF_PX_RANGE;
	config.miter_limit = UI_MSDF_MITER;
	/* Spacing between glyph boxes so linear filtering does not sample neighbors. */
	config.spacing = UI_MSDF_SPACING_PX;
	config.thread_count = 1;
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
	if (bitmap.channel_count != UI_MSDF_CHANNELS || bitmap.pixel_format != MSDF_ATLAS_PIXEL_RGB8) {
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
	/* Copy rows respecting generator row_stride (may be > tight pitch). */
	{
		const u8* src = (const u8*)bitmap.pixels;
		const size_t tight = (size_t)bitmap.width * (size_t)UI_MSDF_CHANNELS;
		const size_t stride = bitmap.row_stride_bytes > 0 ? (size_t)bitmap.row_stride_bytes : tight;
		for (y = 0u; y < atlas->height; ++y) {
			memcpy(atlas->pixels + (size_t)y * tight, src + (size_t)y * stride, tight);
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

	/* Drop all msdf-atlas-c handles; pixels/layouts were copied. */
	ui_msdf_destroy_handles(msdf_font, charset, set, packer, generator);

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
	TEST_ASSERT_TRUE(atlas.glyph_count <= 95u);
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

#endif /* SK_TESTS */
