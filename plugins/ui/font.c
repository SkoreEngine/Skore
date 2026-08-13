/**
 * @file font.c
 * @brief FreeType face management for the UI font system (APX-271).
 *
 * FreeType is retained only for the responsibilities the APX-264 audit
 * identified as still required: face loading (FT_New_Memory_Face), glyph
 * index / cmap queries (FT_Get_Char_Index), and face metrics (ascender /
 * descender / height scaled to pixel size). Glyph rasterization and the R8
 * stb_rect_pack bitmap atlas are retired; all glyph geometry comes from the
 * scale-independent MSDF atlas baked by font_msdf.c (APX-265) and layout /
 * paint scale its em-space metrics to any pixel size.
 *
 * Memory: each font owns a private copy of the TTF/OTF bytes; FreeType sees
 * that buffer for the face lifetime.
 */

#include "ui.h"
#include "ui_internal.h"

#include "allocator.h"
#include "array.h"

#include <string.h>

/* Unity (via test.h) may define noreturn as _Noreturn; FreeType/UCRT break. */
#ifdef noreturn
#undef noreturn
#endif
#include <ft2build.h>
#include FT_FREETYPE_H

/* -------------------------------------------------------------------------- */
/* Internal types                                                             */
/* -------------------------------------------------------------------------- */

struct sk_ui_font_t {
	sk_ui_font_system_t* system;
	u32 id;
	u8* file_bytes;
	u32 file_size;
	FT_Face face;
	ui_msdf_atlas_live_t* msdf; /* optional MSDF atlas; paint/layout use it when baked */
};

typedef SK_ARRAY(sk_ui_font_t*) ui_font_ptr_array_t;

struct sk_ui_font_system_t {
	const sk_allocator_t* allocator;
	FT_Library library;
	ui_font_ptr_array_t fonts;
	u32 next_font_id;
};

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

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

sk_ui_font_system_t* ui_font_system_create_impl(const sk_allocator_t* allocator) {
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
	sk_array_init(&system->fonts, a);

	if (FT_Init_FreeType(&library) != 0) {
		sk_array_free(&system->fonts);
		a->free(a->instance, system);
		return NULL;
	}
	system->library = library;
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
		if (font->msdf != NULL) {
			ui_msdf_atlas_release(a, font->msdf);
			font->msdf = NULL;
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

	if (font->msdf != NULL) {
		ui_msdf_atlas_release(a, font->msdf);
		font->msdf = NULL;
	}
	if (font->face != NULL) {
		FT_Done_Face(font->face);
	}
	if (font->file_bytes != NULL) {
		a->free(a->instance, font->file_bytes);
	}
	a->free(a->instance, font);
}

const sk_allocator_t* ui_font_allocator(const sk_ui_font_t* font) {
	if (font == NULL || font->system == NULL) {
		return sk_allocator_default();
	}
	return font->system->allocator;
}

const u8* ui_font_file_bytes(const sk_ui_font_t* font) {
	return font != NULL ? font->file_bytes : NULL;
}

u32 ui_font_file_size(const sk_ui_font_t* font) {
	return font != NULL ? font->file_size : 0u;
}

u32 ui_font_id(const sk_ui_font_t* font) {
	return font != NULL ? font->id : 0u;
}

sk_ui_font_t* ui_font_system_find(sk_ui_font_system_t* system, u32 font_id) {
	u32 i;
	if (system == NULL || font_id == 0u) {
		return NULL;
	}
	for (i = 0u; i < system->fonts.count; ++i) {
		sk_ui_font_t* font = system->fonts.items[i];
		if (font != NULL && font->id == font_id) {
			return font;
		}
	}
	return NULL;
}

u32 ui_font_system_font_count(const sk_ui_font_system_t* system) {
	return system != NULL ? system->fonts.count : 0u;
}

sk_ui_font_t* ui_font_system_font_at(sk_ui_font_system_t* system, u32 index) {
	if (system == NULL || index >= system->fonts.count) {
		return NULL;
	}
	return system->fonts.items[index];
}

ui_msdf_atlas_live_t* ui_font_msdf_ptr(const sk_ui_font_t* font) {
	return font != NULL ? font->msdf : NULL;
}

void ui_font_msdf_set(sk_ui_font_t* font, ui_msdf_atlas_live_t* atlas) {
	if (font != NULL) {
		font->msdf = atlas;
	}
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
	sk_ui_font_system_t* sys = ui->font_system_create(NULL);
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

SK_TEST(ui_font_glyph_index_and_ids) {
	const sk_ui_api_t* ui = ui_font_test_api();
	sk_ui_font_system_t* sys = ui->font_system_create(NULL);
	sk_ui_font_t* font;
	u32 gi_a;
	u32 gi_space;
	u32 gi_missing;
	u32 id;

	TEST_ASSERT_NOT_NULL(sys);
	TEST_ASSERT_EQUAL_UINT(0u, ui_font_system_font_count(sys));
	font = ui->font_load_memory(sys, skore_test_font_ttf, (u32)skore_test_font_ttf_size);
	TEST_ASSERT_NOT_NULL(font);
	TEST_ASSERT_EQUAL_UINT(1u, ui_font_system_font_count(sys));

	gi_a = ui->font_glyph_index(font, (u32)'A');
	gi_space = ui->font_glyph_index(font, 32u);
	gi_missing = ui->font_glyph_index(font, 0x2603u); /* SNOWMAN outside subset */
	TEST_ASSERT_TRUE(gi_a != 0u);
	TEST_ASSERT_TRUE(gi_space != 0u);
	/* Missing glyphs map to 0 (cmap default / .notdef sentinel). */
	TEST_ASSERT_EQUAL_UINT(0u, gi_missing);

	id = ui_font_id(font);
	TEST_ASSERT_TRUE(id != 0u);
	TEST_ASSERT_TRUE(ui_font_system_find(sys, id) == font);

	ui->font_destroy(font);
	TEST_ASSERT_EQUAL_UINT(0u, ui_font_system_font_count(sys));
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

	sys = ui->font_system_create(NULL);
	TEST_ASSERT_NOT_NULL(sys);

	font = ui->font_load_path(sys, &fs, "fixture://skore_test_font.ttf");
	TEST_ASSERT_NOT_NULL(font);
	TEST_ASSERT_TRUE(ui->font_glyph_index(font, (u32)'B') != 0u);

	ui->font_system_destroy(sys);
}

#endif /* SK_TESTS */
