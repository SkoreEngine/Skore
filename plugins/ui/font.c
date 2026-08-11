/**
 * @file font.c
 * @brief FreeType glyph rasterization, stb_rect_pack atlas, and glyph cache.
 *
 * CPU-only text pipeline: load TTF/OTF via filesystem or memory, rasterize at
 * physical pixel size (logical size × content scale), pack into R8 atlas pages
 * (grow page or add pages when full), cache by (font, pixel_size, glyph_index).
 * GPU upload is intentionally out of scope (APX-134).
 */

#include "ui.h"
#include "ui_internal.h"

#include "allocator.h"
#include "array.h"
#include "hashmap.h"

#include <string.h>

/* Unity (via test.h) may define noreturn as _Noreturn; FreeType/UCRT break. */
#ifdef noreturn
#undef noreturn
#endif
#include <ft2build.h>
#include FT_FREETYPE_H
#include "stb_rect_pack.h"

/* -------------------------------------------------------------------------- */
/* Constants                                                                  */
/* -------------------------------------------------------------------------- */

enum {
	UI_FONT_DEFAULT_PAGE = 512u,
	UI_FONT_MAX_PAGE = 2048u,
	UI_FONT_PAD_PX = 1u, /* padding around each glyph in the atlas */
	UI_FONT_PACK_NODES_MIN = 64u,
};

/* -------------------------------------------------------------------------- */
/* Internal types                                                             */
/* -------------------------------------------------------------------------- */

typedef struct ui_atlas_page_live_t {
	u32 width;
	u32 height;
	u32 generation;
	u8* pixels; /* R8, width * height */
	stbrp_context pack_ctx;
	stbrp_node* pack_nodes;
	u32 pack_node_count;
} ui_atlas_page_live_t;

/* Pages are heap-allocated: stbrp_context embeds pointers into its own
 * extra[] nodes, so the context must not be relocated by array growth. */
typedef SK_ARRAY(ui_atlas_page_live_t*) ui_atlas_page_array_t;

struct sk_ui_font_t {
	sk_ui_font_system_t* system;
	u32 id;
	u8* file_bytes;
	u32 file_size;
	FT_Face face;
};

typedef SK_ARRAY(sk_ui_font_t*) ui_font_ptr_array_t;

typedef struct ui_glyph_cache_key_t {
	u32 font_id;
	u32 pixel_size;
	u32 glyph_index;
} ui_glyph_cache_key_t;

typedef SK_HASH_MAP(ui_glyph_cache_key_t, sk_ui_glyph_t) ui_glyph_cache_t;

struct sk_ui_font_system_t {
	const sk_allocator_t* allocator;
	FT_Library library;
	ui_font_ptr_array_t fonts;
	u32 next_font_id;
	ui_atlas_page_array_t pages;
	u32 page_width0;
	u32 page_height0;
	ui_glyph_cache_t cache;
	u32 cache_hits;
	u32 cache_misses;
};

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

static u32 ui_font_round_up_pow2(u32 v) {
	u32 p;
	if (v <= 1u) {
		return 1u;
	}
	p = 1u;
	while (p < v) {
		p <<= 1u;
		if (p == 0u) {
			return v;
		}
	}
	return p;
}

static void ui_atlas_page_release(const sk_allocator_t* a, ui_atlas_page_live_t* page) {
	if (page->pixels != NULL) {
		a->free(a->instance, page->pixels);
		page->pixels = NULL;
	}
	if (page->pack_nodes != NULL) {
		a->free(a->instance, page->pack_nodes);
		page->pack_nodes = NULL;
	}
	page->pack_node_count = 0u;
	page->width = 0u;
	page->height = 0u;
}

static i32 ui_atlas_page_init(const sk_allocator_t* a, ui_atlas_page_live_t* page, u32 width, u32 height) {
	u32 node_count;
	size_t pix_bytes;
	memset(page, 0, sizeof(*page));
	page->width = width;
	page->height = height;
	page->generation = 1u;
	pix_bytes = (size_t)width * (size_t)height;
	page->pixels = (u8*)a->alloc(a->instance, pix_bytes > 0u ? pix_bytes : 1u);
	if (page->pixels == NULL) {
		return -1;
	}
	memset(page->pixels, 0, pix_bytes);

	node_count = width;
	if (node_count < UI_FONT_PACK_NODES_MIN) {
		node_count = UI_FONT_PACK_NODES_MIN;
	}
	page->pack_nodes = (stbrp_node*)a->alloc(a->instance, sizeof(stbrp_node) * (size_t)node_count);
	if (page->pack_nodes == NULL) {
		a->free(a->instance, page->pixels);
		page->pixels = NULL;
		return -1;
	}
	page->pack_node_count = node_count;
	stbrp_init_target(&page->pack_ctx, (int)width, (int)height, page->pack_nodes, (int)node_count);
	return 0;
}

static u32 ui_atlas_page_index(const sk_ui_font_system_t* system, const ui_atlas_page_live_t* page) {
	u32 i;
	for (i = 0u; i < system->pages.count; ++i) {
		if (system->pages.items[i] == page) {
			return i;
		}
	}
	return 0u;
}

