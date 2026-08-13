/*
 * msdf_atlas_c.h — public C API for msdf-atlas-gen (APX-219)
 * ===========================================================
 *
 * A pure C99 interface over the C++ msdf-atlas-gen / msdfgen libraries.
 * This header defines the full API surface; the implementation (the
 * "wrapper") is provided separately and is NOT part of this header.
 *
 * Design rules
 * ------------
 * - Pure C99: every type used in a signature is a C type (stdint/stddef/
 *   stdbool). No C++ types, no templates, no exceptions. The header compiles
 *   standalone as C99 and as C++ (extern "C" guards).
 * - Opaque handles: msdf_atlas_font_t, msdf_atlas_charset_t,
 *   msdf_atlas_glyphset_t, msdf_atlas_packer_t, msdf_atlas_generator_t.
 *   Handles are created by an explicit create/open function and released
 *   by the matching destroy/close function. A handle that has been
 *   destroyed must not be used again. Destroying a handle never frees data
 *   owned by a different handle.
 * - Error reporting: every fallible call returns msdf_atlas_error_t
 *   (MSDF_ATLAS_OK == 0 on success). msdf_atlas_error_string() maps a code
 *   to static text; msdf_atlas_last_error_message() returns a detail message
 *   recorded by the most recent failing call on the calling thread.
 * - Ownership of returned buffers: pointers returned by this API are owned
 *   by the object that returned them, are never freed by the caller, and are
 *   invalidated by the next mutating call on that object or by destroying
 *   the object. The one exception is the buffer passed to
 *   msdf_atlas_font_open_memory(): the caller keeps ownership and it must
 *   outlive the returned font handle.
 * - Nullability: NULL out-parameters are errors (MSDF_ATLAS_ERROR_INVALID_
 *   ARGUMENT) unless the documentation says otherwise. Input handles must be
 *   non-NULL; NULL is an invalid argument everywhere.
 * - Threading: handles are not thread-safe; a single handle must not be
 *   used concurrently from multiple threads. msdf_atlas_generator_t may use
 *   internal worker threads while msdf_atlas_generator_generate() runs (see
 *   thread_count in msdf_atlas_config_t). The last-error message is
 *   thread-local.
 * - ABI: enum values and struct layouts are fixed. Do not reorder struct
 *   fields or change enum values; append instead. msdf_atlas_config_t is
 *   versioned through its struct_size field.
 *
 * Version: MSDF_ATLAS_C_API_VERSION
 */

#ifndef MSDF_ATLAS_C_H
#define MSDF_ATLAS_C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Version of this C API surface. Bump only on incompatible ABI changes. */
#define MSDF_ATLAS_C_API_VERSION 1

/*
 * Export macro. Define MSDF_ATLAS_C_STATIC to force a plain declaration on
 * Windows; define MSDF_ATLAS_C_BUILD when building the wrapper itself.
 * Overridable by defining MSDF_ATLAS_C_API before including this header.
 */
#ifndef MSDF_ATLAS_C_API
#if defined(_WIN32) && !defined(MSDF_ATLAS_C_STATIC)
#if defined(MSDF_ATLAS_C_BUILD)
#define MSDF_ATLAS_C_API __declspec(dllexport)
#else
#define MSDF_ATLAS_C_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define MSDF_ATLAS_C_API __attribute__((visibility("default")))
#else
#define MSDF_ATLAS_C_API
#endif
#endif

/* ------------------------------------------------------------------ */
/* Opaque handles                                                      */
/* ------------------------------------------------------------------ */

/**
 * A loaded font (wrapper over msdfgen's FontHandle). Created with
 * msdf_atlas_font_open() / msdf_atlas_font_open_memory(), released with
 * msdf_atlas_font_destroy(). Read-only after creation.
 */
typedef struct msdf_atlas_font msdf_atlas_font_t;

/**
 * A set of Unicode codepoints (wrapper over msdf_atlas::Charset). Created
 * with msdf_atlas_charset_create() / msdf_atlas_charset_create_ascii(),
 * released with msdf_atlas_charset_destroy().
 */
typedef struct msdf_atlas_charset msdf_atlas_charset_t;

/**
 * Glyph geometry of one font (wrapper over msdf_atlas::FontGeometry and its
 * array of msdf_atlas::GlyphGeometry). Created with
 * msdf_atlas_glyphset_create(), released with msdf_atlas_glyphset_destroy().
 * Glyphs are loaded into it from a msdf_atlas_font_t; the packer writes
 * atlas placement back into it; the generator reads it.
 */
typedef struct msdf_atlas_glyphset msdf_atlas_glyphset_t;

/**
 * Atlas layout calculator (wrapper over msdf_atlas::TightAtlasPacker or
 * msdf_atlas::GridAtlasPacker, selected by config.packing_style). Created
 * with msdf_atlas_packer_create(), released with msdf_atlas_packer_destroy().
 */
typedef struct msdf_atlas_packer msdf_atlas_packer_t;

/**
 * Atlas bitmap generator (wrapper over
 * msdf_atlas::ImmediateAtlasGenerator<T, N, fn, BitmapAtlasStorage<T, N>>).
 * Created with msdf_atlas_generator_create(), released with
 * msdf_atlas_generator_destroy(). Produces the final atlas bitmap.
 */
typedef struct msdf_atlas_generator msdf_atlas_generator_t;

/* ------------------------------------------------------------------ */
/* Enumerations                                                        */
/* ------------------------------------------------------------------ */

/** Result code returned by every fallible call in this API. */
typedef enum msdf_atlas_error {
    MSDF_ATLAS_OK = 0,               /**< Success. */
    MSDF_ATLAS_ERROR_INVALID_ARGUMENT, /**< NULL handle/param, bad value, malformed config. */
    MSDF_ATLAS_ERROR_OUT_OF_MEMORY,  /**< Allocation failed. */
    MSDF_ATLAS_ERROR_OUT_OF_RANGE,   /**< Index/identifier out of range. */
    MSDF_ATLAS_ERROR_INVALID_STATE,  /**< Call not valid in the object's current state (e.g. pack before load, grid-only setter on a tight packer). */
    MSDF_ATLAS_ERROR_UNSUPPORTED,    /**< Unsupported combination (e.g. pixel format vs. image type). */
    MSDF_ATLAS_ERROR_IO,             /**< File or stream I/O failure. */
    MSDF_ATLAS_ERROR_FONT_LOAD,      /**< Font file/data could not be loaded. */
    MSDF_ATLAS_ERROR_GLYPH_LOAD,     /**< Glyph geometry could not be loaded from the font. */
    MSDF_ATLAS_ERROR_CHARSET_PARSE,  /**< Charset string is syntactically invalid. */
    MSDF_ATLAS_ERROR_PACK,           /**< Layout computation failed. */
    MSDF_ATLAS_ERROR_GENERATE,       /**< Bitmap generation failed. */
    MSDF_ATLAS_ERROR_COUNT           /**< Number of error codes (not a valid code). */
} msdf_atlas_error_t;

