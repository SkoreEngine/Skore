/**
 * msdf_atlas_smoke.cpp — vendored msdf-atlas-gen smoke check (APX-224).
 *
 * Generates an MTSDF glyph atlas from the project's sample font with the
 * vendored msdf-atlas-gen + msdfgen libraries (thirdparty/msdf-atlas-gen,
 * reusing thirdparty/freetype) and asserts the two expected output
 * artifacts are produced:
 *
 *   1. the atlas image  — a float32 RGBA bitmap (atlas pixels + non-zero
 *      content, dimensions and stride sanity-checked), also dumped as
 *      msdf_atlas_smoke.raw next to the test binary; and
 *   2. the layout data  — one GlyphBox per loaded glyph (count, advance,
 *      plane quad bounds, atlas box rectangle inside the atlas), also
 *      dumped as msdf_atlas_smoke_layout.txt.
 *
 * This is the same library pipeline the engine's font resources will use
 * (FreetypeHandle → FontGeometry → TightAtlasPacker →
 * ImmediateAtlasGenerator<..., mtsdfGenerator, BitmapAtlasStorage>), so a
 * passing run proves the vendored build is linkable and functional, not
 * just compilable.
 *
 * The sample font is the checked-in plugins/ui/testdata/skore_test_font.ttf
 * (38 glyphs; also embedded in tests as a C header for the plugin tests). Paths
 * are baked in at configure time:
 *   SK_MSDF_SMOKE_FONT_PATH     — source-tree sample font
 *   SK_MSDF_SMOKE_ARTIFACT_DIR  — where the .raw / .txt artifacts are written
 */

#include <msdf-atlas-gen.h>
#include <msdfgen.h>
#include <msdfgen-ext.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#ifndef SK_MSDF_SMOKE_FONT_PATH
#error "SK_MSDF_SMOKE_FONT_PATH must be defined by CMake"
#endif
#ifndef SK_MSDF_SMOKE_ARTIFACT_DIR
#error "SK_MSDF_SMOKE_ARTIFACT_DIR must be defined by CMake"
#endif

static int g_failures = 0;

#define CHECK(cond, ...)                                              \
	do {                                                              \
		if (!(cond)) {                                                \
			++g_failures;                                             \
			std::fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
			std::fprintf(stderr, __VA_ARGS__);                        \
			std::fprintf(stderr, "\n");                               \
		}                                                             \
	} while (0)

static bool read_file(const std::string& path, std::vector<unsigned char>& out) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return false;
	}
	in.seekg(0, std::ios::end);
	std::streamoff size = in.tellg();
	in.seekg(0, std::ios::beg);
	if (size <= 0) {
		return false;
	}
	out.resize(static_cast<size_t>(size));
	in.read(reinterpret_cast<char*>(out.data()), size);
	return in.good() || in.eof();
}

static bool write_file(const std::string& path, const void* data, size_t size) {
	std::ofstream out(path, std::ios::binary);
	if (!out) {
		return false;
	}
	out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
	return out.good();
}