static i32 ui_atlas_page_grow(sk_ui_font_system_t* system, ui_atlas_page_live_t* page, u32 need_w, u32 need_h) {
	const sk_allocator_t* a = system->allocator;
	u32 new_w = page->width;
	u32 new_h = page->height;
	u8* old_pixels;
	u32 old_w;
	u32 old_h;
	u32 old_gen;
	stbrp_node* old_nodes;
	size_t new_bytes;
	u32 node_count;
	u32 y;
	u32 this_page;

	while (new_w < need_w && new_w < UI_FONT_MAX_PAGE) {
		new_w = new_w < 1u ? 64u : (new_w * 2u);
	}
	while (new_h < need_h && new_h < UI_FONT_MAX_PAGE) {
		new_h = new_h < 1u ? 64u : (new_h * 2u);
	}
	if (new_w > UI_FONT_MAX_PAGE) {
		new_w = UI_FONT_MAX_PAGE;
	}
	if (new_h > UI_FONT_MAX_PAGE) {
		new_h = UI_FONT_MAX_PAGE;
	}
	if (new_w < need_w || new_h < need_h) {
		return -1; /* single glyph larger than max page */
	}
	if (new_w == page->width && new_h == page->height) {
		return -1;
	}

	old_pixels = page->pixels;
	old_w = page->width;
	old_h = page->height;
	old_gen = page->generation;
	old_nodes = page->pack_nodes;
	this_page = ui_atlas_page_index(system, page);

	new_bytes = (size_t)new_w * (size_t)new_h;
	page->pixels = (u8*)a->alloc(a->instance, new_bytes);
	if (page->pixels == NULL) {
		page->pixels = old_pixels;
		return -1;
	}
	memset(page->pixels, 0, new_bytes);
	for (y = 0u; y < old_h; ++y) {
		memcpy(page->pixels + (size_t)y * (size_t)new_w, old_pixels + (size_t)y * (size_t)old_w, (size_t)old_w);
	}

	node_count = new_w;
	if (node_count < UI_FONT_PACK_NODES_MIN) {
		node_count = UI_FONT_PACK_NODES_MIN;
	}
	page->pack_nodes = (stbrp_node*)a->alloc(a->instance, sizeof(stbrp_node) * (size_t)node_count);
	if (page->pack_nodes == NULL) {
		a->free(a->instance, page->pixels);
		page->pixels = old_pixels;
		page->pack_nodes = old_nodes;
		return -1;
	}

	page->width = new_w;
	page->height = new_h;
	page->pack_node_count = node_count;
	page->generation = old_gen + 1u;
	/* Page is heap-stable: re-init packer in place, reserve old top-left region. */
	stbrp_init_target(&page->pack_ctx, (int)new_w, (int)new_h, page->pack_nodes, (int)node_count);
	if (old_w > 0u && old_h > 0u && old_w <= new_w && old_h <= new_h) {
		stbrp_rect reserved;
		memset(&reserved, 0, sizeof(reserved));
		reserved.id = -1;
		reserved.w = (stbrp_coord)old_w;
		reserved.h = (stbrp_coord)old_h;
		stbrp_pack_rects(&page->pack_ctx, &reserved, 1);
	}

	/* Rescale UVs for glyphs already packed on this page (bitmap top-left kept). */
	{
		const f32 su = (f32)old_w / (f32)new_w;
		const f32 sv = (f32)old_h / (f32)new_h;
		u32 slot;
		u8* controls = system->cache._hm.controls;
		sk_ui_glyph_t* values = (sk_ui_glyph_t*)system->cache._hm.values;
		const u32 cap = system->cache._hm.capacity;
		for (slot = 0u; slot < cap; ++slot) {
			if (controls[slot] != 1u) {
				continue;
			}
			if (values[slot].page_index == this_page) {
				values[slot].u0 *= su;
				values[slot].u1 *= su;
				values[slot].v0 *= sv;
				values[slot].v1 *= sv;
			}
		}
	}

	a->free(a->instance, old_pixels);
	a->free(a->instance, old_nodes);
	return 0;
}

static i32 ui_atlas_add_page(sk_ui_font_system_t* system, u32 width, u32 height) {
	const sk_allocator_t* a = system->allocator;
	ui_atlas_page_live_t* page = (ui_atlas_page_live_t*)a->alloc(a->instance, sizeof(ui_atlas_page_live_t));
	if (page == NULL) {
		return -1;
	}
	if (ui_atlas_page_init(a, page, width, height) != 0) {
		a->free(a->instance, page);
		return -1;
	}
	if (sk_array_push(&system->pages, page) != 0) {
		ui_atlas_page_release(a, page);
		a->free(a->instance, page);
		return -1;
	}
	return 0;
}