/** Type of atlas image contents; selects the distance-field variant. */
typedef enum msdf_atlas_image_type {
    MSDF_ATLAS_IMAGE_HARD_MASK = 0,  /**< Rendered glyphs, no anti-aliasing (two colors). */
    MSDF_ATLAS_IMAGE_SOFT_MASK,      /**< Rendered glyphs with anti-aliasing. */
    MSDF_ATLAS_IMAGE_SDF,            /**< Signed (true) distance field. */
    MSDF_ATLAS_IMAGE_PSDF,           /**< Signed perpendicular distance field. */
    MSDF_ATLAS_IMAGE_MSDF,           /**< Multi-channel signed distance field (default). */
    MSDF_ATLAS_IMAGE_MTSDF           /**< Multi-channel & true signed distance field. */
} msdf_atlas_image_type_t;

/** Memory layout of the atlas bitmap produced by the generator. */
typedef enum msdf_atlas_pixel_format {
    MSDF_ATLAS_PIXEL_UNKNOWN = 0,    /**< Derive from image_type (see msdf_atlas_config_t). */
    MSDF_ATLAS_PIXEL_R8,             /**< unsigned byte, 1 channel. */
    MSDF_ATLAS_PIXEL_RGB8,           /**< unsigned byte, 3 channels. */
    MSDF_ATLAS_PIXEL_RGBA8,          /**< unsigned byte, 4 channels. */
    MSDF_ATLAS_PIXEL_R32F,           /**< float, 1 channel. */
    MSDF_ATLAS_PIXEL_RGB32F,         /**< float, 3 channels. */
    MSDF_ATLAS_PIXEL_RGBA32F         /**< float, 4 channels. */
} msdf_atlas_pixel_format_t;

/** Direction of the Y axis in the produced bitmap (see msdf_atlas_bitmap_t). */
typedef enum msdf_atlas_y_direction {
    MSDF_ATLAS_Y_BOTTOM_UP = 0,      /**< First row in memory is the bottom row of the atlas. */
    MSDF_ATLAS_Y_TOP_DOWN            /**< First row in memory is the top row of the atlas. */
} msdf_atlas_y_direction_t;

/** How the glyph boxes are laid out in the atlas. */
typedef enum msdf_atlas_packing_style {
    MSDF_ATLAS_PACKING_TIGHT = 0,    /**< Tightly packed rectangles (default). */
    MSDF_ATLAS_PACKING_GRID          /**< Uniform grid of cells. */
} msdf_atlas_packing_style_t;

/** Constraint applied when the packer auto-selects atlas dimensions. */
typedef enum msdf_atlas_dimensions_constraint {
    MSDF_ATLAS_DIMENSIONS_NONE = 0,              /**< No constraint. */
    MSDF_ATLAS_DIMENSIONS_SQUARE,                /**< width == height. */
    MSDF_ATLAS_DIMENSIONS_EVEN_SQUARE,           /**< Square with even dimensions. */
    MSDF_ATLAS_DIMENSIONS_MULTIPLE_OF_FOUR_SQUARE, /**< Square, multiple of four. */
    MSDF_ATLAS_DIMENSIONS_POWER_OF_TWO_RECTANGLE,  /**< Power-of-two width and height. */
    MSDF_ATLAS_DIMENSIONS_POWER_OF_TWO_SQUARE    /**< Square, power of two. */
} msdf_atlas_dimensions_constraint_t;

/** How glyph identifiers are interpreted by msdf_atlas_glyphset_get_advance(). */
typedef enum msdf_atlas_glyph_identifier_type {
    MSDF_ATLAS_IDENTIFIER_GLYPH_INDEX = 0,       /**< Font glyph indices. */
    MSDF_ATLAS_IDENTIFIER_UNICODE_CODEPOINT      /**< Unicode codepoints. */
} msdf_atlas_glyph_identifier_type_t;

/** Edge coloring heuristic applied to glyph shapes. */
typedef enum msdf_atlas_edge_coloring {
    MSDF_ATLAS_EDGE_COLORING_NONE = 0,  /**< Skip edge coloring. */
    MSDF_ATLAS_EDGE_COLORING_SIMPLE,    /**< Simple coloring heuristic. */
    MSDF_ATLAS_EDGE_COLORING_INKTRAP,   /**< "Ink trap" heuristic (recommended). */
    MSDF_ATLAS_EDGE_COLORING_DIAGONAL,  /**< Diagonal coloring. */
    MSDF_ATLAS_EDGE_COLORING_BY_DISTANCE /**< Color edges by distance. */
} msdf_atlas_edge_coloring_t;

/** MSDF error correction pass mode (maps to msdfgen ErrorCorrectionConfig). */
typedef enum msdf_atlas_error_correction_mode {
    MSDF_ATLAS_ERROR_CORRECTION_DISABLED = 0,  /**< Skip the error correction pass. */
    MSDF_ATLAS_ERROR_CORRECTION_INDISCRIMINATE, /**< Correct all discontinuities. */
    MSDF_ATLAS_ERROR_CORRECTION_EDGE_PRIORITY,  /**< Correct artifacts not affecting edges (default). */
    MSDF_ATLAS_ERROR_CORRECTION_EDGE_ONLY       /**< Correct only edge artifacts. */
} msdf_atlas_error_correction_mode_t;

/** When the error correction pass computes exact shape distances. */
typedef enum msdf_atlas_error_correction_check {
    MSDF_ATLAS_ERROR_CHECK_DO_NOT_CHECK = 0,  /**< Never compute exact distance. */
    MSDF_ATLAS_ERROR_CHECK_AT_EDGE,           /**< Only at edges (default). */
    MSDF_ATLAS_ERROR_CHECK_ALWAYS             /**< For every suspected artifact. */
} msdf_atlas_error_correction_check_t;

/* ------------------------------------------------------------------ */
/* POD result / configuration structs                                  */
/* ------------------------------------------------------------------ */

/** A closed interval [lower, upper] used for distance ranges. */
typedef struct msdf_atlas_range {
    double lower;
    double upper;
} msdf_atlas_range_t;

