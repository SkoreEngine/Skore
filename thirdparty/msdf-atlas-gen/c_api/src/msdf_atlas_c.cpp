/*
 * msdf_atlas_c.cpp — C API wrapper over msdf-atlas-gen (APX-220)
 * ==============================================================
 *
 * Implements the surface declared in <msdf_atlas_c.h> on top of the
 * vendored msdf-atlas-gen 1.3.0 / msdfgen 1.12.1 static libraries.
 *
 * Mapping notes (deviations from the header that are forced by the
 * underlying libraries are documented inline):
 *
 * - font           -> msdfgen::FreetypeHandle + msdfgen::FontHandle
 *                     (one FreeType library instance per font handle).
 * - charset        -> msdf_atlas::Charset. The vendored tree has no
 *                     Charset::parse implementation (charset-parser.cpp is
 *                     a CLI-only TU that was trimmed), so the wrapper
 *                     implements the charset grammar itself (upstream
 *                     syntax plus the forms documented by the header).
 * - glyphset       -> msdf_atlas::FontGeometry; every load starts from a
 *                     fresh FontGeometry and move-assigns it in so a load
 *                     *replaces* the previously loaded glyphs.
 * - packer         -> msdf_atlas::TightAtlasPacker / GridAtlasPacker.
 *                     GridAtlasPacker.cpp was restored into the vendored
 *                     build for this API (it had been trimmed as CLI-only).
 *                     config.fixed_origin_x/y cannot be fed into the grid
 *                     packer (it computes the fixed origin itself during
 *                     pack); only the fixed-origin *flags* are applied and
 *                     msdf_atlas_packer_get_fixed_origin() reports the
 *                     computed origin, as the header's getter documents.
 * - generator      -> msdf_atlas::ImmediateAtlasGenerator<T, N, FN,
 *                     BitmapAtlasStorage<T, N>>, type-erased behind an
 *                     internal interface. The glyph layout snapshot is
 *                     built by the wrapper (GlyphBox lacks codepoint /
 *                     whitespace flags) and, when the config's y_direction
 *                     is TOP_DOWN, the storage rows are flipped in place
 *                     and the layout's atlas Y coordinates are flipped to
 *                     match the produced bitmap.
 * - error handling -> every entry point runs inside guard(); no C++
 *                     exception ever crosses the boundary. The last error
 *                     detail is recorded in a thread-local string.
 */

#include "msdf_atlas_c.h"

#include <msdf-atlas-gen.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

/* ------------------------------------------------------------------ */
/* Error recording (thread-local)                                      */
/* ------------------------------------------------------------------ */

thread_local std::string g_lastError;

void clearError() {
    g_lastError.clear();
}

msdf_atlas_error_t recordError(msdf_atlas_error_t code, const char *message) {
    try {
        g_lastError = message ? message : "";
    } catch (...) {
        // Never let message recording itself throw across the boundary.
    }
    return code;
}

/** Runs fn and maps any C++ exception to an error code + message. */
template <typename FN>
msdf_atlas_error_t guard(FN &&fn) {
    try {
        return fn();
    } catch (const std::bad_alloc &) {
        return recordError(MSDF_ATLAS_ERROR_OUT_OF_MEMORY, "out of memory");
    } catch (const std::exception &e) {
        return recordError(MSDF_ATLAS_ERROR_INVALID_STATE, e.what());
    } catch (...) {
        return recordError(MSDF_ATLAS_ERROR_INVALID_STATE, "unknown C++ exception");
    }
}

} // namespace (error helpers)

/* ------------------------------------------------------------------ */
/* Handle definitions (global scope: they complete the header's        */
/* opaque structs)                                                     */
/* ------------------------------------------------------------------ */

struct msdf_atlas_font {
    msdfgen::FreetypeHandle *ft;
    msdfgen::FontHandle *handle;
};

struct msdf_atlas_charset {
    msdf_atlas::Charset charset;
};

struct msdf_atlas_glyphset {
    msdf_atlas::FontGeometry geometry;
    int packW = 0; // Atlas dimensions chosen by the last successful pack.
    int packH = 0;
    bool kerningLoaded = false;
};

struct msdf_atlas_packer {
    bool grid = false;
    msdf_atlas::TightAtlasPacker tight;
    msdf_atlas::GridAtlasPacker gridP;
    // Results of the last successful pack (zero before the first pack).
    bool packed = false;
    int dimW = 0, dimH = 0;
    double scale = 0.0;
    msdfgen::Range pxRange{ 0.0, 0.0 };
    int cellW = 0, cellH = 0;
    int columns = 0, rows = 0;
    double fixedX = 0.0, fixedY = 0.0;
    bool cutoff = false;
};

/** Type-erased generator backend (one per T/N/generator-function combo). */
class GeneratorBase {
public:
    virtual ~GeneratorBase() = default;
    virtual void setAttributes(const msdf_atlas::GeneratorAttributes &attributes) = 0;
    virtual void setThreadCount(int threadCount) = 0;
    virtual void resize(int width, int height) = 0;
    virtual void generate(const msdf_atlas::GlyphGeometry *glyphs, int count) = 0;
    virtual void flipRows() = 0;
    virtual const void *pixels() const = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual size_t componentSize() const = 0;
    virtual int channels() const = 0;
};

template <typename STORAGE, int N, msdf_atlas::GeneratorFunction<float, N> FN>
class GeneratorImpl final : public GeneratorBase {
public:
    void setAttributes(const msdf_atlas::GeneratorAttributes &attributes) override {
        generator.setAttributes(attributes);
    }
    void setThreadCount(int threadCount) override {
        generator.setThreadCount(threadCount);
    }
    void resize(int width, int height) override {
        generator.resize(width, height);
    }
    void generate(const msdf_atlas::GlyphGeometry *glyphs, int count) override {
        generator.generate(glyphs, count);
    }
    void flipRows() override {
        // atlasStorage() is const-qualified upstream; the bitmap it wraps is
        // mutable, so the cast only removes const from the accessor result.
        auto &storage = const_cast<msdf_atlas::BitmapAtlasStorage<STORAGE, N> &>(generator.atlasStorage());
        msdfgen::BitmapRef<STORAGE, N> bitmap = storage;
        if (!bitmap.pixels || bitmap.height < 2)
            return;
        const size_t rowBytes = sizeof(STORAGE) * N * (size_t) bitmap.width;
        std::vector<STORAGE> tmp((size_t) bitmap.width * N);
        for (int y = 0; y < bitmap.height / 2; ++y) {
            STORAGE *top = bitmap(0, y);
            STORAGE *bottom = bitmap(0, bitmap.height - 1 - y);
            memcpy(tmp.data(), top, rowBytes);
            memcpy(top, bottom, rowBytes);
            memcpy(bottom, tmp.data(), rowBytes);
        }
    }
    const void *pixels() const override {
        msdfgen::BitmapConstRef<STORAGE, N> bitmap = generator.atlasStorage();
        return bitmap.pixels;
    }
    int width() const override {
        msdfgen::BitmapConstRef<STORAGE, N> bitmap = generator.atlasStorage();
        return bitmap.width;
    }
    int height() const override {
        msdfgen::BitmapConstRef<STORAGE, N> bitmap = generator.atlasStorage();
        return bitmap.height;
    }
    size_t componentSize() const override {
        return sizeof(STORAGE);
    }
    int channels() const override {
        return N;
    }

private:
    msdf_atlas::ImmediateAtlasGenerator<float, N, FN, msdf_atlas::BitmapAtlasStorage<STORAGE, N>> generator;
};

struct msdf_atlas_generator {
    std::unique_ptr<GeneratorBase> impl;
    std::vector<msdf_atlas_glyph_layout_t> layouts; // Snapshot of the last generated glyphset.
    msdf_atlas_pixel_format_t pixelFormat = MSDF_ATLAS_PIXEL_UNKNOWN;
    msdf_atlas_y_direction_t yDirection = MSDF_ATLAS_Y_BOTTOM_UP;
    bool generated = false;
    // Generator knobs (kept so set_flags can re-derive attributes).
    uint32_t flags = 0;
    msdf_atlas_error_correction_mode_t errorCorrectionMode = MSDF_ATLAS_ERROR_CORRECTION_EDGE_PRIORITY;
    msdf_atlas_error_correction_check_t errorCorrectionCheck = MSDF_ATLAS_ERROR_CHECK_AT_EDGE;
};