static i32 ui_atlas_pack_bitmap(sk_ui_font_system_t* system, const u8* src, u32 bw, u32 bh, u32 src_pitch, sk_ui_glyph_t* out) {
	stbrp_rect rect;
	u32 page_i;
	u32 need_w;
	u32 need_h;
	u32 y;

	if (bw == 0u || bh == 0u) {
		out->width = 0u;
		out->height = 0u;
		out->u0 = out->v0 = out->u1 = out->v1 = 0.0f;
		out->page_index = 0u;
		return 0;
	}

	need_w = bw + UI_FONT_PAD_PX * 2u;
	need_h = bh + UI_FONT_PAD_PX * 2u;

	for (;;) {
		if (system->pages.count == 0u) {
			u32 w = system->page_width0;
			u32 h = system->page_height0;
			if (w < need_w) {
				w = ui_font_round_up_pow2(need_w);
			}
			if (h < need_h) {
				h = ui_font_round_up_pow2(need_h);
			}
			if (w > UI_FONT_MAX_PAGE) {
				w = UI_FONT_MAX_PAGE;
			}
			if (h > UI_FONT_MAX_PAGE) {
				h = UI_FONT_MAX_PAGE;
			}
			if (ui_atlas_add_page(system, w, h) != 0) {
				return -1;
			}
		}

		page_i = system->pages.count - 1u;
		{
			ui_atlas_page_live_t* page = system->pages.items[page_i];
			memset(&rect, 0, sizeof(rect));
			rect.id = 0;
			rect.w = (stbrp_coord)need_w;
			rect.h = (stbrp_coord)need_h;
			stbrp_pack_rects(&page->pack_ctx, &rect, 1);
			if (rect.was_packed) {
				const u32 x0 = (u32)rect.x + UI_FONT_PAD_PX;
				const u32 y0 = (u32)rect.y + UI_FONT_PAD_PX;
				for (y = 0u; y < bh; ++y) {
					memcpy(page->pixels + (size_t)(y0 + y) * (size_t)page->width + (size_t)x0, src + (size_t)y * (size_t)src_pitch, (size_t)bw);
				}
				out->width = bw;
				out->height = bh;
				out->page_index = page_i;
				out->u0 = (f32)x0 / (f32)page->width;
				out->v0 = (f32)y0 / (f32)page->height;
				out->u1 = (f32)(x0 + bw) / (f32)page->width;
				out->v1 = (f32)(y0 + bh) / (f32)page->height;
				return 0;
			}

			/* Current page full: try grow, else new page. */
			if (page->width < UI_FONT_MAX_PAGE || page->height < UI_FONT_MAX_PAGE) {
				u32 grow_w = page->width * 2u;
				u32 grow_h = page->height * 2u;
				if (grow_w < need_w) {
					grow_w = ui_font_round_up_pow2(need_w);
				}
				if (grow_h < need_h) {
					grow_h = ui_font_round_up_pow2(need_h);
				}
				if (grow_w > UI_FONT_MAX_PAGE) {
					grow_w = UI_FONT_MAX_PAGE;
				}
				if (grow_h > UI_FONT_MAX_PAGE) {
					grow_h = UI_FONT_MAX_PAGE;
				}
				if (grow_w > page->width || grow_h > page->height) {
					if (ui_atlas_page_grow(system, page, grow_w > page->width ? grow_w : page->width, grow_h > page->height ? grow_h : page->height) == 0) {
						continue; /* retry pack on grown page */
					}
				}
			}

			/* Add a fresh page sized for the glyph (or default). */
			{
				u32 w = system->page_width0;
				u32 h = system->page_height0;
				if (w < need_w) {
					w = ui_font_round_up_pow2(need_w);
				}
				if (h < need_h) {
					h = ui_font_round_up_pow2(need_h);
				}
				if (w > UI_FONT_MAX_PAGE) {
					w = UI_FONT_MAX_PAGE;
				}
				if (h > UI_FONT_MAX_PAGE) {
					h = UI_FONT_MAX_PAGE;
				}
				if (need_w > w || need_h > h) {
					return -1;
				}
				if (ui_atlas_add_page(system, w, h) != 0) {
					return -1;
				}
			}
		}
	}
}

static i32 ui_font_set_pixel_size(sk_ui_font_t* font, u32 pixel_size) {
	if (pixel_size == 0u) {
		return -1;
	}
	if (FT_Set_Pixel_Sizes(font->face, 0, pixel_size) != 0) {
		return -1;
	}
	return 0;
}

static sk_ui_font_t* ui_font_load_bytes(sk_ui_font_system_t* system, const u8* data, u32 size) {
	const sk_allocator_t* a;
	sk_ui_font_t* font;
	u8* copy;
	FT_Face face = NULL;

	if (system == NULL || data == NULL || size == 0u) {
		return NULL;
	}
	a = system->allocator;
	copy = (u8*)a->alloc(a->instance, (size_t)size);
	if (copy == NULL) {
		return NULL;
	}
	memcpy(copy, data, (size_t)size);

	if (FT_New_Memory_Face(system->library, copy, (FT_Long)size, 0, &face) != 0) {
		a->free(a->instance, copy);
		return NULL;
	}

	font = (sk_ui_font_t*)a->alloc(a->instance, sizeof(sk_ui_font_t));
	if (font == NULL) {
		FT_Done_Face(face);
		a->free(a->instance, copy);
		return NULL;
	}
	memset(font, 0, sizeof(*font));
	font->system = system;
	font->id = system->next_font_id++;
	if (font->id == 0u) {
		font->id = system->next_font_id++; /* 0 reserved unused */
	}
	font->file_bytes = copy;
	font->file_size = size;
	font->face = face;

	if (sk_array_push(&system->fonts, font) != 0) {
		FT_Done_Face(face);
		a->free(a->instance, copy);
		a->free(a->instance, font);
		return NULL;
	}
	return font;
}

