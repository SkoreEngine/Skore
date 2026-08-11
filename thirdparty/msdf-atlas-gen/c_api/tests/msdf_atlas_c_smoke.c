/*
 * msdf_atlas_c_smoke.c — smoke test for the msdf-atlas-gen C API (APX-220).
 *
 * Pure C99. Walks the full pipeline against the vendored test font:
 *   version/errors -> config -> charset (ASCII, parse, ranges) -> font (file
 *   and memory) -> glyphset (load, edge color, layout, advance) -> packer
 *   (tight and grid) -> generator (bitmap + layout snapshot, both Y
 *   directions) -> clean destruction of every handle.
 *
 * Also doubles as the sanitizer verification target: when built with ASan
 * (-fsanitize=address) any leaked or double-freed handle fails the run.
 *
 * The font path is injected at build time via MSDF_ATLAS_C_SMOKE_FONT_PATH.
 */

#include <msdf_atlas_c.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MSDF_ATLAS_C_SMOKE_FONT_PATH
#define MSDF_ATLAS_C_SMOKE_FONT_PATH "skore_test_font.ttf"
#endif

static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_ERR(expr, expected)                                            \
    do {                                                                     \
        msdf_atlas_error_t err_ = (expr);                                    \
        if (err_ != (expected)) {                                            \
            fprintf(stderr, "FAIL %s:%d: %s -> %d (%s), expected %d (%s)\n", \
                    __FILE__, __LINE__, #expr, (int) err_,                   \
                    msdf_atlas_error_string(err_), (int) (expected),         \
                    msdf_atlas_error_string(expected));                      \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

static void expect_message_after_failure(void) {
    if (!msdf_atlas_last_error_message()) {
        fprintf(stderr, "FAIL: last_error_message is NULL after a failing call\n");
        ++g_failures;
    }
}

static void expect_no_message_after_success(void) {
    if (msdf_atlas_last_error_message()) {
        fprintf(stderr, "FAIL: last_error_message is set after a successful call\n");
        ++g_failures;
    }
}

/* Reads the whole font file into a heap buffer (caller frees). */
static unsigned char *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long length = ftell(f);
    if (length <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    unsigned char *data = (unsigned char *) malloc((size_t) length);
    if (!data) {
        fclose(f);
        return NULL;
    }
    if (fread(data, 1, (size_t) length, f) != (size_t) length) {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = (size_t) length;
    return data;
}

static size_t find_layout(const msdf_atlas_glyph_layout_t *layouts, size_t count, uint32_t codepoint) {
    size_t i;
    for (i = 0; i < count; ++i)
        if (layouts[i].codepoint == codepoint)
            return i;
    return SIZE_MAX;
}

static void test_errors_and_config(void) {
    const char *version = msdf_atlas_version();
    CHECK(version != NULL && version[0] != '\0');
    expect_no_message_after_success();
    CHECK(msdf_atlas_error_string(MSDF_ATLAS_OK) != NULL);
    CHECK(msdf_atlas_error_string((msdf_atlas_error_t) 9999) != NULL);

    msdf_atlas_config_t config;
    CHECK_ERR(msdf_atlas_config_default(&config), MSDF_ATLAS_OK);
    expect_no_message_after_success();
    CHECK(config.struct_size == sizeof(config));
    CHECK(config.image_type == MSDF_ATLAS_IMAGE_MSDF);
    CHECK(config.pixel_format == MSDF_ATLAS_PIXEL_UNKNOWN);
    CHECK(config.px_range.lower == 2.0 && config.px_range.upper == 2.0);
    CHECK(config.miter_limit == 1.0);
    CHECK_ERR(msdf_atlas_config_validate(&config), MSDF_ATLAS_OK);

    /* struct_size mismatch. */
    msdf_atlas_config_t bad = config;
    bad.struct_size = 0;
    CHECK_ERR(msdf_atlas_config_validate(&bad), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
    expect_message_after_failure();
    /* Channel count mismatch between image type and explicit pixel format. */
    bad = config;
    bad.pixel_format = MSDF_ATLAS_PIXEL_RGB8; /* 3 channels, MSDF needs 4? no: MSDF is 3. use MTSDF. */
    bad.image_type = MSDF_ATLAS_IMAGE_MTSDF;
    CHECK_ERR(msdf_atlas_config_validate(&bad), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
    /* Malformed pixel range. */
    bad = config;
    bad.px_range.lower = 3.0;
    CHECK_ERR(msdf_atlas_config_validate(&bad), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
    /* Negative spacing. */
    bad = config;
    bad.spacing = -1;
    CHECK_ERR(msdf_atlas_config_validate(&bad), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);

    CHECK_ERR(msdf_atlas_config_default(NULL), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
    expect_message_after_failure();
    CHECK_ERR(msdf_atlas_config_validate(NULL), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
}

static void test_charset(void) {
    msdf_atlas_charset_t *charset = NULL;
    msdf_atlas_charset_t *ascii = NULL;
    size_t size = 0;
    bool contains = false;

    CHECK_ERR(msdf_atlas_charset_create(&charset), MSDF_ATLAS_OK);
    CHECK(charset != NULL);
    CHECK_ERR(msdf_atlas_charset_create_ascii(&ascii), MSDF_ATLAS_OK);
    CHECK(ascii != NULL);
    CHECK_ERR(msdf_atlas_charset_size(ascii, &size), MSDF_ATLAS_OK);
    CHECK(size == 95);
    CHECK_ERR(msdf_atlas_charset_contains(ascii, 'A', &contains), MSDF_ATLAS_OK);
    CHECK(contains);
    CHECK_ERR(msdf_atlas_charset_contains(ascii, 0xE9, &contains), MSDF_ATLAS_OK);
    CHECK(!contains);

    CHECK_ERR(msdf_atlas_charset_add(charset, 0xE9), MSDF_ATLAS_OK); /* é */
    CHECK_ERR(msdf_atlas_charset_contains(charset, 0xE9, &contains), MSDF_ATLAS_OK);
    CHECK(contains);
    CHECK_ERR(msdf_atlas_charset_remove(charset, 0xE9), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_charset_contains(charset, 0xE9, &contains), MSDF_ATLAS_OK);
    CHECK(!contains);

    /* Header-documented syntax: bare chars with dash ranges. */
    CHECK_ERR(msdf_atlas_charset_parse(charset, "a-z0-9", SIZE_MAX), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_charset_contains(charset, 'a', &contains), MSDF_ATLAS_OK);
    CHECK(contains);
    CHECK_ERR(msdf_atlas_charset_contains(charset, 'z', &contains), MSDF_ATLAS_OK);
    CHECK(contains);
    CHECK_ERR(msdf_atlas_charset_contains(charset, '0', &contains), MSDF_ATLAS_OK);
    CHECK(contains);

    /* Header-documented syntax: U+XXXX with dash range. */
    CHECK_ERR(msdf_atlas_charset_parse(charset, "U+0041-U+005A", SIZE_MAX), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_charset_contains(charset, 0x41, &contains), MSDF_ATLAS_OK);
    CHECK(contains);
    CHECK_ERR(msdf_atlas_charset_contains(charset, 0x5A, &contains), MSDF_ATLAS_OK);
    CHECK(contains);

    /* Upstream syntax: quoted char, quoted string, bracketed range, hex. */
    CHECK_ERR(msdf_atlas_charset_parse(charset, "'A'", SIZE_MAX), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_charset_parse(charset, "\"xyz\"", SIZE_MAX), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_charset_parse(charset, "['A', 'Z']", SIZE_MAX), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_charset_parse(charset, "[0x61, 0x63]", SIZE_MAX), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_charset_parse(charset, "U+03B1", SIZE_MAX), MSDF_ATLAS_OK); /* α */

    /* Parse errors are atomic: charset must be unchanged. */
    CHECK_ERR(msdf_atlas_charset_parse(charset, "a[", SIZE_MAX), MSDF_ATLAS_ERROR_CHARSET_PARSE);
    expect_message_after_failure();
    CHECK_ERR(msdf_atlas_charset_contains(charset, 'a', &contains), MSDF_ATLAS_OK);
    CHECK(contains); /* 'a' came from the earlier successful parse */
    CHECK_ERR(msdf_atlas_charset_parse(charset, "@include \"x\"", SIZE_MAX), MSDF_ATLAS_ERROR_CHARSET_PARSE);
    CHECK_ERR(msdf_atlas_charset_parse(charset, "['A' 'Z']", SIZE_MAX), MSDF_ATLAS_ERROR_CHARSET_PARSE);
    CHECK_ERR(msdf_atlas_charset_parse(charset, "'ab'", SIZE_MAX), MSDF_ATLAS_ERROR_CHARSET_PARSE);

    /* text_length-respecting overload: only the first 3 bytes ("a-z"). */
    CHECK_ERR(msdf_atlas_charset_parse(charset, "a-z]]]]]]", 3), MSDF_ATLAS_OK);

    CHECK_ERR(msdf_atlas_charset_size(charset, &size), MSDF_ATLAS_OK);
    CHECK(size > 40);

    CHECK_ERR(msdf_atlas_charset_create(NULL), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
    expect_message_after_failure();
    CHECK_ERR(msdf_atlas_charset_add(NULL, 'A'), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
    CHECK_ERR(msdf_atlas_charset_parse(NULL, "a", SIZE_MAX), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
    CHECK_ERR(msdf_atlas_charset_parse(charset, NULL, SIZE_MAX), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
    CHECK_ERR(msdf_atlas_charset_destroy(NULL), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_charset_destroy(charset), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_charset_destroy(ascii), MSDF_ATLAS_OK);
}

static void test_font(void) {
    msdf_atlas_font_t *font = NULL;
    msdf_atlas_font_metrics_t metrics;
    uint32_t count = 0;
    uint32_t index = 0;

    CHECK_ERR(msdf_atlas_font_open(MSDF_ATLAS_C_SMOKE_FONT_PATH, &font), MSDF_ATLAS_OK);
    expect_no_message_after_success();
    CHECK(font != NULL);
    CHECK_ERR(msdf_atlas_font_get_metrics(font, &metrics), MSDF_ATLAS_OK);
    CHECK(metrics.em_size > 0.0);
    CHECK(metrics.line_height > 0.0);
    CHECK(metrics.ascender_y > metrics.descender_y);
    CHECK_ERR(msdf_atlas_font_get_glyph_count(font, &count), MSDF_ATLAS_OK);
    CHECK(count > 10);
    CHECK_ERR(msdf_atlas_font_get_glyph_index(font, 'A', &index), MSDF_ATLAS_OK);
    CHECK(index < count);

    /* Missing file: IO or FONT_LOAD, never OK, always with a detail message. */
    msdf_atlas_font_t *missing = NULL;
    msdf_atlas_error_t err = msdf_atlas_font_open("/nonexistent/definitely-missing.ttf", &missing);
    CHECK(err == MSDF_ATLAS_ERROR_IO || err == MSDF_ATLAS_ERROR_FONT_LOAD);
    CHECK(missing == NULL);
    expect_message_after_failure();

    CHECK_ERR(msdf_atlas_font_open(NULL, &font), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
    CHECK_ERR(msdf_atlas_font_open(MSDF_ATLAS_C_SMOKE_FONT_PATH, NULL), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
    CHECK_ERR(msdf_atlas_font_get_metrics(NULL, &metrics), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);

    /* In-memory font: the buffer must outlive the handle. */
    size_t data_size = 0;
    unsigned char *data = read_file(MSDF_ATLAS_C_SMOKE_FONT_PATH, &data_size);
    CHECK(data != NULL && data_size > 0);
    if (data) {
        msdf_atlas_font_t *mem_font = NULL;
        CHECK_ERR(msdf_atlas_font_open_memory(data, data_size, &mem_font), MSDF_ATLAS_OK);
        CHECK(mem_font != NULL);
        CHECK_ERR(msdf_atlas_font_get_glyph_count(mem_font, &count), MSDF_ATLAS_OK);
        CHECK(count > 10);
        CHECK_ERR(msdf_atlas_font_open_memory(data, 0, &mem_font), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
        CHECK(mem_font == NULL);
        CHECK_ERR(msdf_atlas_font_open_memory(NULL, data_size, &mem_font), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
        CHECK_ERR(msdf_atlas_font_open_memory(data, data_size, NULL), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
        CHECK_ERR(msdf_atlas_font_destroy(mem_font), MSDF_ATLAS_OK);
        free(data);
    }

    CHECK_ERR(msdf_atlas_font_destroy(NULL), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_font_destroy(font), MSDF_ATLAS_OK);
}

static msdf_atlas_glyphset_t *make_loaded_glyphset(msdf_atlas_font_t *font, msdf_atlas_charset_t *charset,
                                                   size_t *out_count) {
    msdf_atlas_glyphset_t *set = NULL;
    int32_t loaded = 0;
    CHECK_ERR(msdf_atlas_glyphset_create(&set), MSDF_ATLAS_OK);
    CHECK(set != NULL);
    CHECK_ERR(msdf_atlas_glyphset_load_charset(set, font, 1.0, charset, MSDF_ATLAS_LOAD_KERNING, &loaded),
              MSDF_ATLAS_OK);
    expect_no_message_after_success();
    CHECK(loaded > 20);
    CHECK_ERR(msdf_atlas_glyphset_get_count(set, out_count), MSDF_ATLAS_OK);
    CHECK(*out_count == (size_t) loaded);
    return set;
}

static void test_glyphset_and_pack(void) {
    msdf_atlas_font_t *font = NULL;
    msdf_atlas_charset_t *charset = NULL;
    msdf_atlas_glyphset_t *set = NULL;
    msdf_atlas_glyphset_t *range_set = NULL;
    msdf_atlas_glyphset_t *empty_set = NULL;
    msdf_atlas_glyph_layout_t layout;
    msdf_atlas_glyph_layout_t glyph_a;
    size_t count = 0;
    size_t space_index = SIZE_MAX;
    size_t i;
    double advance = 0.0;
    int32_t loaded = 0;

    CHECK_ERR(msdf_atlas_font_open(MSDF_ATLAS_C_SMOKE_FONT_PATH, &font), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_charset_create_ascii(&charset), MSDF_ATLAS_OK);

    set = make_loaded_glyphset(font, charset, &count);

    /* Layouts are available right after load (plane bounds + advance). */
    for (i = 0; i < count; ++i) {
        CHECK_ERR(msdf_atlas_glyphset_get_layout(set, i, &layout), MSDF_ATLAS_OK);
        if (layout.codepoint == 0x20) {
            space_index = i;
            CHECK(layout.is_whitespace);
            CHECK(layout.advance > 0.0);
        } else {
            CHECK(layout.plane_bounds_r > layout.plane_bounds_l);
            CHECK(layout.plane_bounds_t > layout.plane_bounds_b);
        }
        CHECK(layout.atlas_w == 0); /* placement not computed before pack */
    }
    CHECK(space_index != SIZE_MAX);
    CHECK_ERR(msdf_atlas_glyphset_get_layout(set, count, &layout), MSDF_ATLAS_ERROR_OUT_OF_RANGE);
    expect_message_after_failure();
    CHECK_ERR(msdf_atlas_glyphset_get_layout(NULL, 0, &layout), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);

    /* Edge coloring (MSDF quality pass). */
    CHECK_ERR(msdf_atlas_glyphset_edge_color(set, MSDF_ATLAS_EDGE_COLORING_INKTRAP, 3.0, 0), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_glyphset_edge_color(set, MSDF_ATLAS_EDGE_COLORING_NONE, 3.0, 0), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_glyphset_edge_color(set, MSDF_ATLAS_EDGE_COLORING_SIMPLE, 3.0, 0), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_glyphset_edge_color(set, MSDF_ATLAS_EDGE_COLORING_BY_DISTANCE, 3.0, 0), MSDF_ATLAS_OK);
    /* The pinned msdfgen has no diagonal heuristic. */
    CHECK_ERR(msdf_atlas_glyphset_edge_color(set, MSDF_ATLAS_EDGE_COLORING_DIAGONAL, 3.0, 0),
              MSDF_ATLAS_ERROR_UNSUPPORTED);
    expect_message_after_failure();

    CHECK_ERR(msdf_atlas_glyphset_create(&empty_set), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_glyphset_edge_color(empty_set, MSDF_ATLAS_EDGE_COLORING_INKTRAP, 3.0, 0),
              MSDF_ATLAS_ERROR_INVALID_STATE);
    expect_message_after_failure();

    /* Kerning was loaded with MSDF_ATLAS_LOAD_KERNING. */
    CHECK_ERR(msdf_atlas_glyphset_get_advance(set, 'A', 'B', MSDF_ATLAS_IDENTIFIER_UNICODE_CODEPOINT, &advance),
              MSDF_ATLAS_OK);
    CHECK(advance > 0.0);
    CHECK_ERR(msdf_atlas_glyphset_get_advance(set, 0x10FFFF, 0x10FFFF, MSDF_ATLAS_IDENTIFIER_UNICODE_CODEPOINT, &advance),
              MSDF_ATLAS_ERROR_OUT_OF_RANGE);
    expect_message_after_failure();

    /* A glyphset loaded without kerning rejects advance queries. */
    {
        msdf_atlas_glyphset_t *no_kern = NULL;
        CHECK_ERR(msdf_atlas_glyphset_create(&no_kern), MSDF_ATLAS_OK);
        CHECK_ERR(msdf_atlas_glyphset_load_charset(no_kern, font, 1.0, charset, 0, &loaded), MSDF_ATLAS_OK);
        CHECK_ERR(msdf_atlas_glyphset_get_advance(no_kern, 'A', 'B', MSDF_ATLAS_IDENTIFIER_UNICODE_CODEPOINT, &advance),
                  MSDF_ATLAS_ERROR_INVALID_STATE);
        expect_message_after_failure();
        CHECK_ERR(msdf_atlas_glyphset_destroy(no_kern), MSDF_ATLAS_OK);
    }

    /* Glyph-index range load on a fresh glyphset. */
    CHECK_ERR(msdf_atlas_glyphset_create(&range_set), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_glyphset_load_glyph_range(range_set, font, 1.0, 0, 32, 0, &loaded), MSDF_ATLAS_OK);
    CHECK(loaded > 0);
    CHECK_ERR(msdf_atlas_glyphset_get_count(range_set, &count), MSDF_ATLAS_OK);
    CHECK(count == (size_t) loaded);
    CHECK_ERR(msdf_atlas_glyphset_load_glyph_range(range_set, font, 1.0, 0, 0, 0, &loaded),
              MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
    CHECK_ERR(msdf_atlas_glyphset_load_charset(range_set, font, 0.0, charset, 0, &loaded),
              MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
    CHECK_ERR(msdf_atlas_glyphset_load_charset(range_set, font, 1.0, NULL, 0, &loaded),
              MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
    CHECK_ERR(msdf_atlas_glyphset_destroy(range_set), MSDF_ATLAS_OK);

    /* Re-loading replaces the previously loaded glyphs. */
    CHECK_ERR(msdf_atlas_glyphset_load_glyph_range(set, font, 1.0, 0, 8, 0, &loaded), MSDF_ATLAS_OK);
    CHECK(loaded > 0);
    CHECK_ERR(msdf_atlas_glyphset_get_count(set, &count), MSDF_ATLAS_OK);
    CHECK(count == (size_t) loaded);
    /* Restore the full charset for the pack step. */
    CHECK_ERR(msdf_atlas_glyphset_load_charset(set, font, 1.0, charset, MSDF_ATLAS_LOAD_KERNING, &loaded),
              MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_glyphset_get_count(set, &count), MSDF_ATLAS_OK);
    CHECK(count == (size_t) loaded);
    /* space is U+0020, always first in the ASCII charset order. */
    space_index = 0;
    CHECK_ERR(msdf_atlas_glyphset_edge_color(set, MSDF_ATLAS_EDGE_COLORING_INKTRAP, 3.0, 0), MSDF_ATLAS_OK);

    /* ---- Tight packing ---- */
    {
        msdf_atlas_packer_t *packer = NULL;
        int32_t width = 0, height = 0;
        double scale = 0.0;
        msdf_atlas_range_t range;

        CHECK_ERR(msdf_atlas_packer_create(NULL, &packer), MSDF_ATLAS_OK);
        CHECK(packer != NULL);

        CHECK_ERR(msdf_atlas_packer_get_dimensions(packer, &width, &height), MSDF_ATLAS_OK);
        CHECK(width == 0 && height == 0); /* nothing packed yet */

        /* Grid-only calls on a tight packer. */
        CHECK_ERR(msdf_atlas_packer_set_columns(packer, 4), MSDF_ATLAS_ERROR_INVALID_STATE);
        expect_message_after_failure();
        CHECK_ERR(msdf_atlas_packer_get_columns(packer, &width), MSDF_ATLAS_ERROR_INVALID_STATE);

        CHECK_ERR(msdf_atlas_packer_pack(packer, set), MSDF_ATLAS_OK);
        expect_no_message_after_success();
        CHECK_ERR(msdf_atlas_packer_get_dimensions(packer, &width, &height), MSDF_ATLAS_OK);
        CHECK(width > 0 && height > 0);
        CHECK_ERR(msdf_atlas_packer_get_scale(packer, &scale), MSDF_ATLAS_OK);
        CHECK(scale > 0.0);
        CHECK_ERR(msdf_atlas_packer_get_pixel_range(packer, &range), MSDF_ATLAS_OK);
        CHECK(range.lower == 2.0 && range.upper == 2.0);

        /* Glyph layouts now carry atlas placement. Index 0 is space (ASCII
         * charset starts at U+0020); pick a non-whitespace glyph instead. */
        CHECK_ERR(msdf_atlas_glyphset_get_layout(set, space_index, &layout), MSDF_ATLAS_OK);
        CHECK(layout.atlas_w == 0 && layout.atlas_h == 0); /* whitespace */
        {
            size_t a_idx = SIZE_MAX;
            for (i = 0; i < count; ++i) {
                CHECK_ERR(msdf_atlas_glyphset_get_layout(set, i, &glyph_a), MSDF_ATLAS_OK);
                if (glyph_a.codepoint == 'A') {
                    a_idx = i;
                    break;
                }
            }
            CHECK(a_idx != SIZE_MAX);
            CHECK_ERR(msdf_atlas_glyphset_get_layout(set, a_idx, &glyph_a), MSDF_ATLAS_OK);
        }
        CHECK(glyph_a.atlas_w > 0 && glyph_a.atlas_h > 0);
        CHECK(glyph_a.atlas_x >= 0 && glyph_a.atlas_y >= 0);
        CHECK(glyph_a.atlas_bounds_r > glyph_a.atlas_bounds_l);

        /* Packing an empty glyphset is an invalid state. */
        CHECK_ERR(msdf_atlas_packer_pack(packer, empty_set), MSDF_ATLAS_ERROR_INVALID_STATE);
        expect_message_after_failure();

        /* Re-pack with fixed dimensions via setters. */
        CHECK_ERR(msdf_atlas_packer_set_dimensions(packer, 256, 128), MSDF_ATLAS_OK);
        CHECK_ERR(msdf_atlas_packer_pack(packer, set), MSDF_ATLAS_OK);
        CHECK_ERR(msdf_atlas_packer_get_dimensions(packer, &width, &height), MSDF_ATLAS_OK);
        CHECK(width == 256 && height == 128);
        CHECK_ERR(msdf_atlas_packer_unset_dimensions(packer), MSDF_ATLAS_OK);

        /* Setter validation. */
        CHECK_ERR(msdf_atlas_packer_set_dimensions(packer, 0, 128), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
        CHECK_ERR(msdf_atlas_packer_set_scale(packer, 0.0), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
        CHECK_ERR(msdf_atlas_packer_set_minimum_scale(packer, -1.0), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
        CHECK_ERR(msdf_atlas_packer_set_pixel_range(packer, (msdf_atlas_range_t){ 3.0, 2.0 }),
                  MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
        CHECK_ERR(msdf_atlas_packer_set_padding(packer, NULL, NULL, NULL, NULL), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);

        CHECK_ERR(msdf_atlas_packer_destroy(packer), MSDF_ATLAS_OK);
    }

    /* ---- Grid packing ---- */
    {
        msdf_atlas_config_t config;
        msdf_atlas_packer_t *packer = NULL;
        int32_t width = 0, height = 0;
        int32_t columns = 0, rows = 0;
        int32_t cell_w = 0, cell_h = 0;
        bool cutoff = false;
        double origin_x = 0.0, origin_y = 0.0;

        CHECK_ERR(msdf_atlas_config_default(&config), MSDF_ATLAS_OK);
        config.packing_style = MSDF_ATLAS_PACKING_GRID;
        config.pack_width = 256;
        config.pack_height = 256;
        CHECK_ERR(msdf_atlas_packer_create(&config, &packer), MSDF_ATLAS_OK);

        CHECK_ERR(msdf_atlas_packer_set_fixed_origin(packer, false, true), MSDF_ATLAS_OK);
        CHECK_ERR(msdf_atlas_packer_pack(packer, set), MSDF_ATLAS_OK);
        CHECK_ERR(msdf_atlas_packer_get_dimensions(packer, &width, &height), MSDF_ATLAS_OK);
        CHECK(width == 256 && height == 256);
        CHECK_ERR(msdf_atlas_packer_get_columns(packer, &columns), MSDF_ATLAS_OK);
        CHECK_ERR(msdf_atlas_packer_get_rows(packer, &rows), MSDF_ATLAS_OK);
        CHECK(columns > 0 && rows > 0);
        CHECK_ERR(msdf_atlas_packer_get_cell_dimensions(packer, &cell_w, &cell_h), MSDF_ATLAS_OK);
        CHECK(cell_w > 0 && cell_h > 0);
        CHECK_ERR(msdf_atlas_packer_get_fixed_origin(packer, &origin_x, &origin_y), MSDF_ATLAS_OK);
        CHECK_ERR(msdf_atlas_packer_has_cutoff(packer, &cutoff), MSDF_ATLAS_OK);

        /* apply_config can re-tune an existing packer but not change style. */
        config.spacing = 4;
        CHECK_ERR(msdf_atlas_packer_apply_config(packer, &config), MSDF_ATLAS_OK);
        config.packing_style = MSDF_ATLAS_PACKING_TIGHT;
        CHECK_ERR(msdf_atlas_packer_apply_config(packer, &config), MSDF_ATLAS_ERROR_INVALID_STATE);
        expect_message_after_failure();

        CHECK_ERR(msdf_atlas_packer_set_columns(packer, 16), MSDF_ATLAS_OK);
        CHECK_ERR(msdf_atlas_packer_set_rows(packer, 16), MSDF_ATLAS_OK);
        CHECK_ERR(msdf_atlas_packer_pack(packer, set), MSDF_ATLAS_OK);
        CHECK_ERR(msdf_atlas_packer_get_columns(packer, &columns), MSDF_ATLAS_OK);
        CHECK_ERR(msdf_atlas_packer_get_rows(packer, &rows), MSDF_ATLAS_OK);
        CHECK(columns == 16 && rows == 16);

        CHECK_ERR(msdf_atlas_packer_set_columns(packer, 0), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
        CHECK_ERR(msdf_atlas_packer_set_cell_dimensions(packer, 0, 8), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
        CHECK_ERR(msdf_atlas_packer_set_fixed_origin(NULL, true, true), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);

        CHECK_ERR(msdf_atlas_packer_destroy(packer), MSDF_ATLAS_OK);
    }

    /* Packer create validates its config. */
    {
        msdf_atlas_config_t config;
        msdf_atlas_packer_t *packer = NULL;
        CHECK_ERR(msdf_atlas_config_default(&config), MSDF_ATLAS_OK);
        config.struct_size = 0;
        CHECK_ERR(msdf_atlas_packer_create(&config, &packer), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
        CHECK(packer == NULL);
    }

    CHECK_ERR(msdf_atlas_glyphset_destroy(empty_set), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_glyphset_destroy(set), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_charset_destroy(charset), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_font_destroy(font), MSDF_ATLAS_OK);
}

static void test_generator(void) {
    msdf_atlas_font_t *font = NULL;
    msdf_atlas_charset_t *charset = NULL;
    msdf_atlas_glyphset_t *set = NULL;
    msdf_atlas_packer_t *packer = NULL;
    msdf_atlas_generator_t *generator = NULL;
    msdf_atlas_config_t config;
    msdf_atlas_bitmap_t bitmap;
    const msdf_atlas_glyph_layout_t *layouts = NULL;
    size_t layout_count = 0;
    size_t set_count = 0;
    int32_t loaded = 0;
    size_t a_index;
    const float *pixels;
    int32_t x, y;
    double sum = 0.0;

    CHECK_ERR(msdf_atlas_font_open(MSDF_ATLAS_C_SMOKE_FONT_PATH, &font), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_charset_create_ascii(&charset), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_glyphset_create(&set), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_glyphset_load_charset(set, font, 1.0, charset, MSDF_ATLAS_LOAD_KERNING, &loaded),
              MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_glyphset_edge_color(set, MSDF_ATLAS_EDGE_COLORING_INKTRAP, 3.0, 0), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_glyphset_get_count(set, &set_count), MSDF_ATLAS_OK);

    CHECK_ERR(msdf_atlas_packer_create(NULL, &packer), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_packer_pack(packer, set), MSDF_ATLAS_OK);

    /* Default generator: MSDF into RGB32F, bottom-up, auto-sized from the pack. */
    CHECK_ERR(msdf_atlas_generator_create(NULL, &generator), MSDF_ATLAS_OK);
    CHECK(generator != NULL);
    CHECK_ERR(msdf_atlas_generator_set_thread_count(generator, 2), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_generator_set_thread_count(generator, 0), MSDF_ATLAS_OK); /* auto */

    CHECK_ERR(msdf_atlas_generator_get_bitmap(generator, &bitmap), MSDF_ATLAS_OK);
    CHECK(bitmap.pixels == NULL && bitmap.width == 0 && bitmap.height == 0);
    CHECK(bitmap.pixel_format == MSDF_ATLAS_PIXEL_UNKNOWN);

    CHECK_ERR(msdf_atlas_generator_generate(generator, set), MSDF_ATLAS_OK);
    expect_no_message_after_success();
    CHECK_ERR(msdf_atlas_generator_get_bitmap(generator, &bitmap), MSDF_ATLAS_OK);
    CHECK(bitmap.pixels != NULL);
    CHECK(bitmap.width > 0 && bitmap.height > 0);
    CHECK(bitmap.channel_count == 3);
    CHECK(bitmap.pixel_format == MSDF_ATLAS_PIXEL_RGB32F);
    CHECK(bitmap.row_stride_bytes == bitmap.width * 3 * 4);

    /* The MSDF bitmap must actually contain rendered glyphs. */
    pixels = (const float *) bitmap.pixels;
    for (y = 0; y < bitmap.height; y += 3)
        for (x = 0; x < bitmap.width; x += 3) {
            size_t o = (size_t) y * (size_t) (bitmap.row_stride_bytes / 4) + (size_t) x * 3;
            sum += pixels[o] + pixels[o + 1] + pixels[o + 2];
        }
    CHECK(sum != 0.0);

    CHECK_ERR(msdf_atlas_generator_get_layout_count(generator, &layout_count), MSDF_ATLAS_OK);
    CHECK(layout_count == set_count);
    CHECK_ERR(msdf_atlas_generator_get_layout_all(generator, &layouts, &layout_count), MSDF_ATLAS_OK);
    CHECK(layouts != NULL && layout_count == set_count);
    a_index = find_layout(layouts, layout_count, 'A');
    CHECK(a_index != SIZE_MAX);
    CHECK(layouts[a_index].atlas_w > 0 && layouts[a_index].atlas_h > 0);
    CHECK(layouts[a_index].atlas_x >= 0 && layouts[a_index].atlas_y >= 0);
    CHECK(layouts[a_index].atlas_bounds_l >= (double) layouts[a_index].atlas_x);
    CHECK(layouts[a_index].atlas_bounds_t <= (double) (layouts[a_index].atlas_y + layouts[a_index].atlas_h));

    /* Per-index access agrees with the array. */
    {
        msdf_atlas_glyph_layout_t single;
        CHECK_ERR(msdf_atlas_generator_get_layout(generator, a_index, &single), MSDF_ATLAS_OK);
        CHECK(single.codepoint == layouts[a_index].codepoint);
        CHECK(single.atlas_x == layouts[a_index].atlas_x);
        CHECK(single.atlas_y == layouts[a_index].atlas_y);
        CHECK_ERR(msdf_atlas_generator_get_layout(generator, layout_count, &single),
                  MSDF_ATLAS_ERROR_OUT_OF_RANGE);
        expect_message_after_failure();
    }

    CHECK_ERR(msdf_atlas_generator_destroy(generator), MSDF_ATLAS_OK);

    /* Explicit resize + top-down Y direction: layouts flip with the rows. */
    CHECK_ERR(msdf_atlas_config_default(&config), MSDF_ATLAS_OK);
    config.y_direction = MSDF_ATLAS_Y_TOP_DOWN;
    config.flags = MSDF_ATLAS_CONFIG_ERROR_CORRECTION | MSDF_ATLAS_CONFIG_OVERLAP_SUPPORT;
    config.error_correction_mode = MSDF_ATLAS_ERROR_CORRECTION_EDGE_PRIORITY;
    CHECK_ERR(msdf_atlas_generator_create(&config, &generator), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_generator_resize(generator, 256, 128), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_generator_generate(generator, set), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_generator_get_layout_all(generator, &layouts, &layout_count), MSDF_ATLAS_OK);
    a_index = find_layout(layouts, layout_count, 'A');
    CHECK(a_index != SIZE_MAX);
    CHECK(layouts[a_index].atlas_y >= 0 && layouts[a_index].atlas_y < 128);
    CHECK(layouts[a_index].atlas_y + layouts[a_index].atlas_h <= 128);
    CHECK_ERR(msdf_atlas_generator_get_bitmap(generator, &bitmap), MSDF_ATLAS_OK);
    CHECK(bitmap.height == 128);

    /* resize invalidates the previous bitmap pointer (new allocation). */
    CHECK_ERR(msdf_atlas_generator_resize(generator, 64, 64), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_generator_get_bitmap(generator, &bitmap), MSDF_ATLAS_OK);
    CHECK(bitmap.width == 64 && bitmap.height == 64);
    CHECK_ERR(msdf_atlas_generator_set_flags(generator, 0), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_generator_destroy(generator), MSDF_ATLAS_OK);

    /* generate() on a glyphset that was never packed is an invalid state. */
    {
        msdf_atlas_glyphset_t *unpacked = NULL;
        CHECK_ERR(msdf_atlas_glyphset_create(&unpacked), MSDF_ATLAS_OK);
        CHECK_ERR(msdf_atlas_glyphset_load_charset(unpacked, font, 1.0, charset, 0, &loaded), MSDF_ATLAS_OK);
        CHECK_ERR(msdf_atlas_generator_create(NULL, &generator), MSDF_ATLAS_OK);
        CHECK_ERR(msdf_atlas_generator_generate(generator, unpacked), MSDF_ATLAS_ERROR_INVALID_STATE);
        expect_message_after_failure();
        CHECK_ERR(msdf_atlas_generator_generate(generator, NULL), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
        CHECK_ERR(msdf_atlas_generator_generate(NULL, unpacked), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
        CHECK_ERR(msdf_atlas_generator_destroy(generator), MSDF_ATLAS_OK);
        CHECK_ERR(msdf_atlas_glyphset_destroy(unpacked), MSDF_ATLAS_OK);
    }

    /* 8-bit MSDF output (explicit pixel format, byte storage). */
    {
        const unsigned char *pixels8;
        CHECK_ERR(msdf_atlas_config_default(&config), MSDF_ATLAS_OK);
        config.pixel_format = MSDF_ATLAS_PIXEL_RGB8;
        CHECK_ERR(msdf_atlas_generator_create(&config, &generator), MSDF_ATLAS_OK);
        CHECK_ERR(msdf_atlas_generator_generate(generator, set), MSDF_ATLAS_OK);
        CHECK_ERR(msdf_atlas_generator_get_bitmap(generator, &bitmap), MSDF_ATLAS_OK);
        CHECK(bitmap.channel_count == 3);
        CHECK(bitmap.pixel_format == MSDF_ATLAS_PIXEL_RGB8);
        CHECK(bitmap.row_stride_bytes == bitmap.width * 3);
        pixels8 = (const unsigned char *) bitmap.pixels;
        CHECK(pixels8[0] == 0 && pixels8[1] == 0); /* first byte of a fresh atlas */
        CHECK_ERR(msdf_atlas_generator_destroy(generator), MSDF_ATLAS_OK);
    }

    /* Mismatched pixel format is rejected at create. */
    CHECK_ERR(msdf_atlas_config_default(&config), MSDF_ATLAS_OK);
    config.pixel_format = MSDF_ATLAS_PIXEL_RGBA8;
    CHECK_ERR(msdf_atlas_generator_create(&config, &generator), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);
    CHECK(generator == NULL);
    CHECK_ERR(msdf_atlas_generator_create(&config, NULL), MSDF_ATLAS_ERROR_INVALID_ARGUMENT);

    CHECK_ERR(msdf_atlas_packer_destroy(packer), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_glyphset_destroy(set), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_charset_destroy(charset), MSDF_ATLAS_OK);
    CHECK_ERR(msdf_atlas_font_destroy(font), MSDF_ATLAS_OK);
}

int main(void) {
    test_errors_and_config();
    test_charset();
    test_font();
    test_glyphset_and_pack();
    test_generator();

    if (g_failures == 0) {
        printf("msdf-atlas-c smoke: all checks passed\n");
        return 0;
    }
    fprintf(stderr, "msdf-atlas-c smoke: %d check(s) failed\n", g_failures);
    return 1;
}