namespace {

msdf_atlas::ImageType toImageType(msdf_atlas_image_type_t type) {
    return (msdf_atlas::ImageType) type; // Enum values match 1:1.
}

msdf_atlas::DimensionsConstraint toConstraint(msdf_atlas_dimensions_constraint_t constraint) {
    return (msdf_atlas::DimensionsConstraint) constraint; // Enum values match 1:1.
}

msdfgen::Range toRange(msdf_atlas_range_t range) {
    return msdfgen::Range(range.lower, range.upper);
}

msdf_atlas::Padding toPadding(msdf_atlas_padding_t padding) {
    return msdf_atlas::Padding(padding.l, padding.b, padding.r, padding.t);
}

msdfgen::ErrorCorrectionConfig::Mode toErrorCorrectionMode(msdf_atlas_error_correction_mode_t mode) {
    switch (mode) {
        case MSDF_ATLAS_ERROR_CORRECTION_DISABLED:
            return msdfgen::ErrorCorrectionConfig::DISABLED;
        case MSDF_ATLAS_ERROR_CORRECTION_INDISCRIMINATE:
            return msdfgen::ErrorCorrectionConfig::INDISCRIMINATE;
        case MSDF_ATLAS_ERROR_CORRECTION_EDGE_PRIORITY:
            return msdfgen::ErrorCorrectionConfig::EDGE_PRIORITY;
        case MSDF_ATLAS_ERROR_CORRECTION_EDGE_ONLY:
        default:
            return msdfgen::ErrorCorrectionConfig::EDGE_ONLY;
    }
}

msdfgen::ErrorCorrectionConfig::DistanceCheckMode toDistanceCheckMode(msdf_atlas_error_correction_check_t check) {
    switch (check) {
        case MSDF_ATLAS_ERROR_CHECK_DO_NOT_CHECK:
            return msdfgen::ErrorCorrectionConfig::DO_NOT_CHECK_DISTANCE;
        case MSDF_ATLAS_ERROR_CHECK_ALWAYS:
            return msdfgen::ErrorCorrectionConfig::ALWAYS_CHECK_DISTANCE;
        case MSDF_ATLAS_ERROR_CHECK_AT_EDGE:
        default:
            return msdfgen::ErrorCorrectionConfig::CHECK_DISTANCE_AT_EDGE;
    }
}

/** Channel count implied by an image type (1, 3 or 4). */
int imageTypeChannels(msdf_atlas_image_type_t imageType) {
    switch (imageType) {
        case MSDF_ATLAS_IMAGE_MSDF:
            return 3;
        case MSDF_ATLAS_IMAGE_MTSDF:
            return 4;
        default:
            return 1;
    }
}

/* ------------------------------------------------------------------ */
/* Config validation                                                   */
/* ------------------------------------------------------------------ */

msdf_atlas_error_t validateConfig(const msdf_atlas_config_t *config) {
    if (!config)
        return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "config is NULL");
    if (config->struct_size != sizeof(msdf_atlas_config_t))
        return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "config struct_size does not match the header's layout");
    if (config->image_type < 0 || config->image_type > MSDF_ATLAS_IMAGE_MTSDF)
        return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "config image_type is out of range");
    if (config->pixel_format < 0 || config->pixel_format > MSDF_ATLAS_PIXEL_RGBA32F)
        return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "config pixel_format is out of range");
    if (config->y_direction != MSDF_ATLAS_Y_BOTTOM_UP && config->y_direction != MSDF_ATLAS_Y_TOP_DOWN)
        return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "config y_direction is out of range");
    if (config->packing_style != MSDF_ATLAS_PACKING_TIGHT && config->packing_style != MSDF_ATLAS_PACKING_GRID)
        return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "config packing_style is out of range");
    if (config->dimensions_constraint < 0 || config->dimensions_constraint > MSDF_ATLAS_DIMENSIONS_POWER_OF_TWO_SQUARE)
        return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "config dimensions_constraint is out of range");
    if (config->error_correction_mode < 0 || config->error_correction_mode > MSDF_ATLAS_ERROR_CORRECTION_EDGE_ONLY)
        return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "config error_correction_mode is out of range");
    if (config->error_correction_check < 0 || config->error_correction_check > MSDF_ATLAS_ERROR_CHECK_ALWAYS)
        return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "config error_correction_check is out of range");

    if (config->pixel_format != MSDF_ATLAS_PIXEL_UNKNOWN) {
        int expected = imageTypeChannels(config->image_type);
        int actual = 0;
        switch (config->pixel_format) {
            case MSDF_ATLAS_PIXEL_R8:
            case MSDF_ATLAS_PIXEL_R32F:
                actual = 1;
                break;
            case MSDF_ATLAS_PIXEL_RGB8:
            case MSDF_ATLAS_PIXEL_RGB32F:
                actual = 3;
                break;
            case MSDF_ATLAS_PIXEL_RGBA8:
            case MSDF_ATLAS_PIXEL_RGBA32F:
                actual = 4;
                break;
            default:
                break;
        }
        if (actual != expected)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "config pixel_format channel count does not match image_type");
    }

    if ((config->pack_width > 0) != (config->pack_height > 0))
        return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "config pack_width and pack_height must be both fixed or both unset");
    if ((config->cell_width > 0) != (config->cell_height > 0))
        return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "config cell_width and cell_height must be both fixed or both unset");
    if (config->min_glyph_scale < 0)
        return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "config min_glyph_scale is negative");
    if (config->unit_range.lower < 0 || config->unit_range.upper < config->unit_range.lower)
        return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "config unit_range is malformed");
    /* Endpoints, not a width. C++ Range(2) is {-1, +1}; lower may be negative. */
    if (config->px_range.upper < config->px_range.lower)
        return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "config px_range is malformed");
    if (config->miter_limit < 0)
        return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "config miter_limit is negative");
    if (config->spacing < 0)
        return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "config spacing is negative");
    return MSDF_ATLAS_OK;
}

/* ------------------------------------------------------------------ */
/* Packer config application                                           */
/* ------------------------------------------------------------------ */

void applyTightConfig(msdf_atlas::TightAtlasPacker &packer, const msdf_atlas_config_t &config) {
    if (config.pack_width > 0 && config.pack_height > 0)
        packer.setDimensions(config.pack_width, config.pack_height);
    else
        packer.unsetDimensions();
    packer.setDimensionsConstraint(toConstraint(config.dimensions_constraint));
    packer.setSpacing(config.spacing);
    if (config.glyph_scale > 0)
        packer.setScale(config.glyph_scale);
    // min_glyph_scale 0 means "no minimum": leave the packer's internal
    // initial-scale default (a zero minimum would make the tight packer
    // refuse auto-dimension packing).
    if (config.min_glyph_scale > 0)
        packer.setMinimumScale(config.min_glyph_scale);
    packer.setUnitRange(toRange(config.unit_range));
    packer.setPixelRange(toRange(config.px_range));
    packer.setMiterLimit(config.miter_limit);
    packer.setOriginPixelAlignment((config.flags & MSDF_ATLAS_CONFIG_PX_ALIGN_ORIGIN_X) != 0,
                                   (config.flags & MSDF_ATLAS_CONFIG_PX_ALIGN_ORIGIN_Y) != 0);
    packer.setInnerUnitPadding(toPadding(config.inner_unit_padding));
    packer.setOuterUnitPadding(toPadding(config.outer_unit_padding));
    packer.setInnerPixelPadding(toPadding(config.inner_px_padding));
    packer.setOuterPixelPadding(toPadding(config.outer_px_padding));
}

void applyGridConfig(msdf_atlas::GridAtlasPacker &packer, const msdf_atlas_config_t &config) {
    if (config.pack_width > 0 && config.pack_height > 0)
        packer.setDimensions(config.pack_width, config.pack_height);
    else
        packer.unsetDimensions();
    packer.setDimensionsConstraint(toConstraint(config.dimensions_constraint));
    packer.setSpacing(config.spacing);
    if (config.glyph_scale > 0)
        packer.setScale(config.glyph_scale);
    // The grid packer treats minScale as a hard floor; 0 removes the floor.
    packer.setMinimumScale(config.min_glyph_scale > 0 ? config.min_glyph_scale : 0);
    packer.setUnitRange(toRange(config.unit_range));
    packer.setPixelRange(toRange(config.px_range));
    packer.setMiterLimit(config.miter_limit);
    packer.setOriginPixelAlignment((config.flags & MSDF_ATLAS_CONFIG_PX_ALIGN_ORIGIN_X) != 0,
                                   (config.flags & MSDF_ATLAS_CONFIG_PX_ALIGN_ORIGIN_Y) != 0);
    packer.setInnerUnitPadding(toPadding(config.inner_unit_padding));
    packer.setOuterUnitPadding(toPadding(config.outer_unit_padding));
    packer.setInnerPixelPadding(toPadding(config.inner_px_padding));
    packer.setOuterPixelPadding(toPadding(config.outer_px_padding));
    if (config.columns > 0)
        packer.setColumns(config.columns);
    else
        packer.unsetColumns();
    if (config.rows > 0)
        packer.setRows(config.rows);
    else
        packer.unsetRows();
    if (config.cell_width > 0 && config.cell_height > 0)
        packer.setCellDimensions(config.cell_width, config.cell_height);
    else
        packer.unsetCellDimensions();
    packer.setFixedOrigin((config.flags & MSDF_ATLAS_CONFIG_GRID_FIXED_ORIGIN_X) != 0,
                          (config.flags & MSDF_ATLAS_CONFIG_GRID_FIXED_ORIGIN_Y) != 0);
}

void applyPackerConfig(msdf_atlas_packer *packer, const msdf_atlas_config_t &config) {
    if (packer->grid)
        applyGridConfig(packer->gridP, config);
    else
        applyTightConfig(packer->tight, config);
}

/* ------------------------------------------------------------------ */
/* Generator attributes                                                */
/* ------------------------------------------------------------------ */