/* -------------------------------------------------------------------------- */
/* Public impl                                                                */
/* -------------------------------------------------------------------------- */

sk_ui_font_system_t* ui_font_system_create_impl(const sk_allocator_t* allocator, u32 page_width, u32 page_height) {
	const sk_allocator_t* a = allocator != NULL ? allocator : sk_allocator_default();
	sk_ui_font_system_t* system;
	FT_Library library = NULL;

	system = (sk_ui_font_system_t*)a->alloc(a->instance, sizeof(sk_ui_font_system_t));
	if (system == NULL) {
		return NULL;
	}
	memset(system, 0, sizeof(*system));
	system->allocator = a;
	system->next_font_id = 1u;
	system->page_width0 = page_width != 0u ? page_width : UI_FONT_DEFAULT_PAGE;
	system->page_height0 = page_height != 0u ? page_height : UI_FONT_DEFAULT_PAGE;
	sk_array_init(&system->fonts, a);
	sk_array_init(&system->pages, a);
	if (sk_hash_map_init(&system->cache, a, NULL, NULL) != 0) {
		a->free(a->instance, system);
		return NULL;
	}

	if (FT_Init_FreeType(&library) != 0) {
		sk_hash_map_free(&system->cache);
		sk_array_free(&system->pages);
		sk_array_free(&system->fonts);
		a->free(a->instance, system);
		return NULL;
	}
	system->library = library;

	if (ui_atlas_add_page(system, system->page_width0, system->page_height0) != 0) {
		FT_Done_FreeType(library);
		sk_hash_map_free(&system->cache);
		sk_array_free(&system->pages);
		sk_array_free(&system->fonts);
		a->free(a->instance, system);
		return NULL;
	}
	return system;
}

void ui_font_system_destroy_impl(sk_ui_font_system_t* system) {
	const sk_allocator_t* a;
	u32 i;
	if (system == NULL) {
		return;
	}
	a = system->allocator;

	for (i = 0u; i < system->fonts.count; ++i) {
		sk_ui_font_t* font = system->fonts.items[i];
		if (font == NULL) {
			continue;
		}
		if (font->face != NULL) {
			FT_Done_Face(font->face);
			font->face = NULL;
		}
		if (font->file_bytes != NULL) {
			a->free(a->instance, font->file_bytes);
			font->file_bytes = NULL;
		}
		a->free(a->instance, font);
	}
	sk_array_free(&system->fonts);

	for (i = 0u; i < system->pages.count; ++i) {
		ui_atlas_page_live_t* page = system->pages.items[i];
		if (page != NULL) {
			ui_atlas_page_release(a, page);
			a->free(a->instance, page);
		}
	}
	sk_array_free(&system->pages);

	sk_hash_map_free(&system->cache);

	if (system->library != NULL) {
		FT_Done_FreeType(system->library);
		system->library = NULL;
	}
	a->free(a->instance, system);
}

sk_ui_font_t* ui_font_load_path_impl(sk_ui_font_system_t* system, const sk_filesystem_api_t* fs, const_chr_t path) {
	sk_file_handle_t file;
	u64 size_u64;
	u32 size;
	u8* bytes;
	sk_ui_font_t* font;

	if (system == NULL || fs == NULL || path == NULL || path[0] == '\0') {
		return NULL;
	}

	file = fs->open_file(path, SK_FILE_ACCESS_READ);
	if (file == NULL) {
		return NULL;
	}
	size_u64 = fs->get_file_size(file);
	if (size_u64 == 0u || size_u64 > 0x7FFFFFFFull) {
		fs->close_file(file);
		return NULL;
	}
	size = (u32)size_u64;
	bytes = (u8*)system->allocator->alloc(system->allocator->instance, (size_t)size);
	if (bytes == NULL) {
		fs->close_file(file);
		return NULL;
	}
	if (fs->read_file(file, bytes, (size_t)size) != (u64)size) {
		system->allocator->free(system->allocator->instance, bytes);
		fs->close_file(file);
		return NULL;
	}
	fs->close_file(file);

	font = ui_font_load_bytes(system, bytes, size);
	system->allocator->free(system->allocator->instance, bytes);
	return font;
}

sk_ui_font_t* ui_font_load_memory_impl(sk_ui_font_system_t* system, const u8* data, u32 size) {
	return ui_font_load_bytes(system, data, size);
}