/** Per-side padding amounts. */
typedef struct msdf_atlas_padding {
    double l; /**< Left. */
    double b; /**< Bottom. */
    double r; /**< Right. */
    double t; /**< Top. */
} msdf_atlas_padding_t;

/** Global metrics of a font, in font units (see msdf_atlas_font_get_metrics). */
typedef struct msdf_atlas_font_metrics {
    double em_size;             /**< Size of one em. */
    double ascender_y;          /**< Ascender position above the baseline. */
    double descender_y;         /**< Descender position below the baseline. */
    double line_height;         /**< Vertical distance between baselines. */
    double underline_y;         /**< Underline position. */
    double underline_thickness; /**< Underline thickness. */
} msdf_atlas_font_metrics_t;

/**
 * Layout of one glyph, POD and ABI-stable.
 *
 * Valid after glyph load: codepoint, glyph_index, advance, plane bounds.
 * Valid after a successful pack: atlas bounds and atlas rect (the packer
 * writes the placement into the glyphset; the generator copies it into its
 * own layout array, see msdf_atlas_generator_get_layout*).
 */
typedef struct msdf_atlas_glyph_layout {
    uint32_t codepoint;   /**< Unicode codepoint; 0 if unknown. */
    int32_t  glyph_index; /**< Glyph index within the font. */
    double   advance;     /**< Horizontal advance in font units. */
    /** Quad bounds in plane coordinates (left, bottom, right, top). */
    double plane_bounds_l, plane_bounds_b, plane_bounds_r, plane_bounds_t;
    /** Quad bounds in atlas pixel coordinates (left, bottom, right, top). */
    double atlas_bounds_l, atlas_bounds_b, atlas_bounds_r, atlas_bounds_t;
    /** Glyph box rectangle in atlas pixels. */
    int32_t atlas_x, atlas_y, atlas_w, atlas_h;
    int32_t is_whitespace; /**< Non-zero if the glyph has no geometry. */
} msdf_atlas_glyph_layout_t;

/**
 * The generated atlas bitmap, POD and ABI-stable.
 *
 * Ownership: pixels points into memory owned by the generator that returned
 * this struct. The caller must not free it. It stays valid until the next
 * msdf_atlas_generator_generate() / msdf_atlas_generator_resize() /
 * msdf_atlas_generator_destroy() on that generator. If generation has not
 * run yet, pixels is NULL and the remaining fields are zero.
 */
typedef struct msdf_atlas_bitmap {
    const void *pixels;         /**< Row-major pixel data; owned by the generator. */
    int32_t width;              /**< Width in pixels. */
    int32_t height;             /**< Height in pixels. */
    int32_t channel_count;      /**< Channels per pixel (1, 3 or 4). */
    msdf_atlas_pixel_format_t pixel_format; /**< Component type and channel count. */
    int32_t row_stride_bytes;   /**< Bytes between consecutive rows (>= width * channels * component size). */
} msdf_atlas_bitmap_t;

/*
 * Bit flags for msdf_atlas_config_t.flags.
 */
/** Enable the MSDF error correction pass (generator). */
#define MSDF_ATLAS_CONFIG_ERROR_CORRECTION (1u << 0)
/** Enable overlap support in the SDF generator (slower; safe when contours overlap). */
#define MSDF_ATLAS_CONFIG_OVERLAP_SUPPORT  (1u << 1)
/** Use the scanline-based SDF pass instead of the analytical one (generator). */
#define MSDF_ATLAS_CONFIG_SCANLINE_PASS    (1u << 2)
/** Keep each glyph origin aligned to the pixel grid on X (packer). */
#define MSDF_ATLAS_CONFIG_PX_ALIGN_ORIGIN_X (1u << 3)
/** Keep each glyph origin aligned to the pixel grid on Y (packer). */
#define MSDF_ATLAS_CONFIG_PX_ALIGN_ORIGIN_Y (1u << 4)
/** Grid packing only: fix the origin position on X (uses fixed_origin_x). */
#define MSDF_ATLAS_CONFIG_GRID_FIXED_ORIGIN_X (1u << 5)
/** Grid packing only: fix the origin position on Y (uses fixed_origin_y). */
#define MSDF_ATLAS_CONFIG_GRID_FIXED_ORIGIN_Y (1u << 6)

/*
 * Bit flags for the load_flags parameter of msdf_atlas_glyphset_load_*().
 */
/** Load kerning pairs for the loaded glyphs (msdf_atlas_glyphset_get_advance). */
#define MSDF_ATLAS_LOAD_KERNING (1u << 0)
/** Preprocess glyph geometry (merge contours, remove overlaps) on load. */
#define MSDF_ATLAS_LOAD_PREPROCESS_GEOMETRY (1u << 1)

/**
 * Configuration of one atlas generation run, POD and ABI-stable.
 *
 * Initialize with msdf_atlas_config_default(), then override fields. The
 * struct_size field must equal sizeof(msdf_atlas_config_t); every entry
 * point validates it (forward compatibility for appended fields).
 *
 * Numeric convention: a dimension, scale or count field <= 0 means "auto /
 * unset" and lets the library pick a value (see each field). Zero-initialized
 * structs are therefore safe, but use msdf_atlas_config_default() for the
 * documented defaults.
 */