void applyGeneratorAttributes(msdf_atlas_generator *generator) {
    msdf_atlas::GeneratorAttributes attributes;
    attributes.config.overlapSupport = (generator->flags & MSDF_ATLAS_CONFIG_OVERLAP_SUPPORT) != 0;
    attributes.scanlinePass = (generator->flags & MSDF_ATLAS_CONFIG_SCANLINE_PASS) != 0;
    attributes.config.errorCorrection.mode =
        (generator->flags & MSDF_ATLAS_CONFIG_ERROR_CORRECTION)
            ? toErrorCorrectionMode(generator->errorCorrectionMode)
            : msdfgen::ErrorCorrectionConfig::DISABLED;
    attributes.config.errorCorrection.distanceCheckMode = toDistanceCheckMode(generator->errorCorrectionCheck);
    generator->impl->setAttributes(attributes);
}

/* ------------------------------------------------------------------ */
/* Generator backend factory                                           */
/* ------------------------------------------------------------------ */

template <typename STORAGE, int N, msdf_atlas::GeneratorFunction<float, N> FN>
std::unique_ptr<GeneratorBase> makeGeneratorImpl(msdf_atlas_pixel_format_t pixelFormat) {
    switch (pixelFormat) {
        case MSDF_ATLAS_PIXEL_R32F:
        case MSDF_ATLAS_PIXEL_RGB32F:
        case MSDF_ATLAS_PIXEL_RGBA32F:
            return std::make_unique<GeneratorImpl<float, N, FN>>();
        default:
            return std::make_unique<GeneratorImpl<msdf_atlas::byte, N, FN>>();
    }
}

std::unique_ptr<GeneratorBase> makeGenerator(msdf_atlas_image_type_t imageType, msdf_atlas_pixel_format_t pixelFormat) {
    switch (imageType) {
        case MSDF_ATLAS_IMAGE_HARD_MASK:
            return makeGeneratorImpl<msdf_atlas::byte, 1, &msdf_atlas::scanlineGenerator>(pixelFormat);
        case MSDF_ATLAS_IMAGE_SOFT_MASK:
        case MSDF_ATLAS_IMAGE_SDF:
            return makeGeneratorImpl<msdf_atlas::byte, 1, &msdf_atlas::sdfGenerator>(pixelFormat);
        case MSDF_ATLAS_IMAGE_PSDF:
            return makeGeneratorImpl<msdf_atlas::byte, 1, &msdf_atlas::psdfGenerator>(pixelFormat);
        case MSDF_ATLAS_IMAGE_MSDF:
            return makeGeneratorImpl<msdf_atlas::byte, 3, &msdf_atlas::msdfGenerator>(pixelFormat);
        case MSDF_ATLAS_IMAGE_MTSDF:
        default:
            return makeGeneratorImpl<msdf_atlas::byte, 4, &msdf_atlas::mtsdfGenerator>(pixelFormat);
    }
}

/* ------------------------------------------------------------------ */
/* Glyph layout conversion                                             */
/* ------------------------------------------------------------------ */