void ui_font_destroy_impl(sk_ui_font_t* font) {
	sk_ui_font_system_t* system;
	const sk_allocator_t* a;
	u32 i;
	if (font == NULL) {
		return;
	}
	system = font->system;
	a = system->allocator;

	/* Drop cache entries for this font (restart after each remove; rehash may move keys). */
	for (;;) {
		u32 slot;
		u8* controls = system->cache._hm.controls;
		ui_glyph_cache_key_t* keys = (ui_glyph_cache_key_t*)system->cache._hm.keys;
		const u32 cap = system->cache._hm.capacity;
		i32 found = 0;
		ui_glyph_cache_key_t to_remove = {0};
		for (slot = 0u; slot < cap; ++slot) {
			if (controls[slot] != 1u) {
				continue;
			}
			if (keys[slot].font_id == font->id) {
				to_remove = keys[slot];
				found = 1;
				break;
			}
		}
		if (found == 0) {
			break;
		}
		sk_hash_map_remove(&system->cache, to_remove);
	}

	for (i = 0u; i < system->fonts.count; ++i) {
		if (system->fonts.items[i] == font) {
			u32 j;
			for (j = i; j + 1u < system->fonts.count; ++j) {
				system->fonts.items[j] = system->fonts.items[j + 1u];
			}
			system->fonts.count -= 1u;
			break;
		}
	}

	if (font->face != NULL) {
		FT_Done_Face(font->face);
	}
	if (font->file_bytes != NULL) {
		a->free(a->instance, font->file_bytes);
	}
	a->free(a->instance, font);
}

i32 ui_font_get_metrics_impl(const sk_ui_font_t* font, u32 pixel_size, sk_ui_font_metrics_t* out) {
	FT_Face face;
	f32 scale;
	if (font == NULL || out == NULL || pixel_size == 0u) {
		return -1;
	}
	/* FT_Set_Pixel_Sizes needs non-const face; cast is safe for metric query. */
	if (ui_font_set_pixel_size(SK_CONST_CAST(sk_ui_font_t*, font), pixel_size) != 0) {
		return -1;
	}
	face = font->face;
	scale = (f32)pixel_size / (f32)face->units_per_EM;
	out->ascent = (f32)face->ascender * scale;
	out->descent = (f32)face->descender * scale;
	out->line_height = (f32)face->height * scale;
	out->pixel_size = (f32)pixel_size;
	return 0;
}

u32 ui_font_glyph_index_impl(const sk_ui_font_t* font, u32 codepoint) {
	if (font == NULL || font->face == NULL) {
		return 0u;
	}
	return FT_Get_Char_Index(font->face, (FT_ULong)codepoint);
}