typedef struct msdf_atlas_config {
    /** Must equal sizeof(msdf_atlas_config_t); set by msdf_atlas_config_default(). */
    uint32_t struct_size;
    /** Combination of MSDF_ATLAS_CONFIG_* flags. */
    uint32_t flags;

    /** Atlas contents / MSDF variant. Default: MSDF_ATLAS_IMAGE_MSDF. */
    msdf_atlas_image_type_t image_type;
    /**
     * Bitmap pixel format. MSDF_ATLAS_PIXEL_UNKNOWN (default) derives it
     * from image_type: HARD_MASK/SOFT_MASK -> R8, SDF/PSDF -> R32F,
     * MSDF -> RGB32F, MTSDF -> RGBA32F. When set explicitly, the channel
     * count must match image_type (1 for mask/SDF/PSDF, 3 for MSDF, 4 for
     * MTSDF); component type (byte/float) may be chosen freely.
     */
    msdf_atlas_pixel_format_t pixel_format;
    /** Row order of the produced bitmap. Default: MSDF_ATLAS_Y_BOTTOM_UP. */
    msdf_atlas_y_direction_t y_direction;
    /** Layout algorithm. Default: MSDF_ATLAS_PACKING_TIGHT. */
    msdf_atlas_packing_style_t packing_style;
    /** Constraint for auto-computed atlas dimensions. Default: NONE. */
    msdf_atlas_dimensions_constraint_t dimensions_constraint;

    /** Fixed atlas width in pixels; <= 0 lets the packer choose. */
    int32_t pack_width;
    /** Fixed atlas height in pixels; <= 0 lets the packer choose. */
    int32_t pack_height;

    /** Grid packing: fixed column count; <= 0 auto. */
    int32_t columns;
    /** Grid packing: fixed row count; <= 0 auto. */
    int32_t rows;
    /** Grid packing: fixed cell width; <= 0 auto. */
    int32_t cell_width;
    /** Grid packing: fixed cell height; <= 0 auto. */
    int32_t cell_height;

    /** Glyph scale in ems; <= 0 maximizes the scale that fits the atlas. */
    double glyph_scale;
    /** Minimum accepted glyph scale when auto-maximizing; 0 = no minimum. */
    double min_glyph_scale;

    /** Distance range in font units (unit component); 0 = none. */
    msdf_atlas_range_t unit_range;
    /** Distance range in pixels as {lower, upper} endpoints (not a width).
     *  C++ Range(2.0) is {-1.0, 1.0}. Default: {2.0, 2.0} (legacy). */
    msdf_atlas_range_t px_range;
    /** Miter limit for glyph bounds computation. Default: 1.0. */
    double miter_limit;
    /** Spacing between glyph boxes in pixels. Default: 0. */
    int32_t spacing;

    /** Inner padding (part of the glyph quad) in pixels. Default: 0. */
    msdf_atlas_padding_t inner_px_padding;
    /** Outer padding (around the glyph quad) in pixels. Default: 0. */
    msdf_atlas_padding_t outer_px_padding;
    /** Inner padding in font units. Default: 0. */
    msdf_atlas_padding_t inner_unit_padding;
    /** Outer padding in font units. Default: 0. */
    msdf_atlas_padding_t outer_unit_padding;

    /**
     * Grid packing: fixed origin position within each cell. Used only when
     * MSDF_ATLAS_CONFIG_GRID_FIXED_ORIGIN_X / _Y is set in flags.
     */
    double fixed_origin_x;
    double fixed_origin_y;

    /**
     * Worker threads for generation; <= 0 uses an implementation-chosen
     * default (typically hardware concurrency). Generation is synchronous:
     * msdf_atlas_generator_generate() returns when all threads finished.
     */
    int32_t thread_count;

    /** Error correction mode. Default: EDGE_PRIORITY (used when the ERROR_CORRECTION flag is set). */
    msdf_atlas_error_correction_mode_t error_correction_mode;
    /** Error correction distance check mode. Default: CHECK_AT_EDGE. */
    msdf_atlas_error_correction_check_t error_correction_check;
} msdf_atlas_config_t;

/* ------------------------------------------------------------------ */
/* Version and error reporting                                         */
/* ------------------------------------------------------------------ */

/**
 * Returns the underlying msdf-atlas-gen version string (static storage,
 * owned by the library, never NULL).
 */
MSDF_ATLAS_C_API const char *msdf_atlas_version(void);

/**
 * Returns a static, human-readable description of an error code. Never NULL;
 * valid for the lifetime of the process; not thread-specific. Unknown codes
 * return a generic message.
 */
MSDF_ATLAS_C_API const char *msdf_atlas_error_string(msdf_atlas_error_t error);

/**
 * Returns a human-readable message for the most recent error recorded on the
 * calling thread, or NULL if the most recent call succeeded and recorded no
 * message. Ownership: the returned string is owned by the library and stays
 * valid until the next msdf_atlas_* call on the same thread. Never free it.
 */
MSDF_ATLAS_C_API const char *msdf_atlas_last_error_message(void);

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

/**
 * Fills out_config with the documented defaults and sets struct_size.
 * @param out_config Required, non-NULL. On success contains a valid config;
 *     on error it is left untouched.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT if out_config
 *     is NULL.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_config_default(msdf_atlas_config_t *out_config);

/**
 * Validates a config without using it: struct_size must match, enum fields
 * must be in range, explicit pixel_format must match image_type's channel
 * count, dimensions/ranges must be sane (non-negative where unsigned).
 * @param config Required, non-NULL.
 * @return MSDF_ATLAS_OK if valid, otherwise an error code describing the
 *     first offending field.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_config_validate(const msdf_atlas_config_t *config);

/* ------------------------------------------------------------------ */
/* Charset                                                             */
/* ------------------------------------------------------------------ */

/**
 * Creates an empty charset. The caller owns the handle and must release it
 * with msdf_atlas_charset_destroy().
 * @param out_charset Required, non-NULL. Receives the new handle, or NULL on
 *     failure (out_charset is set to NULL on error).
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL out param),
 *     MSDF_ATLAS_ERROR_OUT_OF_MEMORY.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_charset_create(msdf_atlas_charset_t **out_charset);

/**
 * Creates a charset pre-filled with the 95 printable ASCII characters.
 * Ownership and errors as in msdf_atlas_charset_create().
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_charset_create_ascii(msdf_atlas_charset_t **out_charset);

/**
 * Destroys a charset and frees all resources it owns. The handle must not be
 * used afterwards. Passing NULL is a no-op (returns MSDF_ATLAS_OK).
 * @return MSDF_ATLAS_OK.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_charset_destroy(msdf_atlas_charset_t *charset);

/**
 * Adds a codepoint to the charset (idempotent).
 * @param charset Required, non-NULL.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT if charset is
 *     NULL.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_charset_add(msdf_atlas_charset_t *charset, uint32_t codepoint);

/**
 * Removes a codepoint from the charset (no-op if absent).
 * @param charset Required, non-NULL.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT if charset is
 *     NULL.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_charset_remove(msdf_atlas_charset_t *charset, uint32_t codepoint);

/**
 * Tests whether a codepoint is present.
 * @param charset Required, non-NULL.
 * @param out_contains Required, non-NULL; receives 1 if present, else 0.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT on NULL input.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_charset_contains(const msdf_atlas_charset_t *charset, uint32_t codepoint, bool *out_contains);

/**
 * Returns the number of codepoints in the charset.
 * @param charset Required, non-NULL.
 * @param out_size Required, non-NULL.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT on NULL input.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_charset_size(const msdf_atlas_charset_t *charset, size_t *out_size);

/**
 * Adds all codepoints described by text (msdf-atlas-gen charset syntax, e.g.
 * "a-z0-9" or "U+0041-U+005A"; character literals are accepted). The parse
 * is atomic: on failure the charset is unchanged.
 * @param charset Required, non-NULL.
 * @param text Required, non-NULL; NUL-terminated. text_length bytes are read
 *     (the NUL terminator is not required to be at text[text_length]).
 * @param text_length Length of text in bytes; may be SIZE_MAX for strlen.
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL input),
 *     MSDF_ATLAS_ERROR_CHARSET_PARSE (syntax error; a detail message is
 *     recorded for msdf_atlas_last_error_message()).
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_charset_parse(msdf_atlas_charset_t *charset, const char *text, size_t text_length);

/* ------------------------------------------------------------------ */
/* Font                                                                */
/* ------------------------------------------------------------------ */