int main() {
	int failures_before = g_failures;

	/* ---- 1. FreeType + font load (msdfgen ext) ------------------------- */
	msdfgen::FreetypeHandle* ft = msdfgen::initializeFreetype();
	CHECK(ft != nullptr, "initializeFreetype() returned NULL");

	std::vector<unsigned char> font_data;
	CHECK(read_file(SK_MSDF_SMOKE_FONT_PATH, font_data), "cannot read sample font: %s", SK_MSDF_SMOKE_FONT_PATH);

	msdfgen::FontHandle* font = nullptr;
	if (ft != nullptr && !font_data.empty()) {
		font = msdfgen::loadFontData(ft, font_data.data(), (int)font_data.size());
	}
	CHECK(font != nullptr, "loadFontData() failed for %s", SK_MSDF_SMOKE_FONT_PATH);

	unsigned glyph_count = 0;
	if (font != nullptr) {
		bool ok = msdfgen::getGlyphCount(glyph_count, font);
		CHECK(ok, "getGlyphCount() failed");
	}
	CHECK(glyph_count > 0, "sample font reports no glyphs");

	msdfgen::FontMetrics metrics = {};
	if (font != nullptr) {
		bool ok = msdfgen::getFontMetrics(metrics, font, msdfgen::FONT_SCALING_EM_NORMALIZED);
		CHECK(ok, "getFontMetrics() failed");
	}
	CHECK(metrics.emSize > 0.0, "font em size is not positive: %g", metrics.emSize);
	CHECK(metrics.ascenderY > 0.0, "font ascender is not positive: %g", metrics.ascenderY);
	CHECK(metrics.lineHeight > 0.0, "font line height is not positive: %g", metrics.lineHeight);

	/* ---- 2. Glyph geometry (msdf_atlas::FontGeometry) ------------------ */
	std::vector<msdf_atlas::GlyphGeometry> glyphs;
	msdf_atlas::FontGeometry geometry(&glyphs);

	int loaded = 0;
	if (font != nullptr) {
		loaded = geometry.loadGlyphRange(font, 1.0, 0, glyph_count, true, true);
	}
	CHECK(loaded == (int)glyph_count, "loadGlyphRange() loaded %d of %u glyphs", loaded, glyph_count);

	size_t non_whitespace = 0;
	if (loaded > 0) {
		/* Same edge-coloring pass the engine's font importer runs. */
		for (msdf_atlas::GlyphGeometry& glyph : glyphs) {
			glyph.edgeColoring(&msdfgen::edgeColoringInkTrap, 3.0, 0);
			if (!glyph.isWhitespace()) {
				++non_whitespace;
			}
		}
	}
	CHECK(non_whitespace > 0, "sample font produced no glyphs with geometry");

	/* ---- 3. Atlas layout (msdf_atlas::TightAtlasPacker) ---------------- */
	msdf_atlas::TightAtlasPacker packer;
	packer.setPixelRange(msdfgen::Range(2.0));
	packer.setMiterLimit(1.0);
	packer.setDimensionsConstraint(msdf_atlas::DimensionsConstraint::POWER_OF_TWO_SQUARE);
	packer.setSpacing(0);

	int remaining = -1;
	if (loaded > 0) {
		remaining = packer.pack(glyphs.data(), (int)glyphs.size());
	}
	CHECK(remaining == 0, "pack() left %d glyphs unpacked", remaining);

	int atlas_w = 0, atlas_h = 0;
	if (remaining == 0) {
		packer.getDimensions(atlas_w, atlas_h);
	}
	CHECK(atlas_w > 0 && atlas_h > 0, "packer chose non-positive atlas: %dx%d", atlas_w, atlas_h);
	CHECK(packer.getScale() > 0.0, "packer scale is not positive: %g", packer.getScale());
	CHECK(packer.getPixelRange().lower < packer.getPixelRange().upper, "packer pixel range is degenerate: [%g, %g]", packer.getPixelRange().lower, packer.getPixelRange().upper);

	/* ---- 4. Atlas generation (ImmediateAtlasGenerator, MTSDF) ---------- */
	msdf_atlas::GeneratorAttributes attributes;
	attributes.config.overlapSupport = true;
	attributes.scanlinePass = true;

	msdf_atlas::ImmediateAtlasGenerator<float, 4, msdf_atlas::mtsdfGenerator, msdf_atlas::BitmapAtlasStorage<float, 4>> generator(atlas_w, atlas_h);
	generator.setAttributes(attributes);
	/* Explicit worker pool: exercises Workload.cpp / Threads::Threads link. */
	generator.setThreadCount(2);

	if (loaded > 0 && atlas_w > 0 && atlas_h > 0) {
		generator.generate(glyphs.data(), (int)glyphs.size());
	}

	/* ---- 5. Assert the atlas image artifact ----------------------------- */
	msdfgen::BitmapConstRef<float, 4> bitmap = generator.atlasStorage();
	CHECK(bitmap.pixels != nullptr, "atlas bitmap has no pixels");
	CHECK(bitmap.width == atlas_w && bitmap.height == atlas_h, "atlas bitmap is %dx%d, expected %dx%d", bitmap.width, bitmap.height, atlas_w, atlas_h);
	CHECK(bitmap.width > 0 && bitmap.height > 0, "atlas bitmap dimensions are not positive: %dx%d", bitmap.width, bitmap.height);

	size_t non_zero_pixels = 0;
	if (bitmap.pixels != nullptr) {
		const float* px = bitmap.pixels;
		size_t total = (size_t)bitmap.width * (size_t)bitmap.height * 4u;
		for (size_t i = 0; i < total; ++i) {
			if (px[i] > 0.0f || px[i] < 0.0f) {
				++non_zero_pixels;
			}
		}
	}
	CHECK(non_zero_pixels > 0, "atlas bitmap is entirely zero — no glyph was rendered");

	/* ---- 6. Assert the layout data artifact ----------------------------- */
	const std::vector<msdf_atlas::GlyphBox>& layout = generator.getLayout();
	CHECK(layout.size() == (size_t)loaded, "generator layout has %zu entries, expected %d glyphs", layout.size(), loaded);

	size_t glyphs_with_boxes = 0;
	size_t clipped_glyphs = 0;
	if (layout.size() == (size_t)loaded) {
		for (const msdf_atlas::GlyphBox& box : layout) {
			const msdf_atlas::Rectangle& rect = box.rect;
			if (rect.w > 0 && rect.h > 0) {
				++glyphs_with_boxes;
			}
			if (rect.x < 0 || rect.y < 0 || rect.w < 0 || rect.h < 0 || rect.x + rect.w > atlas_w || rect.y + rect.h > atlas_h) {
				++clipped_glyphs;
			}
		}
	}
	CHECK(glyphs_with_boxes == non_whitespace, "expected %zu non-whitespace glyph boxes, layout has %zu", non_whitespace, glyphs_with_boxes);
	CHECK(clipped_glyphs == 0, "%zu glyph boxes fall outside the %dx%d atlas", clipped_glyphs, atlas_w, atlas_h);

	/* ---- 7. Write the artifacts next to the test binary ----------------- */
	char raw_path[4096];
	char layout_path[4096];
	std::snprintf(raw_path, sizeof(raw_path), "%s/msdf_atlas_smoke.raw", SK_MSDF_SMOKE_ARTIFACT_DIR);
	std::snprintf(layout_path, sizeof(layout_path), "%s/msdf_atlas_smoke_layout.txt", SK_MSDF_SMOKE_ARTIFACT_DIR);

	size_t raw_bytes = (size_t)bitmap.width * (size_t)bitmap.height * 4u * sizeof(float);
	bool raw_ok = bitmap.pixels != nullptr && write_file(raw_path, bitmap.pixels, raw_bytes);
	CHECK(raw_ok, "failed to write atlas image artifact: %s", raw_path);

	std::string layout_text;
	char line[512];
	for (size_t i = 0; i < layout.size(); ++i) {
		const msdf_atlas::GlyphBox& box = layout[i];
		double l = 0, b = 0, r = 0, t = 0;
		if (i < glyphs.size()) {
			glyphs[i].getQuadPlaneBounds(l, b, r, t);
		}
		std::snprintf(line, sizeof(line), "glyph %zu: index=%d advance=%g plane=[%g %g %g %g] rect=%dx%d+%d+%d\n", i, box.index, box.advance, l, b, r, t, box.rect.w, box.rect.h,
					  box.rect.x, box.rect.y);
		layout_text += line;
	}
	bool layout_ok = write_file(layout_path, layout_text.data(), layout_text.size());
	CHECK(layout_ok, "failed to write layout artifact: %s", layout_path);

	/* ---- 8. Tear down ---------------------------------------------------- */
	if (font != nullptr) {
		msdfgen::destroyFont(font);
	}
	if (ft != nullptr) {
		msdfgen::deinitializeFreetype(ft);
	}

	if (g_failures == failures_before) {
		std::printf("msdf-atlas-gen smoke: OK — %u glyphs, atlas %dx%d, "
					"%.2f%% pixels non-zero, layout %zu entries\n",
					glyph_count, atlas_w, atlas_h, 100.0 * (double)non_zero_pixels / (double)((size_t)atlas_w * (size_t)atlas_h * 4u), layout.size());
		std::printf("artifacts: %s\n%s\n", raw_path, layout_path);
		return 0;
	}

	std::fprintf(stderr, "msdf-atlas-gen smoke: FAILED (%d checks)\n", g_failures - failures_before);
	return 1;
}