i32 ui_font_get_glyph_impl(sk_ui_font_system_t* system, sk_ui_font_t* font, u32 pixel_size, u32 glyph_index, sk_ui_glyph_t* out) {
	ui_glyph_cache_key_t key;
	sk_ui_glyph_t glyph;
	FT_GlyphSlot slot;
	const u8* src;
	u32 bw;
	u32 bh;
	i32 pitch;

	if (system == NULL || font == NULL || out == NULL || pixel_size == 0u) {
		return -1;
	}
	if (font->system != system) {
		return -1;
	}

	key.font_id = font->id;
	key.pixel_size = pixel_size;
	key.glyph_index = glyph_index;

	if (sk_hash_map_get(&system->cache, key, &glyph) == 0) {
		system->cache_hits += 1u;
		*out = glyph;
		return 0;
	}
	system->cache_misses += 1u;

	if (ui_font_set_pixel_size(font, pixel_size) != 0) {
		return -1;
	}

	if (FT_Load_Glyph(font->face, glyph_index, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0) {
		return -1;
	}
	slot = font->face->glyph;

	memset(&glyph, 0, sizeof(glyph));
	glyph.glyph_index = glyph_index;
	glyph.advance_x = (f32)slot->advance.x / 64.0f;
	glyph.advance_y = (f32)slot->advance.y / 64.0f;
	glyph.bearing_x = (f32)slot->bitmap_left;
	glyph.bearing_y = (f32)slot->bitmap_top;

	bw = slot->bitmap.width;
	bh = slot->bitmap.rows;
	pitch = slot->bitmap.pitch;
	src = slot->bitmap.buffer;

	if (bw > 0u && bh > 0u && src != NULL && pitch != 0) {
		const u8* rows = src;
		u32 abs_pitch;
		/* FreeType pitch may be negative (bottom-up). */
		if (pitch < 0) {
			abs_pitch = (u32)(-pitch);
			rows = src + (size_t)(bh - 1u) * (size_t)abs_pitch;
			/* Copy to temp top-down buffer for pack. */
			{
				u8* tmp = (u8*)system->allocator->alloc(system->allocator->instance, (size_t)bw * (size_t)bh);
				u32 y;
				if (tmp == NULL) {
					return -1;
				}
				for (y = 0u; y < bh; ++y) {
					memcpy(tmp + (size_t)y * (size_t)bw, src + (size_t)(bh - 1u - y) * (size_t)abs_pitch, (size_t)bw);
				}
				if (ui_atlas_pack_bitmap(system, tmp, bw, bh, bw, &glyph) != 0) {
					system->allocator->free(system->allocator->instance, tmp);
					return -1;
				}
				system->allocator->free(system->allocator->instance, tmp);
			}
		} else {
			abs_pitch = (u32)pitch;
			if (ui_atlas_pack_bitmap(system, rows, bw, bh, abs_pitch, &glyph) != 0) {
				return -1;
			}
		}
	} else {
		glyph.width = 0u;
		glyph.height = 0u;
		glyph.u0 = glyph.v0 = glyph.u1 = glyph.v1 = 0.0f;
		glyph.page_index = 0u;
	}

	if (sk_hash_map_put(&system->cache, key, glyph) != 0) {
		return -1;
	}
	*out = glyph;
	return 0;
}

u32 ui_font_atlas_page_count_impl(const sk_ui_font_system_t* system) {
	return system != NULL ? system->pages.count : 0u;
}

i32 ui_font_atlas_get_page_impl(const sk_ui_font_system_t* system, u32 page_index, sk_ui_atlas_page_t* out) {
	const ui_atlas_page_live_t* page;
	if (system == NULL || out == NULL || page_index >= system->pages.count) {
		return -1;
	}
	page = system->pages.items[page_index];
	if (page == NULL) {
		return -1;
	}
	out->width = page->width;
	out->height = page->height;
	out->generation = page->generation;
	out->pixels = page->pixels;
	return 0;
}

u32 ui_font_cache_count_impl(const sk_ui_font_system_t* system) {
	return system != NULL ? system->cache._hm.count : 0u;
}

void ui_font_cache_stats_impl(const sk_ui_font_system_t* system, u32* out_hits, u32* out_misses) {
	if (out_hits != NULL) {
		*out_hits = system != NULL ? system->cache_hits : 0u;
	}
	if (out_misses != NULL) {
		*out_misses = system != NULL ? system->cache_misses : 0u;
	}
}

/* -------------------------------------------------------------------------- */
/* Tests                                                                      */
/* -------------------------------------------------------------------------- */

#ifdef SK_TESTS
#include "test.h"
#include "testdata/skore_test_font_ttf.h"

static const sk_ui_api_t* ui_font_test_api(void) {
	return ui_get_api_table();
}

SK_TEST(ui_font_pixel_size_1x_and_2x_scale) {
	/* 1x content scale: logical 16 → 16 px */
	TEST_ASSERT_EQUAL_UINT(16u, sk_ui_font_pixel_size(16.0f, 1.0f));
	/* 2x HiDPI: logical 16 → 32 px */
	TEST_ASSERT_EQUAL_UINT(32u, sk_ui_font_pixel_size(16.0f, 2.0f));
	/* Fractional scale rounds nearest */
	TEST_ASSERT_EQUAL_UINT(24u, sk_ui_font_pixel_size(16.0f, 1.5f));
	/* Zero / negative guard */
	TEST_ASSERT_EQUAL_UINT(0u, sk_ui_font_pixel_size(0.0f, 2.0f));
	TEST_ASSERT_EQUAL_UINT(1u, sk_ui_font_pixel_size(0.4f, 1.0f));
	/* Matches design: style.font_size * content_scale */
	{
		const f32 logical = 15.0f;
		const f32 scale_1x = 1.0f;
		const f32 scale_2x = 2.0f;
		TEST_ASSERT_EQUAL_UINT(15u, sk_ui_font_pixel_size(logical, scale_1x));
		TEST_ASSERT_EQUAL_UINT(30u, sk_ui_font_pixel_size(logical, scale_2x));
	}
}

SK_TEST(ui_font_load_and_metrics) {
	const sk_ui_api_t* ui = ui_font_test_api();
	sk_ui_font_system_t* sys = ui->font_system_create(NULL, 256u, 256u);
	sk_ui_font_t* font;
	sk_ui_font_metrics_t m1;
	sk_ui_font_metrics_t m2;
	u32 px1;
	u32 px2;

	TEST_ASSERT_NOT_NULL(sys);
	font = ui->font_load_memory(sys, skore_test_font_ttf, (u32)skore_test_font_ttf_size);
	TEST_ASSERT_NOT_NULL(font);

	px1 = sk_ui_font_pixel_size(16.0f, 1.0f);
	px2 = sk_ui_font_pixel_size(16.0f, 2.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->font_get_metrics(font, px1, &m1));
	TEST_ASSERT_EQUAL_INT(0, ui->font_get_metrics(font, px2, &m2));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, (f32)px1, m1.pixel_size);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, (f32)px2, m2.pixel_size);
	/* 2x pixel size roughly doubles line metrics */
	TEST_ASSERT_TRUE(m2.line_height > m1.line_height * 1.5f);
	TEST_ASSERT_TRUE(m1.ascent > 0.0f);
	TEST_ASSERT_TRUE(m1.descent < 0.0f);
	TEST_ASSERT_TRUE(m1.line_height > 0.0f);

	ui->font_system_destroy(sys);
}