/**
 * Opens a font file (TTF/OTF and other FreeType-supported formats). The
 * caller owns the returned handle and must release it with
 * msdf_atlas_font_destroy(). The file is read eagerly: it may be closed or
 * removed after this call.
 * @param path Required, non-NULL, NUL-terminated.
 * @param out_font Required, non-NULL; receives the new handle, or NULL on
 *     failure (set to NULL on error).
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL input),
 *     MSDF_ATLAS_ERROR_IO, MSDF_ATLAS_ERROR_FONT_LOAD,
 *     MSDF_ATLAS_ERROR_OUT_OF_MEMORY.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_font_open(const char *path, msdf_atlas_font_t **out_font);

/**
 * Opens a font from an in-memory buffer. Ownership: the caller keeps
 * ownership of data; data must remain valid and unmodified until the
 * returned handle is destroyed.
 * @param data Required, non-NULL; data_size bytes of a font file.
 * @param data_size Size of data in bytes; must be > 0.
 * @param out_font Required, non-NULL; receives the new handle, or NULL on
 *     failure (set to NULL on error).
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL input or
 *     zero size), MSDF_ATLAS_ERROR_FONT_LOAD, MSDF_ATLAS_ERROR_OUT_OF_MEMORY.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_font_open_memory(const void *data, size_t data_size, msdf_atlas_font_t **out_font);

/**
 * Destroys a font handle and frees all resources it owns. Does not free any
 * buffer passed to msdf_atlas_font_open_memory() (that stays with the
 * caller). The handle must not be used afterwards. NULL is a no-op.
 * @return MSDF_ATLAS_OK.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_font_destroy(msdf_atlas_font_t *font);

/**
 * Returns the font's global metrics in font units.
 * @param font Required, non-NULL.
 * @param out_metrics Required, non-NULL.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT on NULL input.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_font_get_metrics(const msdf_atlas_font_t *font, msdf_atlas_font_metrics_t *out_metrics);

/**
 * Returns the total number of glyphs in the font.
 * @param font Required, non-NULL.
 * @param out_count Required, non-NULL.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT on NULL input.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_font_get_glyph_count(const msdf_atlas_font_t *font, uint32_t *out_count);

/**
 * Maps a Unicode codepoint to its glyph index in the font.
 * @param font Required, non-NULL.
 * @param codepoint Unicode codepoint.
 * @param out_glyph_index Required, non-NULL; receives the glyph index.
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL input), or
 *     MSDF_ATLAS_ERROR_OUT_OF_RANGE if the codepoint has no glyph.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_font_get_glyph_index(const msdf_atlas_font_t *font, uint32_t codepoint, uint32_t *out_glyph_index);

/* ------------------------------------------------------------------ */
/* Glyph geometry (glyphset)                                           */
/* ------------------------------------------------------------------ */

/**
 * Creates an empty glyphset. The caller owns the handle and must release it
 * with msdf_atlas_glyphset_destroy(). Glyphs are added with the load
 * functions; a glyphset can be loaded, packed and generated repeatedly
 * (each load replaces the previously loaded glyphs).
 * @param out_set Required, non-NULL; receives the new handle, or NULL on
 *     failure (set to NULL on error).
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL out param),
 *     MSDF_ATLAS_ERROR_OUT_OF_MEMORY.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_glyphset_create(msdf_atlas_glyphset_t **out_set);

/**
 * Destroys a glyphset and frees all resources it owns. Does not destroy the
 * font it was loaded from. The handle must not be used afterwards. NULL is a
 * no-op.
 * @return MSDF_ATLAS_OK.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_glyphset_destroy(msdf_atlas_glyphset_t *set);

/**
 * Loads glyph geometry for every codepoint in charset. Replaces any
 * previously loaded glyphs in set.
 * @param set Required, non-NULL.
 * @param font Required, non-NULL; must outlive the glyphset.
 * @param font_scale Glyph scale in ems (1.0 = one em); must be > 0.
 * @param charset Required, non-NULL.
 * @param load_flags Combination of MSDF_ATLAS_LOAD_* flags (0 allowed).
 * @param out_loaded Optional, may be NULL; receives the number of glyphs
 *     successfully loaded (codepoints without a glyph are skipped, not
 *     errors).
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL input or
 *     font_scale <= 0), MSDF_ATLAS_ERROR_OUT_OF_MEMORY.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_glyphset_load_charset(msdf_atlas_glyphset_t *set, const msdf_atlas_font_t *font, double font_scale, const msdf_atlas_charset_t *charset, uint32_t load_flags, int32_t *out_loaded);

/**
 * Loads glyph geometry for every glyph index in charset (the charset's
 * values are interpreted as glyph indices, not codepoints). Replaces any
 * previously loaded glyphs in set.
 * @param set Required, non-NULL.
 * @param font Required, non-NULL; must outlive the glyphset.
 * @param font_scale Glyph scale in ems; must be > 0.
 * @param glyph_indices Required, non-NULL; charset of glyph indices.
 * @param load_flags Combination of MSDF_ATLAS_LOAD_* flags (0 allowed).
 * @param out_loaded Optional, may be NULL; receives the number of glyphs
 *     successfully loaded.
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL input or
 *     font_scale <= 0), MSDF_ATLAS_ERROR_OUT_OF_MEMORY.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_glyphset_load_glyphset(msdf_atlas_glyphset_t *set, const msdf_atlas_font_t *font, double font_scale, const msdf_atlas_charset_t *glyph_indices, uint32_t load_flags, int32_t *out_loaded);

/**
 * Loads glyph geometry for a consecutive range of glyph indices
 * [first_glyph_index, first_glyph_index + glyph_count). Replaces any
 * previously loaded glyphs in set.
 * @param set Required, non-NULL.
 * @param font Required, non-NULL; must outlive the glyphset.
 * @param font_scale Glyph scale in ems; must be > 0.
 * @param first_glyph_index First glyph index (inclusive).
 * @param glyph_count Number of glyphs to load.
 * @param load_flags Combination of MSDF_ATLAS_LOAD_* flags (0 allowed).
 * @param out_loaded Optional, may be NULL; receives the number of glyphs
 *     successfully loaded.
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL input,
 *     font_scale <= 0 or glyph_count == 0), MSDF_ATLAS_ERROR_OUT_OF_MEMORY.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_glyphset_load_glyph_range(msdf_atlas_glyphset_t *set, const msdf_atlas_font_t *font, double font_scale, uint32_t first_glyph_index, uint32_t glyph_count, uint32_t load_flags, int32_t *out_loaded);

/**
 * Applies an edge coloring heuristic to all loaded glyph shapes. Required
 * for good MSDF/MTSDF quality; harmless for other image types.
 * @param set Required, non-NULL; must contain at least one glyph
 *     (MSDF_ATLAS_ERROR_INVALID_STATE otherwise).
 * @param coloring MSDF_ATLAS_EDGE_COLORING_NONE skips the pass and succeeds.
 * @param angle_threshold Edge angle threshold in degrees (0 <= t <= 180);
 *     values outside [0, 180] are clamped by the implementation.
 * @param seed Seed for the coloring heuristic.
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL set),
 *     MSDF_ATLAS_ERROR_INVALID_STATE (empty glyphset).
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_glyphset_edge_color(msdf_atlas_glyphset_t *set, msdf_atlas_edge_coloring_t coloring, double angle_threshold, uint64_t seed);

/**
 * Returns the number of loaded glyphs in set.
 * @param set Required, non-NULL.
 * @param out_count Required, non-NULL.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT on NULL input.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_glyphset_get_count(const msdf_atlas_glyphset_t *set, size_t *out_count);

/**
 * Returns the layout of one loaded glyph. plane_* fields are valid after
 * load; atlas_* fields are valid only after a successful pack or generate
 * (zero otherwise).
 * @param set Required, non-NULL.
 * @param glyph_index Index in [0, count) of the glyph (the order glyphs were
 *     loaded in, not the font glyph index).
 * @param out_layout Required, non-NULL.
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL input),
 *     MSDF_ATLAS_ERROR_OUT_OF_RANGE (glyph_index >= count).
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_glyphset_get_layout(const msdf_atlas_glyphset_t *set, size_t glyph_index, msdf_atlas_glyph_layout_t *out_layout);

/**
 * Returns the advance between two glyphs with kerning applied. Kerning must
 * have been loaded (MSDF_ATLAS_LOAD_KERNING) and the identifiers must
 * identify glyphs present in the set.
 * @param set Required, non-NULL.
 * @param identifier_a, identifier_b Glyph identifiers (both must resolve to
 *     glyphs present in set).
 * @param identifier_type How the identifiers are interpreted.
 * @param out_advance Required, non-NULL; receives the kerned advance in font
 *     units.
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL input or
 *     invalid identifier_type), MSDF_ATLAS_ERROR_OUT_OF_RANGE (unknown
 *     identifier), MSDF_ATLAS_ERROR_INVALID_STATE (kerning not loaded).
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_glyphset_get_advance(const msdf_atlas_glyphset_t *set, uint32_t identifier_a, uint32_t identifier_b, msdf_atlas_glyph_identifier_type_t identifier_type, double *out_advance);

/* ------------------------------------------------------------------ */
/* Packer                                                              */
/* ------------------------------------------------------------------ */