void fillGlyphLayout(const msdf_atlas::GlyphGeometry &glyph, msdf_atlas_glyph_layout_t &out) {
    out.codepoint = glyph.getCodepoint();
    out.glyph_index = glyph.getIndex();
    out.advance = glyph.getAdvance();
    glyph.getQuadPlaneBounds(out.plane_bounds_l, out.plane_bounds_b, out.plane_bounds_r, out.plane_bounds_t);
    // getQuadPlaneBounds is only non-zero after wrapBox/frameBox (packing).
    // The C API documents plane bounds as valid right after load, so fall back
    // to the shape bounds scaled by geometryScale when the box is not set yet.
    if (!(out.plane_bounds_r > out.plane_bounds_l && out.plane_bounds_t > out.plane_bounds_b) &&
        !glyph.isWhitespace()) {
        const msdfgen::Shape::Bounds &bounds = glyph.getShapeBounds();
        const double s = glyph.getGeometryScale();
        out.plane_bounds_l = bounds.l * s;
        out.plane_bounds_b = bounds.b * s;
        out.plane_bounds_r = bounds.r * s;
        out.plane_bounds_t = bounds.t * s;
    }
    glyph.getQuadAtlasBounds(out.atlas_bounds_l, out.atlas_bounds_b, out.atlas_bounds_r, out.atlas_bounds_t);
    int x = 0, y = 0, w = 0, h = 0;
    glyph.getBoxRect(x, y, w, h);
    out.atlas_x = x;
    out.atlas_y = y;
    out.atlas_w = w;
    out.atlas_h = h;
    out.is_whitespace = glyph.isWhitespace() ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Charset parsing (upstream charset-parser.cpp grammar, see header)   */
/* ------------------------------------------------------------------ */

char escapedChar(char c) {
    switch (c) {
        case '0':
            return '\0';
        case 'n':
        case 'N':
            return '\n';
        case 'r':
        case 'R':
            return '\r';
        case 's':
        case 'S':
            return ' ';
        case 't':
        case 'T':
            return '\t';
        default:
            return c;
    }
}

/** Decodes one UTF-8 sequence; returns bytes consumed or 0 on error. */
int decodeUtf8(const char *s, const char *end, uint32_t &codepoint) {
    const unsigned char *u = (const unsigned char *) s;
    const unsigned char *e = (const unsigned char *) end;
    if (u >= e)
        return 0;
    if (u[0] < 0x80) {
        codepoint = u[0];
        return 1;
    }
    int extra;
    uint32_t value;
    if ((u[0] & 0xE0) == 0xC0) {
        extra = 1;
        value = u[0] & 0x1Fu;
    } else if ((u[0] & 0xF0) == 0xE0) {
        extra = 2;
        value = u[0] & 0x0Fu;
    } else if ((u[0] & 0xF8) == 0xF0) {
        extra = 3;
        value = u[0] & 0x07u;
    } else {
        return 0;
    }
    if ((size_t) (e - u) < (size_t) extra + 1)
        return 0;
    for (int i = 1; i <= extra; ++i) {
        if ((u[i] & 0xC0) != 0x80)
            return 0;
        value = (value << 6) | (u[i] & 0x3Fu);
    }
    // Reject overlong encodings, surrogates and out-of-range codepoints.
    if (extra == 1 && value < 0x80)
        return 0;
    if (extra == 2 && value < 0x800)
        return 0;
    if (extra == 3 && value < 0x10000)
        return 0;
    if (value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
        return 0;
    codepoint = value;
    return extra + 1;
}

/** Parses a charset specification into a temporary set (atomic add). */
bool charsetParseText(const char *text, size_t length, std::set<uint32_t> &out) {
    enum class State {
        CLEAR,
        TIGHT,
        RANGE_BRACKET,
        RANGE_START,
        RANGE_SEPARATOR,
        RANGE_END
    };

    const char *p = text;
    const char *end = text + length;
    State state = State::CLEAR;
    uint32_t rangeStart = 0;

    // Skip a leading UTF-8 byte order mark.
    if (length >= 3 && (unsigned char) p[0] == 0xEF && (unsigned char) p[1] == 0xBB && (unsigned char) p[2] == 0xBF)
        p += 3;

    auto isSpace = [](char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; };
    auto isSeparator = [](char c) { return c == ',' || c == ';'; };

    auto addCodepoint = [&out](uint32_t cp) -> bool {
        if (cp > 0x10FFFF)
            return false;
        out.insert(cp);
        return true;
    };
    auto addRange = [&out](uint32_t lo, uint32_t hi) -> bool {
        if (hi < lo || hi > 0x10FFFF)
            return false;
        for (uint32_t cp = lo; cp <= hi; ++cp)
            out.insert(cp);
        return true;
    };

    /** Consumes one entry (codepoint, quoted char, number, U+hex or bare char). */
    auto processEntry = [&](uint32_t cp) -> bool {
        switch (state) {
            case State::CLEAR:
            case State::TIGHT:
            case State::RANGE_END:
                if (!addCodepoint(cp))
                    return false;
                // Remember for a following bare dash range (e.g. a-z, U+41-U+5A).
                rangeStart = cp;
                state = State::TIGHT;
                return true;
            case State::RANGE_BRACKET:
                rangeStart = cp;
                state = State::RANGE_START;
                return true;
            // Upstream only accepts the range end after an explicit separator
            // (comma/semicolon). RANGE_START alone is not enough — so
            // "['A' 'Z']" (space only) is a syntax error while "['A', 'Z']" is
            // valid. Bare dash ranges transition into RANGE_SEPARATOR below.
            case State::RANGE_SEPARATOR:
                if (!addRange(rangeStart, cp))
                    return false;
                state = State::RANGE_END;
                return true;
            case State::RANGE_START:
                return false;
        }
        return false;
    };

    while (p < end) {
        unsigned char c = (unsigned char) *p;

        if (isSpace((char) c)) {
            if (state == State::TIGHT)
                state = State::CLEAR;
            ++p;
            continue;
        }
        if (isSeparator((char) c)) {
            if (state == State::TIGHT)
                state = State::CLEAR;
            else if (state == State::RANGE_START)
                state = State::RANGE_SEPARATOR;
            else if (state != State::CLEAR)
                return false;
            ++p;
            continue;
        }

        if (c >= '0' && c <= '9') {
            // Number (decimal or 0x hexadecimal), or a bare digit character.
            // The header documents forms like "a-z0-9", where each digit is the
            // character itself (U+0030..U+0039), not the integer codepoint 0..9.
            // Single non-hex digits are therefore treated as bare characters;
            // multi-digit decimals and 0x-prefixed hex remain numeric.
            const char *q = p;
            bool hex = false;
            if (q + 1 < end && *q == '0' && (q[1] == 'x' || q[1] == 'X')) {
                hex = true;
                q += 2;
            }
            if (!hex && (q + 1 >= end || (unsigned char) q[1] < '0' || (unsigned char) q[1] > '9')) {
                // Bare single digit character (e.g. the '0' and '9' in "a-z0-9").
                if (!processEntry((uint32_t) c))
                    return false;
                ++p;
                continue;
            }
            uint64_t value = 0;
            bool any = false;
            while (q < end) {
                unsigned char d = (unsigned char) *q;
                unsigned digit;
                if (d >= '0' && d <= '9')
                    digit = (unsigned) (d - '0');
                else if (hex && d >= 'a' && d <= 'f')
                    digit = (unsigned) (d - 'a' + 10);
                else if (hex && d >= 'A' && d <= 'F')
                    digit = (unsigned) (d - 'A' + 10);
                else
                    break;
                uint64_t base = hex ? 16u : 10u;
                if (value > (0xFFFFFFFFull - digit) / base)
                    return false;
                value = value * base + digit;
                any = true;
                ++q;
            }
            if (!any)
                return false;
            if (!processEntry((uint32_t) value))
                return false;
            p = q;
            continue;
        }

        if (c == '\'') {
            // Single UTF-8 character literal.
            const char *q = p + 1;
            uint32_t cp = 0;
            int charBytes = 0; // Bytes of the character itself (no quotes).
            if (q >= end)
                return false;
            if (*q == '\\') {
                if (q + 1 >= end)
                    return false;
                cp = (unsigned char) escapedChar(q[1]);
                charBytes = 2;
            } else {
                charBytes = decodeUtf8(q, end, cp);
                if (charBytes <= 0)
                    return false;
            }
            // Closing quote sits right after the character.
            if (p + charBytes + 1 >= end || p[charBytes + 1] != '\'')
                return false;
            if (!processEntry(cp))
                return false;
            p += charBytes + 2;
            continue;
        }

        if (c == '"') {
            // String of UTF-8 characters.
            if (state != State::CLEAR)
                return false;
            std::string buffer;
            bool escape = false;
            bool closed = false;
            for (const char *q = p + 1; q < end; ++q) {
                if (escape) {
                    buffer.push_back(escapedChar(*q));
                    escape = false;
                } else if (*q == '\\') {
                    escape = true;
                } else if (*q == '"') {
                    closed = true;
                    p = q + 1;
                    break;
                } else {
                    buffer.push_back(*q);
                }
            }
            if (!closed || escape)
                return false;
            const char *s = buffer.data();
            size_t remaining = buffer.size();
            while (remaining > 0) {
                uint32_t cp = 0;
                int n = decodeUtf8(s, s + remaining, cp);
                if (n <= 0)
                    return false;
                if (!addCodepoint(cp))
                    return false;
                s += n;
                remaining -= (size_t) n;
            }
            state = State::TIGHT;
            continue;
        }

        if (c == '[') {
            if (state != State::CLEAR)
                return false;
            state = State::RANGE_BRACKET;
            ++p;
            continue;
        }

        if (c == ']') {
            if (state != State::RANGE_END)
                return false;
            state = State::TIGHT;
            ++p;
            continue;
        }

        if ((c == 'U' || c == 'u') && p + 1 < end && p[1] == '+') {
            // U+XXXX hexadecimal codepoint.
            const char *q = p + 2;
            uint64_t value = 0;
            bool any = false;
            while (q < end) {
                unsigned char d = (unsigned char) *q;
                unsigned digit;
                if (d >= '0' && d <= '9')
                    digit = (unsigned) (d - '0');
                else if (d >= 'a' && d <= 'f')
                    digit = (unsigned) (d - 'a' + 10);
                else if (d >= 'A' && d <= 'F')
                    digit = (unsigned) (d - 'A' + 10);
                else
                    break;
                if (value > (0xFFFFFFFFull - digit) / 16)
                    return false;
                value = value * 16 + digit;
                any = true;
                ++q;
            }
            if (!any)
                return false;
            if (!processEntry((uint32_t) value))
                return false;
            p = q;
            continue;
        }

        if (c == '-') {
            // A dash between two bare entries opens a range (header extension
            // beyond upstream's bracketed form). rangeStart was captured when
            // the previous entry was added. Transition to RANGE_SEPARATOR so
            // the next entry is accepted as the range end (same as after a
            // comma inside brackets).
            if (state == State::TIGHT) {
                state = State::RANGE_SEPARATOR;
                ++p;
                continue;
            }
            if (!processEntry(0x2D))
                return false;
            ++p;
            continue;
        }

        if (c == '@')
            return false; // @include is a file-only directive.

        // Bare UTF-8 character literal (any other byte sequence).
        uint32_t cp = 0;
        int n = decodeUtf8(p, end, cp);
        if (n <= 0)
            return false;
        if (!processEntry(cp))
            return false;
        p += n;
    }

    return state == State::CLEAR || state == State::TIGHT || state == State::RANGE_END;
}

} // anonymous namespace

/* ================================================================== */
/* Version and error reporting                                         */
/* ================================================================== */

extern "C" const char *msdf_atlas_version(void) {
    clearError();
    return "1.3.0";
}

extern "C" const char *msdf_atlas_error_string(msdf_atlas_error_t error) {
    clearError();
    switch (error) {
        case MSDF_ATLAS_OK:
            return "success";
        case MSDF_ATLAS_ERROR_INVALID_ARGUMENT:
            return "invalid argument";
        case MSDF_ATLAS_ERROR_OUT_OF_MEMORY:
            return "out of memory";
        case MSDF_ATLAS_ERROR_OUT_OF_RANGE:
            return "index or identifier out of range";
        case MSDF_ATLAS_ERROR_INVALID_STATE:
            return "invalid state";
        case MSDF_ATLAS_ERROR_UNSUPPORTED:
            return "unsupported combination";
        case MSDF_ATLAS_ERROR_IO:
            return "I/O failure";
        case MSDF_ATLAS_ERROR_FONT_LOAD:
            return "font could not be loaded";
        case MSDF_ATLAS_ERROR_GLYPH_LOAD:
            return "glyph geometry could not be loaded";
        case MSDF_ATLAS_ERROR_CHARSET_PARSE:
            return "charset syntax error";
        case MSDF_ATLAS_ERROR_PACK:
            return "atlas layout computation failed";
        case MSDF_ATLAS_ERROR_GENERATE:
            return "bitmap generation failed";
        case MSDF_ATLAS_ERROR_COUNT:
            break;
    }
    return "unknown error";
}

extern "C" const char *msdf_atlas_last_error_message(void) {
    return g_lastError.empty() ? NULL : g_lastError.c_str();
}

/* ================================================================== */
/* Configuration                                                       */
/* ================================================================== */

extern "C" msdf_atlas_error_t msdf_atlas_config_default(msdf_atlas_config_t *out_config) {
    return guard([&]() {
        clearError();
        if (!out_config)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "out_config is NULL");
        msdf_atlas_config_t config = { };
        config.struct_size = (uint32_t) sizeof(msdf_atlas_config_t);
        config.image_type = MSDF_ATLAS_IMAGE_MSDF;
        config.pixel_format = MSDF_ATLAS_PIXEL_UNKNOWN;
        config.y_direction = MSDF_ATLAS_Y_BOTTOM_UP;
        config.packing_style = MSDF_ATLAS_PACKING_TIGHT;
        config.dimensions_constraint = MSDF_ATLAS_DIMENSIONS_NONE;
        config.px_range.lower = 2.0;
        config.px_range.upper = 2.0;
        config.miter_limit = 1.0;
        config.error_correction_mode = MSDF_ATLAS_ERROR_CORRECTION_EDGE_PRIORITY;
        config.error_correction_check = MSDF_ATLAS_ERROR_CHECK_AT_EDGE;
        *out_config = config;
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_config_validate(const msdf_atlas_config_t *config) {
    return guard([&]() {
        clearError();
        return validateConfig(config);
    });
}

/* ================================================================== */
/* Charset                                                             */
/* ================================================================== */

extern "C" msdf_atlas_error_t msdf_atlas_charset_create(msdf_atlas_charset_t **out_charset) {
    return guard([&]() {
        clearError();
        if (!out_charset)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "out_charset is NULL");
        *out_charset = NULL;
        *out_charset = new msdf_atlas_charset_t();
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_charset_create_ascii(msdf_atlas_charset_t **out_charset) {
    return guard([&]() {
        clearError();
        if (!out_charset)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "out_charset is NULL");
        *out_charset = NULL;
        msdf_atlas_charset_t *charset = new msdf_atlas_charset_t();
        charset->charset = msdf_atlas::Charset::ASCII;
        *out_charset = charset;
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_charset_destroy(msdf_atlas_charset_t *charset) {
    return guard([&]() {
        clearError();
        delete charset;
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_charset_add(msdf_atlas_charset_t *charset, uint32_t codepoint) {
    return guard([&]() {
        clearError();
        if (!charset)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "charset is NULL");
        charset->charset.add(codepoint);
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_charset_remove(msdf_atlas_charset_t *charset, uint32_t codepoint) {
    return guard([&]() {
        clearError();
        if (!charset)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "charset is NULL");
        charset->charset.remove(codepoint);
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_charset_contains(const msdf_atlas_charset_t *charset, uint32_t codepoint, bool *out_contains) {
    return guard([&]() {
        clearError();
        if (!charset || !out_contains)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "charset or out_contains is NULL");
        *out_contains = std::find(charset->charset.begin(), charset->charset.end(), (msdf_atlas::unicode_t) codepoint) != charset->charset.end();
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_charset_size(const msdf_atlas_charset_t *charset, size_t *out_size) {
    return guard([&]() {
        clearError();
        if (!charset || !out_size)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "charset or out_size is NULL");
        *out_size = charset->charset.size();
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_charset_parse(msdf_atlas_charset_t *charset, const char *text, size_t text_length) {
    return guard([&]() {
        clearError();
        if (!charset || !text)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "charset or text is NULL");
        if (text_length == SIZE_MAX)
            text_length = strlen(text);
        std::set<uint32_t> parsed;
        if (!charsetParseText(text, text_length, parsed))
            return recordError(MSDF_ATLAS_ERROR_CHARSET_PARSE, "charset specification is syntactically invalid");
        for (uint32_t cp : parsed)
            charset->charset.add(cp); // Atomic: charset unchanged on parse failure.
        return MSDF_ATLAS_OK;
    });
}

/* ================================================================== */
/* Font                                                                */
/* ================================================================== */

extern "C" msdf_atlas_error_t msdf_atlas_font_open(const char *path, msdf_atlas_font_t **out_font) {
    return guard([&]() {
        clearError();
        if (!path)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "path is NULL");
        if (!out_font)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "out_font is NULL");
        *out_font = NULL;
        msdf_atlas_font_t *font = new msdf_atlas_font_t();
        font->ft = NULL;
        font->handle = NULL;
        font->ft = msdfgen::initializeFreetype();
        if (!font->ft) {
            delete font;
            return recordError(MSDF_ATLAS_ERROR_FONT_LOAD, "failed to initialize FreeType");
        }
        font->handle = msdfgen::loadFont(font->ft, path);
        if (!font->handle) {
            // Distinguish unreadable files from unparsable font data.
            FILE *probe = fopen(path, "rb");
            if (!probe) {
                delete font;
                return recordError(MSDF_ATLAS_ERROR_IO, "cannot open font file");
            }
            fclose(probe);
            delete font;
            return recordError(MSDF_ATLAS_ERROR_FONT_LOAD, "font file could not be parsed");
        }
        *out_font = font;
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_font_open_memory(const void *data, size_t data_size, msdf_atlas_font_t **out_font) {
    return guard([&]() {
        clearError();
        if (!out_font)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "out_font is NULL");
        *out_font = NULL; // Always NULL on failure, whatever the error path.
        if (!data)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "data is NULL");
        if (data_size == 0)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "data_size is zero");
        if (data_size > (size_t) std::numeric_limits<int>::max())
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "data_size exceeds the supported range");
        msdf_atlas_font_t *font = new msdf_atlas_font_t();
        font->ft = NULL;
        font->handle = NULL;
        font->ft = msdfgen::initializeFreetype();
        if (!font->ft) {
            delete font;
            return recordError(MSDF_ATLAS_ERROR_FONT_LOAD, "failed to initialize FreeType");
        }
        font->handle = msdfgen::loadFontData(font->ft, (const msdfgen::byte *) data, (int) data_size);
        if (!font->handle) {
            delete font;
            return recordError(MSDF_ATLAS_ERROR_FONT_LOAD, "font data could not be parsed");
        }
        *out_font = font;
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_font_destroy(msdf_atlas_font_t *font) {
    return guard([&]() {
        clearError();
        if (font) {
            if (font->handle)
                msdfgen::destroyFont(font->handle);
            if (font->ft)
                msdfgen::deinitializeFreetype(font->ft);
            delete font;
        }
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_font_get_metrics(const msdf_atlas_font_t *font, msdf_atlas_font_metrics_t *out_metrics) {
    return guard([&]() {
        clearError();
        if (!font || !out_metrics)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "font or out_metrics is NULL");
        msdfgen::FontMetrics metrics = { };
        // Raw font units (em_size = units per em), matching "font units".
        if (!msdfgen::getFontMetrics(metrics, font->handle, msdfgen::FONT_SCALING_NONE))
            return recordError(MSDF_ATLAS_ERROR_INVALID_STATE, "font metrics are unavailable");
        out_metrics->em_size = metrics.emSize;
        out_metrics->ascender_y = metrics.ascenderY;
        out_metrics->descender_y = metrics.descenderY;
        out_metrics->line_height = metrics.lineHeight;
        out_metrics->underline_y = metrics.underlineY;
        out_metrics->underline_thickness = metrics.underlineThickness;
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_font_get_glyph_count(const msdf_atlas_font_t *font, uint32_t *out_count) {
    return guard([&]() {
        clearError();
        if (!font || !out_count)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "font or out_count is NULL");
        unsigned count = 0;
        if (!msdfgen::getGlyphCount(count, font->handle))
            return recordError(MSDF_ATLAS_ERROR_INVALID_STATE, "glyph count is unavailable");
        *out_count = count;
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_font_get_glyph_index(const msdf_atlas_font_t *font, uint32_t codepoint, uint32_t *out_glyph_index) {
    return guard([&]() {
        clearError();
        if (!font || !out_glyph_index)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "font or out_glyph_index is NULL");
        msdfgen::GlyphIndex index;
        if (!msdfgen::getGlyphIndex(index, font->handle, codepoint))
            return recordError(MSDF_ATLAS_ERROR_OUT_OF_RANGE, "codepoint has no glyph in this font");
        *out_glyph_index = index.getIndex();
        return MSDF_ATLAS_OK;
    });
}

/* ================================================================== */
/* Glyph geometry (glyphset)                                           */
/* ================================================================== */

extern "C" msdf_atlas_error_t msdf_atlas_glyphset_create(msdf_atlas_glyphset_t **out_set) {
    return guard([&]() {
        clearError();
        if (!out_set)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "out_set is NULL");
        *out_set = NULL;
        *out_set = new msdf_atlas_glyphset_t();
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_glyphset_destroy(msdf_atlas_glyphset_t *set) {
    return guard([&]() {
        clearError();
        delete set;
        return MSDF_ATLAS_OK;
    });
}

namespace {

/** Loads glyphs into a fresh FontGeometry, then replaces the set's glyphs. */
template <typename LOAD>
msdf_atlas_error_t glyphsetLoad(msdf_atlas_glyphset_t *set, const msdf_atlas_font_t *font, double font_scale,
                                uint32_t load_flags, int32_t *out_loaded, LOAD &&load) {
    if (!set)
        return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "set is NULL");
    if (!font)
        return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "font is NULL");
    if (!(font_scale > 0))
        return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "font_scale must be positive");
    msdf_atlas::FontGeometry fresh;
    int loaded = load(fresh);
    if (loaded < 0)
        return recordError(MSDF_ATLAS_ERROR_GLYPH_LOAD, "glyph geometry could not be loaded from the font");
    set->geometry = std::move(fresh); // Replaces previously loaded glyphs.
    set->packW = 0;
    set->packH = 0;
    set->kerningLoaded = (load_flags & MSDF_ATLAS_LOAD_KERNING) != 0;
    if (out_loaded)
        *out_loaded = loaded;
    return MSDF_ATLAS_OK;
}

} // anonymous namespace

extern "C" msdf_atlas_error_t msdf_atlas_glyphset_load_charset(msdf_atlas_glyphset_t *set, const msdf_atlas_font_t *font, double font_scale, const msdf_atlas_charset_t *charset, uint32_t load_flags, int32_t *out_loaded) {
    return guard([&]() {
        clearError();
        if (!charset)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "charset is NULL");
        const bool preprocess = (load_flags & MSDF_ATLAS_LOAD_PREPROCESS_GEOMETRY) != 0;
        const bool kerning = (load_flags & MSDF_ATLAS_LOAD_KERNING) != 0;
        return glyphsetLoad(set, font, font_scale, load_flags, out_loaded,
                            [&](msdf_atlas::FontGeometry &fresh) {
                                return fresh.loadCharset(font->handle, font_scale, charset->charset, preprocess, kerning);
                            });
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_glyphset_load_glyphset(msdf_atlas_glyphset_t *set, const msdf_atlas_font_t *font, double font_scale, const msdf_atlas_charset_t *glyph_indices, uint32_t load_flags, int32_t *out_loaded) {
    return guard([&]() {
        clearError();
        if (!glyph_indices)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "glyph_indices is NULL");
        const bool preprocess = (load_flags & MSDF_ATLAS_LOAD_PREPROCESS_GEOMETRY) != 0;
        const bool kerning = (load_flags & MSDF_ATLAS_LOAD_KERNING) != 0;
        return glyphsetLoad(set, font, font_scale, load_flags, out_loaded,
                            [&](msdf_atlas::FontGeometry &fresh) {
                                return fresh.loadGlyphset(font->handle, font_scale, glyph_indices->charset, preprocess, kerning);
                            });
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_glyphset_load_glyph_range(msdf_atlas_glyphset_t *set, const msdf_atlas_font_t *font, double font_scale, uint32_t first_glyph_index, uint32_t glyph_count, uint32_t load_flags, int32_t *out_loaded) {
    return guard([&]() {
        clearError();
        if (glyph_count == 0)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "glyph_count must be positive");
        if (first_glyph_index > (uint32_t) std::numeric_limits<uint32_t>::max() - glyph_count)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "glyph range overflows");
        const bool preprocess = (load_flags & MSDF_ATLAS_LOAD_PREPROCESS_GEOMETRY) != 0;
        const bool kerning = (load_flags & MSDF_ATLAS_LOAD_KERNING) != 0;
        return glyphsetLoad(set, font, font_scale, load_flags, out_loaded,
                            [&](msdf_atlas::FontGeometry &fresh) {
                                return fresh.loadGlyphRange(font->handle, font_scale, first_glyph_index, first_glyph_index + glyph_count, preprocess, kerning);
                            });
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_glyphset_edge_color(msdf_atlas_glyphset_t *set, msdf_atlas_edge_coloring_t coloring, double angle_threshold, uint64_t seed) {
    return guard([&]() {
        clearError();
        if (!set)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "set is NULL");
        if (coloring == MSDF_ATLAS_EDGE_COLORING_NONE)
            return MSDF_ATLAS_OK;
        if (coloring == MSDF_ATLAS_EDGE_COLORING_DIAGONAL)
            return recordError(MSDF_ATLAS_ERROR_UNSUPPORTED,
                               "diagonal edge coloring is not provided by the vendored msdfgen; use SIMPLE, INKTRAP or BY_DISTANCE");
        auto glyphs = set->geometry.getGlyphs();
        if (glyphs.size() == 0)
            return recordError(MSDF_ATLAS_ERROR_INVALID_STATE, "glyphset is empty");
        const double pi = 3.14159265358979323846;
        double threshold = angle_threshold * pi / 180.0;
        if (threshold < 0)
            threshold = 0;
        if (threshold > pi)
            threshold = pi;
        void (*fn)(msdfgen::Shape &, double, unsigned long long) = NULL;
        switch (coloring) {
            case MSDF_ATLAS_EDGE_COLORING_SIMPLE:
                fn = &msdfgen::edgeColoringSimple;
                break;
            case MSDF_ATLAS_EDGE_COLORING_INKTRAP:
                fn = &msdfgen::edgeColoringInkTrap;
                break;
            case MSDF_ATLAS_EDGE_COLORING_BY_DISTANCE:
                fn = &msdfgen::edgeColoringByDistance;
                break;
            default:
                return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "invalid coloring");
        }
        // getGlyphs() is const-only; the glyphset owns the geometry, so the
        // cast is safe and mirrors upstream, which colors its own glyph copy.
        for (const msdf_atlas::GlyphGeometry &glyph : glyphs)
            const_cast<msdf_atlas::GlyphGeometry &>(glyph).edgeColoring(fn, threshold, (unsigned long long) seed);
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_glyphset_get_count(const msdf_atlas_glyphset_t *set, size_t *out_count) {
    return guard([&]() {
        clearError();
        if (!set || !out_count)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "set or out_count is NULL");
        *out_count = set->geometry.getGlyphs().size();
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_glyphset_get_layout(const msdf_atlas_glyphset_t *set, size_t glyph_index, msdf_atlas_glyph_layout_t *out_layout) {
    return guard([&]() {
        clearError();
        if (!set || !out_layout)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "set or out_layout is NULL");
        auto glyphs = set->geometry.getGlyphs();
        if (glyph_index >= glyphs.size())
            return recordError(MSDF_ATLAS_ERROR_OUT_OF_RANGE, "glyph index out of range");
        *out_layout = msdf_atlas_glyph_layout_t{ };
        fillGlyphLayout(*(glyphs.begin() + glyph_index), *out_layout);
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_glyphset_get_advance(const msdf_atlas_glyphset_t *set, uint32_t identifier_a, uint32_t identifier_b, msdf_atlas_glyph_identifier_type_t identifier_type, double *out_advance) {
    return guard([&]() {
        clearError();
        if (!set || !out_advance)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "set or out_advance is NULL");
        if (!set->kerningLoaded)
            return recordError(MSDF_ATLAS_ERROR_INVALID_STATE, "kerning was not loaded (use MSDF_ATLAS_LOAD_KERNING)");
        double advance = 0;
        switch (identifier_type) {
            case MSDF_ATLAS_IDENTIFIER_GLYPH_INDEX: {
                const msdf_atlas::GlyphGeometry *glyphA = set->geometry.getGlyph(msdfgen::GlyphIndex(identifier_a));
                const msdf_atlas::GlyphGeometry *glyphB = set->geometry.getGlyph(msdfgen::GlyphIndex(identifier_b));
                if (!glyphA || !glyphB)
                    return recordError(MSDF_ATLAS_ERROR_OUT_OF_RANGE, "glyph identifier is not present in the set");
                if (!set->geometry.getAdvance(advance, msdfgen::GlyphIndex(identifier_a), msdfgen::GlyphIndex(identifier_b)))
                    return recordError(MSDF_ATLAS_ERROR_OUT_OF_RANGE, "glyph identifier is not present in the set");
                break;
            }
            case MSDF_ATLAS_IDENTIFIER_UNICODE_CODEPOINT:
                if (!set->geometry.getAdvance(advance, (msdfgen::unicode_t) identifier_a, (msdfgen::unicode_t) identifier_b))
                    return recordError(MSDF_ATLAS_ERROR_OUT_OF_RANGE, "codepoint identifier is not present in the set");
                break;
            default:
                return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "invalid identifier_type");
        }
        *out_advance = advance;
        return MSDF_ATLAS_OK;
    });
}

/* ================================================================== */
/* Packer                                                              */
/* ================================================================== */

extern "C" msdf_atlas_error_t msdf_atlas_packer_create(const msdf_atlas_config_t *config, msdf_atlas_packer_t **out_packer) {
    return guard([&]() {
        clearError();
        if (!out_packer)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "out_packer is NULL");
        *out_packer = NULL; // Always NULL on failure, whatever the error path.
        msdf_atlas_config_t defaults = { };
        if (!config) {
            msdf_atlas_config_default(&defaults);
            config = &defaults;
        }
        if (msdf_atlas_error_t error = validateConfig(config))
            return error;
        msdf_atlas_packer_t *packer = new msdf_atlas_packer_t();
        packer->grid = config->packing_style == MSDF_ATLAS_PACKING_GRID;
        applyPackerConfig(packer, *config);
        *out_packer = packer;
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_destroy(msdf_atlas_packer_t *packer) {
    return guard([&]() {
        clearError();
        delete packer;
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_apply_config(msdf_atlas_packer_t *packer, const msdf_atlas_config_t *config) {
    return guard([&]() {
        clearError();
        if (!packer || !config)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer or config is NULL");
        if (msdf_atlas_error_t error = validateConfig(config))
            return error;
        if ((config->packing_style == MSDF_ATLAS_PACKING_GRID) != packer->grid)
            return recordError(MSDF_ATLAS_ERROR_INVALID_STATE, "packing_style cannot be changed on an existing packer");
        applyPackerConfig(packer, *config);
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_set_dimensions(msdf_atlas_packer_t *packer, int32_t width, int32_t height) {
    return guard([&]() {
        clearError();
        if (!packer)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer is NULL");
        if (width <= 0 || height <= 0)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "width and height must be positive");
        if (packer->grid)
            packer->gridP.setDimensions(width, height);
        else
            packer->tight.setDimensions(width, height);
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_unset_dimensions(msdf_atlas_packer_t *packer) {
    return guard([&]() {
        clearError();
        if (!packer)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer is NULL");
        if (packer->grid)
            packer->gridP.unsetDimensions();
        else
            packer->tight.unsetDimensions();
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_set_dimensions_constraint(msdf_atlas_packer_t *packer, msdf_atlas_dimensions_constraint_t constraint) {
    return guard([&]() {
        clearError();
        if (!packer)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer is NULL");
        if (constraint < 0 || constraint > MSDF_ATLAS_DIMENSIONS_POWER_OF_TWO_SQUARE)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "constraint is out of range");
        if (packer->grid)
            packer->gridP.setDimensionsConstraint(toConstraint(constraint));
        else
            packer->tight.setDimensionsConstraint(toConstraint(constraint));
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_set_spacing(msdf_atlas_packer_t *packer, int32_t spacing) {
    return guard([&]() {
        clearError();
        if (!packer)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer is NULL");
        if (spacing < 0)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "spacing must be non-negative");
        if (packer->grid)
            packer->gridP.setSpacing(spacing);
        else
            packer->tight.setSpacing(spacing);
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_set_scale(msdf_atlas_packer_t *packer, double scale) {
    return guard([&]() {
        clearError();
        if (!packer)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer is NULL");
        if (!(scale > 0))
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "scale must be positive");
        if (packer->grid)
            packer->gridP.setScale(scale);
        else
            packer->tight.setScale(scale);
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_set_minimum_scale(msdf_atlas_packer_t *packer, double min_scale) {
    return guard([&]() {
        clearError();
        if (!packer)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer is NULL");
        if (min_scale < 0)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "min_scale must be non-negative");
        if (packer->grid)
            packer->gridP.setMinimumScale(min_scale);
        else
            packer->tight.setMinimumScale(min_scale);
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_set_unit_range(msdf_atlas_packer_t *packer, msdf_atlas_range_t range) {
    return guard([&]() {
        clearError();
        if (!packer)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer is NULL");
        if (range.lower < 0 || range.upper < range.lower)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "range is malformed");
        if (packer->grid)
            packer->gridP.setUnitRange(toRange(range));
        else
            packer->tight.setUnitRange(toRange(range));
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_set_pixel_range(msdf_atlas_packer_t *packer, msdf_atlas_range_t range) {
    return guard([&]() {
        clearError();
        if (!packer)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer is NULL");
        if (range.lower < 0 || range.upper < range.lower)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "range is malformed");
        if (packer->grid)
            packer->gridP.setPixelRange(toRange(range));
        else
            packer->tight.setPixelRange(toRange(range));
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_set_miter_limit(msdf_atlas_packer_t *packer, double miter_limit) {
    return guard([&]() {
        clearError();
        if (!packer)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer is NULL");
        if (!(miter_limit > 0))
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "miter_limit must be positive");
        if (packer->grid)
            packer->gridP.setMiterLimit(miter_limit);
        else
            packer->tight.setMiterLimit(miter_limit);
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_set_padding(msdf_atlas_packer_t *packer, const msdf_atlas_padding_t *inner_px, const msdf_atlas_padding_t *outer_px, const msdf_atlas_padding_t *inner_unit, const msdf_atlas_padding_t *outer_unit) {
    return guard([&]() {
        clearError();
        if (!packer)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer is NULL");
        if (!inner_px && !outer_px && !inner_unit && !outer_unit)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "at least one padding component must be given");
        if (packer->grid) {
            if (inner_px)
                packer->gridP.setInnerPixelPadding(toPadding(*inner_px));
            if (outer_px)
                packer->gridP.setOuterPixelPadding(toPadding(*outer_px));
            if (inner_unit)
                packer->gridP.setInnerUnitPadding(toPadding(*inner_unit));
            if (outer_unit)
                packer->gridP.setOuterUnitPadding(toPadding(*outer_unit));
        } else {
            if (inner_px)
                packer->tight.setInnerPixelPadding(toPadding(*inner_px));
            if (outer_px)
                packer->tight.setOuterPixelPadding(toPadding(*outer_px));
            if (inner_unit)
                packer->tight.setInnerUnitPadding(toPadding(*inner_unit));
            if (outer_unit)
                packer->tight.setOuterUnitPadding(toPadding(*outer_unit));
        }
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_set_columns(msdf_atlas_packer_t *packer, int32_t columns) {
    return guard([&]() {
        clearError();
        if (!packer)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer is NULL");
        if (!packer->grid)
            return recordError(MSDF_ATLAS_ERROR_INVALID_STATE, "columns are only defined for grid packing");
        if (columns <= 0)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "columns must be positive");
        packer->gridP.setColumns(columns);
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_unset_columns(msdf_atlas_packer_t *packer) {
    return guard([&]() {
        clearError();
        if (!packer)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer is NULL");
        if (!packer->grid)
            return recordError(MSDF_ATLAS_ERROR_INVALID_STATE, "columns are only defined for grid packing");
        packer->gridP.unsetColumns();
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_set_rows(msdf_atlas_packer_t *packer, int32_t rows) {
    return guard([&]() {
        clearError();
        if (!packer)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer is NULL");
        if (!packer->grid)
            return recordError(MSDF_ATLAS_ERROR_INVALID_STATE, "rows are only defined for grid packing");
        if (rows <= 0)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "rows must be positive");
        packer->gridP.setRows(rows);
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_unset_rows(msdf_atlas_packer_t *packer) {
    return guard([&]() {
        clearError();
        if (!packer)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer is NULL");
        if (!packer->grid)
            return recordError(MSDF_ATLAS_ERROR_INVALID_STATE, "rows are only defined for grid packing");
        packer->gridP.unsetRows();
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_set_cell_dimensions(msdf_atlas_packer_t *packer, int32_t width, int32_t height) {
    return guard([&]() {
        clearError();
        if (!packer)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer is NULL");
        if (!packer->grid)
            return recordError(MSDF_ATLAS_ERROR_INVALID_STATE, "cell dimensions are only defined for grid packing");
        if (width <= 0 || height <= 0)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "width and height must be positive");
        packer->gridP.setCellDimensions(width, height);
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_unset_cell_dimensions(msdf_atlas_packer_t *packer) {
    return guard([&]() {
        clearError();
        if (!packer)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer is NULL");
        if (!packer->grid)
            return recordError(MSDF_ATLAS_ERROR_INVALID_STATE, "cell dimensions are only defined for grid packing");
        packer->gridP.unsetCellDimensions();
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_set_fixed_origin(msdf_atlas_packer_t *packer, bool horizontal, bool vertical) {
    return guard([&]() {
        clearError();
        if (!packer)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer is NULL");
        if (!packer->grid)
            return recordError(MSDF_ATLAS_ERROR_INVALID_STATE, "fixed origin is only defined for grid packing");
        packer->gridP.setFixedOrigin(horizontal, vertical);
        return MSDF_ATLAS_OK;
    });
}

namespace {

msdf_atlas_error_t packGlyphs(msdf_atlas_packer_t *packer, msdf_atlas_glyphset_t *set) {
    if (!packer)
        return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer is NULL");
    if (!set)
        return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "set is NULL");
    auto glyphs = set->geometry.getGlyphs();
    if (glyphs.size() == 0)
        return recordError(MSDF_ATLAS_ERROR_INVALID_STATE, "glyphset is empty");
    int result = 0;
    if (packer->grid)
        result = packer->gridP.pack(const_cast<msdf_atlas::GlyphGeometry *>(glyphs.begin()), (int) glyphs.size());
    else
        result = packer->tight.pack(const_cast<msdf_atlas::GlyphGeometry *>(glyphs.begin()), (int) glyphs.size());
    if (result != 0) {
        if (result > 0)
            return recordError(MSDF_ATLAS_ERROR_PACK, "glyphs do not fit in the atlas");
        return recordError(MSDF_ATLAS_ERROR_PACK, "atlas layout computation failed");
    }
    packer->packed = true;
    if (packer->grid) {
        packer->gridP.getDimensions(packer->dimW, packer->dimH);
        packer->gridP.getCellDimensions(packer->cellW, packer->cellH);
        packer->columns = packer->gridP.getColumns();
        packer->rows = packer->gridP.getRows();
        packer->gridP.getFixedOrigin(packer->fixedX, packer->fixedY);
        packer->cutoff = packer->gridP.hasCutoff();
        packer->scale = packer->gridP.getScale();
        packer->pxRange = packer->gridP.getPixelRange();
    } else {
        packer->tight.getDimensions(packer->dimW, packer->dimH);
        packer->scale = packer->tight.getScale();
        packer->pxRange = packer->tight.getPixelRange();
    }
    set->packW = packer->dimW;
    set->packH = packer->dimH;
    return MSDF_ATLAS_OK;
}

} // anonymous namespace

extern "C" msdf_atlas_error_t msdf_atlas_packer_pack(msdf_atlas_packer_t *packer, msdf_atlas_glyphset_t *set) {
    return guard([&]() {
        clearError();
        return packGlyphs(packer, set);
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_get_dimensions(const msdf_atlas_packer_t *packer, int32_t *out_width, int32_t *out_height) {
    return guard([&]() {
        clearError();
        if (!packer || !out_width || !out_height)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer, out_width or out_height is NULL");
        *out_width = packer->dimW;
        *out_height = packer->dimH;
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_get_scale(const msdf_atlas_packer_t *packer, double *out_scale) {
    return guard([&]() {
        clearError();
        if (!packer || !out_scale)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer or out_scale is NULL");
        *out_scale = packer->scale;
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_get_pixel_range(const msdf_atlas_packer_t *packer, msdf_atlas_range_t *out_range) {
    return guard([&]() {
        clearError();
        if (!packer || !out_range)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer or out_range is NULL");
        out_range->lower = packer->pxRange.lower;
        out_range->upper = packer->pxRange.upper;
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_get_cell_dimensions(const msdf_atlas_packer_t *packer, int32_t *out_width, int32_t *out_height) {
    return guard([&]() {
        clearError();
        if (!packer || !out_width || !out_height)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer, out_width or out_height is NULL");
        if (!packer->grid)
            return recordError(MSDF_ATLAS_ERROR_INVALID_STATE, "cell dimensions are only defined for grid packing");
        *out_width = packer->cellW;
        *out_height = packer->cellH;
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_get_columns(const msdf_atlas_packer_t *packer, int32_t *out_columns) {
    return guard([&]() {
        clearError();
        if (!packer || !out_columns)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer or out_columns is NULL");
        if (!packer->grid)
            return recordError(MSDF_ATLAS_ERROR_INVALID_STATE, "columns are only defined for grid packing");
        *out_columns = packer->columns;
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_get_rows(const msdf_atlas_packer_t *packer, int32_t *out_rows) {
    return guard([&]() {
        clearError();
        if (!packer || !out_rows)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer or out_rows is NULL");
        if (!packer->grid)
            return recordError(MSDF_ATLAS_ERROR_INVALID_STATE, "rows are only defined for grid packing");
        *out_rows = packer->rows;
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_get_fixed_origin(const msdf_atlas_packer_t *packer, double *out_x, double *out_y) {
    return guard([&]() {
        clearError();
        if (!packer || !out_x || !out_y)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer, out_x or out_y is NULL");
        if (!packer->grid)
            return recordError(MSDF_ATLAS_ERROR_INVALID_STATE, "fixed origin is only defined for grid packing");
        *out_x = packer->fixedX;
        *out_y = packer->fixedY;
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_packer_has_cutoff(const msdf_atlas_packer_t *packer, bool *out_cutoff) {
    return guard([&]() {
        clearError();
        if (!packer || !out_cutoff)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "packer or out_cutoff is NULL");
        if (!packer->grid)
            return recordError(MSDF_ATLAS_ERROR_INVALID_STATE, "cutoff is only defined for grid packing");
        *out_cutoff = packer->cutoff;
        return MSDF_ATLAS_OK;
    });
}

/* ================================================================== */
/* Generator                                                           */
/* ================================================================== */

extern "C" msdf_atlas_error_t msdf_atlas_generator_create(const msdf_atlas_config_t *config, msdf_atlas_generator_t **out_generator) {
    return guard([&]() {
        clearError();
        if (!out_generator)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "out_generator is NULL");
        *out_generator = NULL; // Always NULL on failure, whatever the error path.
        msdf_atlas_config_t defaults = { };
        if (!config) {
            msdf_atlas_config_default(&defaults);
            config = &defaults;
        }
        if (msdf_atlas_error_t error = validateConfig(config))
            return error;
        msdf_atlas_pixel_format_t pixelFormat = config->pixel_format;
        if (pixelFormat == MSDF_ATLAS_PIXEL_UNKNOWN) {
            switch (config->image_type) {
                case MSDF_ATLAS_IMAGE_MSDF:
                    pixelFormat = MSDF_ATLAS_PIXEL_RGB32F;
                    break;
                case MSDF_ATLAS_IMAGE_MTSDF:
                    pixelFormat = MSDF_ATLAS_PIXEL_RGBA32F;
                    break;
                case MSDF_ATLAS_IMAGE_HARD_MASK:
                case MSDF_ATLAS_IMAGE_SOFT_MASK:
                    pixelFormat = MSDF_ATLAS_PIXEL_R8;
                    break;
                case MSDF_ATLAS_IMAGE_SDF:
                case MSDF_ATLAS_IMAGE_PSDF:
                default:
                    pixelFormat = MSDF_ATLAS_PIXEL_R32F;
                    break;
            }
        }
        msdf_atlas_generator_t *generator = new msdf_atlas_generator_t();
        generator->impl = makeGenerator(config->image_type, pixelFormat);
        generator->pixelFormat = pixelFormat;
        generator->yDirection = config->y_direction;
        generator->flags = config->flags & (MSDF_ATLAS_CONFIG_ERROR_CORRECTION |
                                            MSDF_ATLAS_CONFIG_OVERLAP_SUPPORT |
                                            MSDF_ATLAS_CONFIG_SCANLINE_PASS);
        generator->errorCorrectionMode = config->error_correction_mode;
        generator->errorCorrectionCheck = config->error_correction_check;
        applyGeneratorAttributes(generator);
        if (config->thread_count > 0) {
            generator->impl->setThreadCount(config->thread_count);
        } else {
            unsigned autoThreads = std::thread::hardware_concurrency();
            generator->impl->setThreadCount(autoThreads ? (int) autoThreads : 1);
        }
        *out_generator = generator;
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_generator_destroy(msdf_atlas_generator_t *generator) {
    return guard([&]() {
        clearError();
        delete generator;
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_generator_set_thread_count(msdf_atlas_generator_t *generator, int32_t thread_count) {
    return guard([&]() {
        clearError();
        if (!generator)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "generator is NULL");
        if (thread_count > 0) {
            generator->impl->setThreadCount(thread_count);
        } else {
            unsigned autoThreads = std::thread::hardware_concurrency();
            generator->impl->setThreadCount(autoThreads ? (int) autoThreads : 1);
        }
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_generator_set_flags(msdf_atlas_generator_t *generator, uint32_t flags) {
    return guard([&]() {
        clearError();
        if (!generator)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "generator is NULL");
        generator->flags = flags & (MSDF_ATLAS_CONFIG_ERROR_CORRECTION |
                                    MSDF_ATLAS_CONFIG_OVERLAP_SUPPORT |
                                    MSDF_ATLAS_CONFIG_SCANLINE_PASS);
        applyGeneratorAttributes(generator);
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_generator_resize(msdf_atlas_generator_t *generator, int32_t width, int32_t height) {
    return guard([&]() {
        clearError();
        if (!generator)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "generator is NULL");
        if (width <= 0 || height <= 0)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "width and height must be positive");
        generator->impl->resize(width, height);
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_generator_generate(msdf_atlas_generator_t *generator, const msdf_atlas_glyphset_t *set) {
    return guard([&]() {
        clearError();
        if (!generator)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "generator is NULL");
        if (!set)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "set is NULL");
        auto glyphs = set->geometry.getGlyphs();
        if (glyphs.size() == 0)
            return recordError(MSDF_ATLAS_ERROR_INVALID_STATE, "glyphset is empty");
        // Apply the packer's dimensions when the generator has no size yet.
        if (generator->impl->width() == 0 && generator->impl->height() == 0) {
            if (set->packW <= 0 || set->packH <= 0)
                return recordError(MSDF_ATLAS_ERROR_INVALID_STATE, "glyphset has no atlas layout (pack it first)");
            generator->impl->resize(set->packW, set->packH);
        } else {
            bool hasPlacement = false;
            for (const msdf_atlas::GlyphGeometry &glyph : glyphs) {
                if (glyph.isWhitespace())
                    continue;
                int x = 0, y = 0, w = 0, h = 0;
                glyph.getBoxRect(x, y, w, h);
                if (w > 0 && h > 0) {
                    hasPlacement = true;
                    break;
                }
            }
            if (!hasPlacement)
                return recordError(MSDF_ATLAS_ERROR_INVALID_STATE, "glyphs have no atlas placement (pack the glyphset first)");
        }
        generator->impl->generate(glyphs.begin(), (int) glyphs.size());
        if (generator->yDirection == MSDF_ATLAS_Y_TOP_DOWN)
            generator->impl->flipRows();
        // Snapshot the glyph layouts (GlyphBox lacks codepoint/whitespace).
        const int height = generator->impl->height();
        generator->layouts.clear();
        generator->layouts.reserve(glyphs.size());
        for (const msdf_atlas::GlyphGeometry &glyph : glyphs) {
            msdf_atlas_glyph_layout_t layout = { };
            fillGlyphLayout(glyph, layout);
            if (generator->yDirection == MSDF_ATLAS_Y_TOP_DOWN && layout.atlas_w > 0 && layout.atlas_h > 0) {
                layout.atlas_y = height - layout.atlas_y - layout.atlas_h;
                double bottom = layout.atlas_bounds_b;
                double top = layout.atlas_bounds_t;
                layout.atlas_bounds_b = height - top;
                layout.atlas_bounds_t = height - bottom;
            }
            generator->layouts.push_back(layout);
        }
        generator->generated = true;
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_generator_get_bitmap(const msdf_atlas_generator_t *generator, msdf_atlas_bitmap_t *out_bitmap) {
    return guard([&]() {
        clearError();
        if (!generator || !out_bitmap)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "generator or out_bitmap is NULL");
        *out_bitmap = msdf_atlas_bitmap_t{ };
        if (!generator->generated)
            return MSDF_ATLAS_OK; // pixels NULL, dims 0, format UNKNOWN.
        out_bitmap->pixels = generator->impl->pixels();
        out_bitmap->width = generator->impl->width();
        out_bitmap->height = generator->impl->height();
        out_bitmap->channel_count = generator->impl->channels();
        out_bitmap->pixel_format = generator->pixelFormat;
        out_bitmap->row_stride_bytes = (int32_t) ((size_t) generator->impl->width() *
                                                  (size_t) generator->impl->channels() *
                                                  generator->impl->componentSize());
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_generator_get_layout_count(const msdf_atlas_generator_t *generator, size_t *out_count) {
    return guard([&]() {
        clearError();
        if (!generator || !out_count)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "generator or out_count is NULL");
        *out_count = generator->layouts.size();
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_generator_get_layout(const msdf_atlas_generator_t *generator, size_t index, msdf_atlas_glyph_layout_t *out_layout) {
    return guard([&]() {
        clearError();
        if (!generator || !out_layout)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "generator or out_layout is NULL");
        if (index >= generator->layouts.size())
            return recordError(MSDF_ATLAS_ERROR_OUT_OF_RANGE, "layout index out of range");
        *out_layout = generator->layouts[index];
        return MSDF_ATLAS_OK;
    });
}

extern "C" msdf_atlas_error_t msdf_atlas_generator_get_layout_all(const msdf_atlas_generator_t *generator, const msdf_atlas_glyph_layout_t **out_layouts, size_t *out_count) {
    return guard([&]() {
        clearError();
        if (!generator || !out_layouts || !out_count)
            return recordError(MSDF_ATLAS_ERROR_INVALID_ARGUMENT, "generator, out_layouts or out_count is NULL");
        if (generator->layouts.empty()) {
            *out_layouts = NULL;
            *out_count = 0;
        } else {
            *out_layouts = generator->layouts.data();
            *out_count = generator->layouts.size();
        }
        return MSDF_ATLAS_OK;
    });
}