SK_TEST(ui_font_glyph_cache_hit_miss) {
	const sk_ui_api_t* ui = ui_font_test_api();
	sk_ui_font_system_t* sys = ui->font_system_create(NULL, 256u, 256u);
	sk_ui_font_t* font;
	sk_ui_glyph_t g0;
	sk_ui_glyph_t g1;
	u32 hits = 0u;
	u32 misses = 0u;
	u32 gi;
	u32 px;

	TEST_ASSERT_NOT_NULL(sys);
	font = ui->font_load_memory(sys, skore_test_font_ttf, (u32)skore_test_font_ttf_size);
	TEST_ASSERT_NOT_NULL(font);

	px = sk_ui_font_pixel_size(16.0f, 1.0f);
	gi = ui->font_glyph_index(font, (u32)'A');
	TEST_ASSERT_TRUE(gi != 0u);

	ui->font_cache_stats(sys, &hits, &misses);
	TEST_ASSERT_EQUAL_UINT(0u, hits);
	TEST_ASSERT_EQUAL_UINT(0u, misses);
	TEST_ASSERT_EQUAL_UINT(0u, ui->font_cache_count(sys));

	/* First get: miss, cache grows */
	TEST_ASSERT_EQUAL_INT(0, ui->font_get_glyph(sys, font, px, gi, &g0));
	ui->font_cache_stats(sys, &hits, &misses);
	TEST_ASSERT_EQUAL_UINT(0u, hits);
	TEST_ASSERT_EQUAL_UINT(1u, misses);
	TEST_ASSERT_EQUAL_UINT(1u, ui->font_cache_count(sys));
	TEST_ASSERT_EQUAL_UINT(gi, g0.glyph_index);
	TEST_ASSERT_TRUE(g0.width > 0u);
	TEST_ASSERT_TRUE(g0.height > 0u);
	TEST_ASSERT_TRUE(g0.advance_x > 0.0f);

	/* Second get: hit, same metrics */
	TEST_ASSERT_EQUAL_INT(0, ui->font_get_glyph(sys, font, px, gi, &g1));
	ui->font_cache_stats(sys, &hits, &misses);
	TEST_ASSERT_EQUAL_UINT(1u, hits);
	TEST_ASSERT_EQUAL_UINT(1u, misses);
	TEST_ASSERT_EQUAL_UINT(1u, ui->font_cache_count(sys));
	TEST_ASSERT_EQUAL_UINT(g0.width, g1.width);
	TEST_ASSERT_EQUAL_UINT(g0.height, g1.height);
	TEST_ASSERT_EQUAL_UINT(g0.page_index, g1.page_index);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, g0.u0, g1.u0);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, g0.v0, g1.v0);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, g0.u1, g1.u1);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, g0.v1, g1.v1);

	/* Different pixel size: separate cache key → miss */
	{
		sk_ui_glyph_t g2;
		const u32 px2 = sk_ui_font_pixel_size(16.0f, 2.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->font_get_glyph(sys, font, px2, gi, &g2));
		ui->font_cache_stats(sys, &hits, &misses);
		TEST_ASSERT_EQUAL_UINT(1u, hits);
		TEST_ASSERT_EQUAL_UINT(2u, misses);
		TEST_ASSERT_EQUAL_UINT(2u, ui->font_cache_count(sys));
		TEST_ASSERT_TRUE(g2.width >= g0.width);
	}

	/* Space: empty bitmap, still cached */
	{
		sk_ui_glyph_t gs;
		const u32 gspace = ui->font_glyph_index(font, 32u);
		TEST_ASSERT_EQUAL_INT(0, ui->font_get_glyph(sys, font, px, gspace, &gs));
		TEST_ASSERT_EQUAL_UINT(0u, gs.width);
		TEST_ASSERT_EQUAL_UINT(0u, gs.height);
		TEST_ASSERT_TRUE(gs.advance_x > 0.0f);
	}

	ui->font_system_destroy(sys);
}