/**
 * Creates a packer configured by config (TIGHT or GRID per
 * config.packing_style). config may be NULL, in which case defaults from
 * msdf_atlas_config_default() apply. The caller owns the handle and must
 * release it with msdf_atlas_packer_destroy(). The packer keeps no reference
 * to config.
 * @param config Optional, may be NULL; validated when non-NULL.
 * @param out_packer Required, non-NULL; receives the new handle, or NULL on
 *     failure (set to NULL on error).
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL out param
 *     or invalid config), MSDF_ATLAS_ERROR_OUT_OF_MEMORY.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_create(const msdf_atlas_config_t *config, msdf_atlas_packer_t **out_packer);

/**
 * Destroys a packer and frees all resources it owns. Does not modify the
 * glyphset it packed. The handle must not be used afterwards. NULL is a
 * no-op.
 * @return MSDF_ATLAS_OK.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_destroy(msdf_atlas_packer_t *packer);

/**
 * Re-applies the packing-related fields of config (style, dimensions,
 * constraints, scale, ranges, spacing, padding, grid knobs, flags) to an
 * existing packer. Fields set to their "auto" values unset the
 * corresponding knob. Applies only to the packing style already selected at
 * create time: changing packing_style is MSDF_ATLAS_ERROR_INVALID_STATE.
 * @param packer Required, non-NULL.
 * @param config Required, non-NULL; validated.
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL input or
 *     invalid config), MSDF_ATLAS_ERROR_INVALID_STATE (style change).
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_apply_config(msdf_atlas_packer_t *packer, const msdf_atlas_config_t *config);

/**
 * Fixes the atlas dimensions to width x height pixels.
 * @param packer Required, non-NULL.
 * @param width, height Must both be > 0.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL packer
 *     or non-positive dimension).
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_set_dimensions(msdf_atlas_packer_t *packer, int32_t width, int32_t height);

/**
 * Lets the packer choose the atlas dimensions during pack (subject to the
 * dimensions constraint).
 * @param packer Required, non-NULL.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT if packer is
 *     NULL.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_unset_dimensions(msdf_atlas_packer_t *packer);

/**
 * Sets the constraint applied to auto-computed atlas dimensions.
 * @param packer Required, non-NULL.
 * @param constraint Must be a valid msdf_atlas_dimensions_constraint_t value.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL packer
 *     or invalid constraint).
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_set_dimensions_constraint(msdf_atlas_packer_t *packer, msdf_atlas_dimensions_constraint_t constraint);

/**
 * Sets the spacing between glyph boxes in pixels.
 * @param packer Required, non-NULL.
 * @param spacing Must be >= 0.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL packer
 *     or negative spacing).
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_set_spacing(msdf_atlas_packer_t *packer, int32_t spacing);

/**
 * Fixes the glyph scale in ems.
 * @param packer Required, non-NULL.
 * @param scale Must be > 0.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL packer
 *     or scale <= 0).
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_set_scale(msdf_atlas_packer_t *packer, double scale);

/**
 * Sets the minimum accepted glyph scale used when auto-maximizing.
 * @param packer Required, non-NULL.
 * @param min_scale Must be >= 0; 0 removes the minimum.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL packer
 *     or negative min_scale).
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_set_minimum_scale(msdf_atlas_packer_t *packer, double min_scale);

/**
 * Sets the distance range component in font units.
 * @param packer Required, non-NULL.
 * @param range Must satisfy 0 <= range.lower <= range.upper; {0, 0} unsets
 *     the unit component.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL packer
 *     or malformed range).
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_set_unit_range(msdf_atlas_packer_t *packer, msdf_atlas_range_t range);

/**
 * Sets the distance range component in pixels.
 * @param packer Required, non-NULL.
 * @param range Must satisfy 0 <= range.lower <= range.upper; {0, 0} unsets
 *     the pixel component.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL packer
 *     or malformed range).
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_set_pixel_range(msdf_atlas_packer_t *packer, msdf_atlas_range_t range);

/**
 * Sets the miter limit used for glyph bounds computation.
 * @param packer Required, non-NULL.
 * @param miter_limit Must be > 0.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL packer
 *     or miter_limit <= 0).
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_set_miter_limit(msdf_atlas_packer_t *packer, double miter_limit);

/**
 * Sets one or more padding components. Each pointer argument is optional:
 * pass NULL to leave that component unchanged; passing NULL for all four is
 * MSDF_ATLAS_ERROR_INVALID_ARGUMENT.
 * @param packer Required, non-NULL.
 * @param inner_px Optional; inner padding in pixels (part of the quad).
 * @param outer_px Optional; outer padding in pixels (around the quad).
 * @param inner_unit Optional; inner padding in font units.
 * @param outer_unit Optional; outer padding in font units.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL packer
 *     or all padding pointers NULL).
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_set_padding(msdf_atlas_packer_t *packer, const msdf_atlas_padding_t *inner_px, const msdf_atlas_padding_t *outer_px, const msdf_atlas_padding_t *inner_unit, const msdf_atlas_padding_t *outer_unit);

/**
 * Grid packing only: fixes the column count.
 * @param packer Required, non-NULL; must be a grid packer
 *     (MSDF_ATLAS_ERROR_INVALID_STATE otherwise).
 * @param columns Must be > 0.
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL packer or
 *     columns <= 0), MSDF_ATLAS_ERROR_INVALID_STATE (tight packer).
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_set_columns(msdf_atlas_packer_t *packer, int32_t columns);

/**
 * Grid packing only: lets the packer choose the column count.
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL packer),
 *     MSDF_ATLAS_ERROR_INVALID_STATE (tight packer).
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_unset_columns(msdf_atlas_packer_t *packer);

/**
 * Grid packing only: fixes the row count. Errors as in
 * msdf_atlas_packer_set_columns().
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_set_rows(msdf_atlas_packer_t *packer, int32_t rows);

/**
 * Grid packing only: lets the packer choose the row count. Errors as in
 * msdf_atlas_packer_unset_columns().
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_unset_rows(msdf_atlas_packer_t *packer);

/**
 * Grid packing only: fixes the cell dimensions in pixels.
 * @param packer Required, non-NULL; must be a grid packer.
 * @param width, height Must both be > 0.
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL packer or
 *     non-positive dimension), MSDF_ATLAS_ERROR_INVALID_STATE (tight packer).
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_set_cell_dimensions(msdf_atlas_packer_t *packer, int32_t width, int32_t height);

/**
 * Grid packing only: lets the packer choose the cell dimensions. Errors as
 * in msdf_atlas_packer_unset_columns().
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_unset_cell_dimensions(msdf_atlas_packer_t *packer);

/**
 * Grid packing only: fixes the origin position within each cell, per axis.
 * The fixed positions themselves come from config.fixed_origin_x/y (see
 * MSDF_ATLAS_CONFIG_GRID_FIXED_ORIGIN_X/Y).
 * @param packer Required, non-NULL; must be a grid packer.
 * @param horizontal, vertical Whether to fix the X resp. Y origin.
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL packer),
 *     MSDF_ATLAS_ERROR_INVALID_STATE (tight packer).
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_set_fixed_origin(msdf_atlas_packer_t *packer, bool horizontal, bool vertical);

/**
 * Computes the atlas layout for the glyphs in set and writes the placement
 * back into set (glyph atlas bounds become valid). The packer keeps no
 * reference to set afterwards.
 * @param packer Required, non-NULL.
 * @param set Required, non-NULL; must contain at least one glyph.
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL input),
 *     MSDF_ATLAS_ERROR_INVALID_STATE (empty glyphset), MSDF_ATLAS_ERROR_PACK
 *     (glyphs do not fit; a detail message is recorded).
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_pack(msdf_atlas_packer_t *packer, msdf_atlas_glyphset_t *set);

/**
 * Returns the atlas dimensions chosen by the last successful pack (zero
 * before the first pack).
 * @param packer Required, non-NULL.
 * @param out_width, out_height Required, non-NULL.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT on NULL input.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_get_dimensions(const msdf_atlas_packer_t *packer, int32_t *out_width, int32_t *out_height);

/**
 * Returns the glyph scale used by the last successful pack.
 * @param packer Required, non-NULL.
 * @param out_scale Required, non-NULL.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT on NULL input.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_get_scale(const msdf_atlas_packer_t *packer, double *out_scale);

/**
 * Returns the final combined pixel range (pixel component plus converted
 * unit component) used by the last successful pack.
 * @param packer Required, non-NULL.
 * @param out_range Required, non-NULL.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT on NULL input.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_get_pixel_range(const msdf_atlas_packer_t *packer, msdf_atlas_range_t *out_range);

/**
 * Grid packing only: returns the cell dimensions used by the last successful
 * pack (zero before the first pack).
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL input),
 *     MSDF_ATLAS_ERROR_INVALID_STATE (tight packer).
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_get_cell_dimensions(const msdf_atlas_packer_t *packer, int32_t *out_width, int32_t *out_height);

/**
 * Grid packing only: returns the column count used by the last successful
 * pack. Errors as in msdf_atlas_packer_get_cell_dimensions().
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_get_columns(const msdf_atlas_packer_t *packer, int32_t *out_columns);

/**
 * Grid packing only: returns the row count used by the last successful pack.
 * Errors as in msdf_atlas_packer_get_cell_dimensions().
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_get_rows(const msdf_atlas_packer_t *packer, int32_t *out_rows);

/**
 * Grid packing only: returns the fixed origin position within each cell.
 * Each output is valid only if the corresponding axis was fixed.
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL input),
 *     MSDF_ATLAS_ERROR_INVALID_STATE (tight packer).
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_get_fixed_origin(const msdf_atlas_packer_t *packer, double *out_x, double *out_y);

/**
 * Grid packing only: reports whether explicitly constrained cell dimensions
 * are too small to fit every glyph fully (glyphs are cut off).
 * @param packer Required, non-NULL; must be a grid packer.
 * @param out_cutoff Required, non-NULL; receives 1 if glyphs are cut off.
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL input),
 *     MSDF_ATLAS_ERROR_INVALID_STATE (tight packer).
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_packer_has_cutoff(const msdf_atlas_packer_t *packer, bool *out_cutoff);

/* ------------------------------------------------------------------ */
/* Generator                                                           */
/* ------------------------------------------------------------------ */