SK_TEST(ui_font_atlas_pack_and_page_overflow) {
	const sk_ui_api_t* ui = ui_font_test_api();
	/* Tiny page forces overflow → grow and/or additional pages. */
	sk_ui_font_system_t* sys = ui->font_system_create(NULL, 32u, 32u);
	sk_ui_font_t* font;
	u32 px;
	u32 pages_before;
	u32 pages_after;
	u32 packed = 0u;
	u32 max_page = 0u;
	const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	u32 i;
	sk_ui_atlas_page_t page0;

	TEST_ASSERT_NOT_NULL(sys);
	font = ui->font_load_memory(sys, skore_test_font_ttf, (u32)skore_test_font_ttf_size);
	TEST_ASSERT_NOT_NULL(font);

	pages_before = ui->font_atlas_page_count(sys);
	TEST_ASSERT_EQUAL_UINT(1u, pages_before);
	TEST_ASSERT_EQUAL_INT(0, ui->font_atlas_get_page(sys, 0u, &page0));
	TEST_ASSERT_EQUAL_UINT(32u, page0.width);
	TEST_ASSERT_EQUAL_UINT(32u, page0.height);
	TEST_ASSERT_NOT_NULL(page0.pixels);

	/* Large pixel size so each glyph is big relative to 32x32. */
	px = sk_ui_font_pixel_size(24.0f, 2.0f); /* 48 px */
	TEST_ASSERT_EQUAL_UINT(48u, px);

	for (i = 0u; chars[i] != '\0'; ++i) {
		sk_ui_glyph_t g;
		const u32 gi = ui->font_glyph_index(font, (u32)(u8)chars[i]);
		TEST_ASSERT_EQUAL_INT(0, ui->font_get_glyph(sys, font, px, gi, &g));
		if (g.width > 0u) {
			packed += 1u;
			if (g.page_index > max_page) {
				max_page = g.page_index;
			}
			TEST_ASSERT_TRUE(g.u1 > g.u0);
			TEST_ASSERT_TRUE(g.v1 > g.v0);
		}
	}

	pages_after = ui->font_atlas_page_count(sys);
	TEST_ASSERT_TRUE(packed > 4u);
	/* Either grown past 32 or more than one page (or both). */
	{
		sk_ui_atlas_page_t p;
		i32 grew = 0;
		TEST_ASSERT_EQUAL_INT(0, ui->font_atlas_get_page(sys, 0u, &p));
		if (p.width > 32u || p.height > 32u) {
			grew = 1;
		}
		TEST_ASSERT_TRUE(grew != 0 || pages_after > 1u || max_page > 0u);
	}
	TEST_ASSERT_TRUE(ui->font_cache_count(sys) >= packed);

	/* Non-empty atlas has some coverage ink. */
	{
		sk_ui_atlas_page_t p;
		u32 x;
		u32 y;
		u32 ink = 0u;
		u32 pi;
		for (pi = 0u; pi < pages_after; ++pi) {
			TEST_ASSERT_EQUAL_INT(0, ui->font_atlas_get_page(sys, pi, &p));
			for (y = 0u; y < p.height; ++y) {
				for (x = 0u; x < p.width; ++x) {
					if (p.pixels[y * p.width + x] != 0u) {
						ink += 1u;
					}
				}
			}
		}
		TEST_ASSERT_TRUE(ink > 0u);
	}

	ui->font_system_destroy(sys);
}

/* Minimal filesystem fake: open/read/size/close only, for font_load_path. */
typedef struct {
	const u8* bytes;
	u32 size;
	u32 pos;
	i32 open;
} ui_font_fake_file_t;

static ui_font_fake_file_t ui_font_fake_file;

static sk_file_handle_t ui_font_fake_open(const_chr_t path, sk_file_access_t access_mode) {
	(void)access_mode;
	if (path == NULL || path[0] == '\0') {
		return NULL;
	}
	ui_font_fake_file.bytes = skore_test_font_ttf;
	ui_font_fake_file.size = (u32)skore_test_font_ttf_size;
	ui_font_fake_file.pos = 0u;
	ui_font_fake_file.open = 1;
	return (sk_file_handle_t)&ui_font_fake_file;
}

static u64 ui_font_fake_size(sk_file_handle_t file) {
	ui_font_fake_file_t* f = (ui_font_fake_file_t*)file;
	return f != NULL ? (u64)f->size : 0ull;
}

static u64 ui_font_fake_read(sk_file_handle_t file, void_ptr_t data, size_t size) {
	ui_font_fake_file_t* f = (ui_font_fake_file_t*)file;
	u32 remain;
	u32 n;
	if (f == NULL || data == NULL || f->open == 0) {
		return 0ull;
	}
	remain = f->size - f->pos;
	n = remain < (u32)size ? remain : (u32)size;
	if (n > 0u) {
		memcpy(data, f->bytes + f->pos, (size_t)n);
		f->pos += n;
	}
	return (u64)n;
}

static void ui_font_fake_close(sk_file_handle_t file) {
	ui_font_fake_file_t* f = (ui_font_fake_file_t*)file;
	if (f != NULL) {
		f->open = 0;
	}
}

SK_TEST(ui_font_load_path_via_filesystem) {
	const sk_ui_api_t* ui = ui_font_test_api();
	sk_ui_font_system_t* sys;
	sk_ui_font_t* font;
	sk_filesystem_api_t fs;

	memset(&fs, 0, sizeof(fs));
	fs.open_file = ui_font_fake_open;
	fs.get_file_size = ui_font_fake_size;
	fs.read_file = ui_font_fake_read;
	fs.close_file = ui_font_fake_close;

	sys = ui->font_system_create(NULL, 128u, 128u);
	TEST_ASSERT_NOT_NULL(sys);

	font = ui->font_load_path(sys, &fs, "fixture://skore_test_font.ttf");
	TEST_ASSERT_NOT_NULL(font);
	{
		sk_ui_glyph_t g;
		const u32 px = sk_ui_font_pixel_size(16.0f, 1.0f);
		const u32 gi = ui->font_glyph_index(font, (u32)'B');
		TEST_ASSERT_EQUAL_INT(0, ui->font_get_glyph(sys, font, px, gi, &g));
		TEST_ASSERT_TRUE(g.advance_x > 0.0f);
	}

	ui->font_system_destroy(sys);
}

#endif /* SK_TESTS */