/**
 * Creates an atlas generator configured by config: image_type selects the
 * distance-field variant and default pixel format, pixel_format overrides
 * the component type, flags carry error-correction/overlap/scanline knobs,
 * thread_count sets the worker pool. config may be NULL for defaults. The
 * caller owns the handle and must release it with
 * msdf_atlas_generator_destroy(). The generator keeps no reference to
 * config; the atlas size is set by msdf_atlas_generator_resize() or by
 * generate-time hints from the glyphset (see msdf_atlas_generator_generate).
 * @param config Optional, may be NULL; validated when non-NULL.
 * @param out_generator Required, non-NULL; receives the new handle, or NULL
 *     on failure (set to NULL on error).
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL out param
 *     or invalid config), MSDF_ATLAS_ERROR_UNSUPPORTED (pixel format / image
 *     type mismatch not expressible), MSDF_ATLAS_ERROR_OUT_OF_MEMORY.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_generator_create(const msdf_atlas_config_t *config, msdf_atlas_generator_t **out_generator);

/**
 * Destroys a generator and frees all resources it owns, including the bitmap
 * returned by msdf_atlas_generator_get_bitmap() and the layout returned by
 * msdf_atlas_generator_get_layout_all(). Pointers obtained from this
 * generator must not be used afterwards. NULL is a no-op.
 * @return MSDF_ATLAS_OK.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_generator_destroy(msdf_atlas_generator_t *generator);

/**
 * Sets the number of worker threads used by the next generate call.
 * @param generator Required, non-NULL.
 * @param thread_count > 0 fixes the count; <= 0 restores the automatic
 *     default.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT if generator
 *     is NULL.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_generator_set_thread_count(msdf_atlas_generator_t *generator, int32_t thread_count);

/**
 * Updates generator behavior flags after creation. Only the generator-
 * related flags are accepted (MSDF_ATLAS_CONFIG_ERROR_CORRECTION,
 * MSDF_ATLAS_CONFIG_OVERLAP_SUPPORT, MSDF_ATLAS_CONFIG_SCANLINE_PASS);
 * other bits are ignored.
 * @param generator Required, non-NULL.
 * @param flags New flag combination (0 clears the generator flags).
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT if generator
 *     is NULL.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_generator_set_flags(msdf_atlas_generator_t *generator, uint32_t flags);

/**
 * Resizes the atlas (and its bitmap) to width x height pixels, keeping
 * already generated pixels in place where possible. Invalidates the bitmap
 * pointer previously returned by msdf_atlas_generator_get_bitmap().
 * @param generator Required, non-NULL.
 * @param width, height Must both be > 0.
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL generator
 *     or non-positive dimension), MSDF_ATLAS_ERROR_OUT_OF_MEMORY.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_generator_resize(msdf_atlas_generator_t *generator, int32_t width, int32_t height);

/**
 * Generates the atlas bitmap for the glyphs in set (synchronous; returns
 * when all worker threads finished). The generator's current size is used;
 * if it is 0x0 and set has a valid packed layout, the packer dimensions are
 * applied first. The glyphs' packed placement (atlas bounds) must be valid,
 * i.e. set must have been packed with msdf_atlas_packer_pack().
 * Invalidates the previous bitmap and layout pointers.
 * @param generator Required, non-NULL.
 * @param set Required, non-NULL; must contain at least one glyph with valid
 *     atlas placement.
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL input),
 *     MSDF_ATLAS_ERROR_INVALID_STATE (empty set or missing placement),
 *     MSDF_ATLAS_ERROR_GENERATE (generation failed; a detail message is
 *     recorded), MSDF_ATLAS_ERROR_OUT_OF_MEMORY.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_generator_generate(msdf_atlas_generator_t *generator, const msdf_atlas_glyphset_t *set);

/**
 * Returns the generated atlas bitmap. Ownership: out_bitmap->pixels is owned
 * by the generator; the caller must not free it, and it stays valid until
 * the next msdf_atlas_generator_generate() / resize() / destroy(). Before
 * the first successful generate, pixels is NULL, width/height/channel_count
 * are 0 and pixel_format is MSDF_ATLAS_PIXEL_UNKNOWN.
 * @param generator Required, non-NULL.
 * @param out_bitmap Required, non-NULL.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT on NULL input.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_generator_get_bitmap(const msdf_atlas_generator_t *generator, msdf_atlas_bitmap_t *out_bitmap);

/**
 * Returns the number of glyphs in the generator's layout array (equals the
 * glyph count of the last generated glyphset).
 * @param generator Required, non-NULL.
 * @param out_count Required, non-NULL.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT on NULL input.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_generator_get_layout_count(const msdf_atlas_generator_t *generator, size_t *out_count);

/**
 * Returns one entry of the generator's layout array.
 * @param generator Required, non-NULL.
 * @param index Entry index in [0, layout count).
 * @param out_layout Required, non-NULL.
 * @return MSDF_ATLAS_OK, MSDF_ATLAS_ERROR_INVALID_ARGUMENT (NULL input),
 *     MSDF_ATLAS_ERROR_OUT_OF_RANGE (index out of range).
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_generator_get_layout(const msdf_atlas_generator_t *generator, size_t index, msdf_atlas_glyph_layout_t *out_layout);

/**
 * Returns the generator's whole layout array. Ownership: *out_layouts points
 * into memory owned by the generator; the caller must not free it, and it is
 * invalidated by the next msdf_atlas_generator_generate() / resize() /
 * destroy(). Before the first successful generate, *out_layouts is NULL and
 * *out_count is 0.
 * @param generator Required, non-NULL.
 * @param out_layouts Required, non-NULL; receives the array pointer.
 * @param out_count Required, non-NULL; receives the element count.
 * @return MSDF_ATLAS_OK, or MSDF_ATLAS_ERROR_INVALID_ARGUMENT on NULL input.
 */
MSDF_ATLAS_C_API msdf_atlas_error_t msdf_atlas_generator_get_layout_all(const msdf_atlas_generator_t *generator, const msdf_atlas_glyph_layout_t **out_layouts, size_t *out_count);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MSDF_ATLAS_C_H */
