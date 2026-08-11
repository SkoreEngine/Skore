/*
 * image_structure.c — structural image assertions beyond pixel sampling (APX-230).
 *
 * Golden-image comparison (image_compare.c) catches exact regressions but
 * requires a committed golden and fails on any pixel difference, which makes
 * it useless for layout drift that keeps colors intact. These helpers assert
 * *structure* so regressions are caught even when no golden is used:
 *
 *   - cpu_image_assert_solid     — a region is uniformly one color (tolerance)
 *   - cpu_image_assert_coverage  — fraction of matching pixels in a range
 *   - cpu_image_find_bbox / assert_bbox — tight bbox of matching pixels,
 *     asserted for position + size (catches drift and misalignment)
 *   - cpu_image_histogram / assert_histogram — dominant colors and their
 *     approximate proportions (catches wrong theming / missing widgets)
 *   - cpu_image_region_hash / assert_region_hash — stable per-region FNV-1a
 *     64-bit hash for cheap change detection
 *
 * Every failure logs the actual measured values (counts, fractions, bboxes,
 * hashes) so a CI log alone is actionable without opening an artifact.
 * Regions are half-open [x0,x1) x [y0,y1) and clamped to the image; a
 * degenerate or clamped-empty region is an ERROR (likely a test bug).
 */

#include "ui_internal.h"

#include "logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Logging                                                                    */
/* -------------------------------------------------------------------------- */

static sk_logger_t* ui_si_logger(void) {
	static sk_logger_t* log = NULL;
	if (log == NULL) {
		log = sk_logger_api()->create_logger("ui-image-structure");
	}
	return log;
}

static void ui_si_fail(const_chr_t fmt, ...) {
	va_list args;
	va_start(args, fmt);
	sk_log_messagev(sk_logger_api(), SK_LOGGER_TYPE_ERROR, ui_si_logger(), fmt, args);
	va_end(args);
}

/* -------------------------------------------------------------------------- */
/* Shared helpers                                                             */
/* -------------------------------------------------------------------------- */

static u32 ui_si_abs_delta(u32 a, u32 b) {
	return a > b ? (a - b) : (b - a);
}

/** Clamp half-open region to image bounds. -1 on invalid input / empty result. */
static i32 ui_si_clamp_region(const sk_ui_cpu_image_t* img, sk_ui_region_t in, sk_ui_region_t* out) {
	if (out == NULL || img == NULL) {
		return -1;
	}
	if (in.x1 <= in.x0 || in.y1 <= in.y0) {
		return -1;
	}
	out->x0 = in.x0 < img->width ? in.x0 : img->width;
	out->y0 = in.y0 < img->height ? in.y0 : img->height;
	out->x1 = in.x1 < img->width ? in.x1 : img->width;
	out->y1 = in.y1 < img->height ? in.y1 : img->height;
	if (out->x1 <= out->x0 || out->y1 <= out->y0) {
		return -1;
	}
	return 0;
}

/** Alpha of a pixel; images without an alpha channel read as 255. */
static u32 ui_si_pixel_alpha(const u8* px, u32 channels) {
	return channels >= 4u ? (u32)px[3] : 255u;
}

/** Channel c (0..2 -> r,g,b; 3 -> a) of a color spec. */
static u32 ui_si_spec_comp(const sk_ui_color_match_t* spec, u32 c) {
	switch (c) {
	case 0u:
		return (u32)spec->r;
	case 1u:
		return (u32)spec->g;
	case 2u:
		return (u32)spec->b;
	default:
		return (u32)spec->a;
	}
}

/** Per-channel tolerance match of a pixel against a color spec. */
static i32 ui_si_pixel_matches(const u8* px, u32 channels, const sk_ui_color_match_t* spec) {
	u32 c;
	for (c = 0u; c < 3u; ++c) {
		if (ui_si_abs_delta((u32)px[c], ui_si_spec_comp(spec, c)) > (u32)spec->tolerance) {
			return 0;
		}
	}
	if (spec->include_alpha != 0 && ui_si_abs_delta(ui_si_pixel_alpha(px, channels), (u32)spec->a) > (u32)spec->tolerance) {
		return 0;
	}
	return 1;
}

/**
 * Validate image / region for the RGB-based assertions and clamp the region.
 * Logs a specific error message and returns -1 on bad input.
 */
static i32 ui_si_validate_image_region(const sk_ui_cpu_image_t* img, sk_ui_region_t region, const_chr_t api, sk_ui_region_t* out_region) {
	if (img == NULL || img->pixels == NULL) {
		ui_si_fail("%s: null image", api);
		return -1;
	}
	if (img->width == 0u || img->height == 0u || img->channels < 3u || img->channels > 4u) {
		ui_si_fail("%s: unsupported image %ux%u channels=%u (need 3 or 4)", api, img->width, img->height, img->channels);
		return -1;
	}
	if (ui_si_clamp_region(img, region, out_region) != 0) {
		ui_si_fail("%s: invalid or empty region [%u,%u]..[%u,%u] for %ux%u image", api, region.x0, region.y0, region.x1, region.y1, img->width, img->height);
		return -1;
	}
	return 0;
}

static i32 ui_si_validate_rgb(const sk_ui_cpu_image_t* img, sk_ui_region_t region, const sk_ui_color_match_t* spec, const_chr_t api, sk_ui_region_t* out_region) {
	if (spec == NULL) {
		ui_si_fail("%s: null color spec", api);
		return -1;
	}
	return ui_si_validate_image_region(img, region, api, out_region);
}

/* -------------------------------------------------------------------------- */
/* Solid-region assertion                                                     */
/* -------------------------------------------------------------------------- */

i32 ui_cpu_image_assert_solid_impl(const sk_ui_cpu_image_t* img, sk_ui_region_t region, const sk_ui_color_match_t* color, sk_ui_solid_stats_t* out_stats) {
	sk_ui_solid_stats_t local;
	sk_ui_solid_stats_t* stats = (out_stats != NULL) ? out_stats : &local;
	sk_ui_region_t r;
	u32 x;
	u32 y;
	i32 bbox_init = 0;

	memset(stats, 0, sizeof(*stats));
	if (ui_si_validate_rgb(img, region, color, "cpu_image_assert_solid", &r) != 0) {
		return SK_UI_IMAGE_ASSERT_ERROR;
	}

	stats->region_pixels = (r.x1 - r.x0) * (r.y1 - r.y0);
	for (y = r.y0; y < r.y1; ++y) {
		for (x = r.x0; x < r.x1; ++x) {
			const u8* px = img->pixels + ((size_t)y * img->width + x) * img->channels;
			u32 c;
			u32 maxd = 0u;
			for (c = 0u; c < 3u; ++c) {
				u32 d = ui_si_abs_delta((u32)px[c], ui_si_spec_comp(color, c));
				if (d > maxd) {
					maxd = d;
				}
			}
			if (color->include_alpha != 0) {
				u32 d = ui_si_abs_delta(ui_si_pixel_alpha(px, img->channels), (u32)color->a);
				if (d > maxd) {
					maxd = d;
				}
			}
			if (maxd > stats->max_channel_delta) {
				stats->max_channel_delta = maxd;
			}
			if (maxd > (u32)color->tolerance) {
				stats->nonmatching += 1u;
				if (bbox_init == 0) {
					stats->bad_bbox.min_x = x;
					stats->bad_bbox.min_y = y;
					stats->bad_bbox.max_x = x;
					stats->bad_bbox.max_y = y;
					bbox_init = 1;
				} else {
					if (x < stats->bad_bbox.min_x) {
						stats->bad_bbox.min_x = x;
					}
					if (y < stats->bad_bbox.min_y) {
						stats->bad_bbox.min_y = y;
					}
					if (x > stats->bad_bbox.max_x) {
						stats->bad_bbox.max_x = x;
					}
					if (y > stats->bad_bbox.max_y) {
						stats->bad_bbox.max_y = y;
					}
				}
			}
		}
	}
	stats->bad_bbox.pixel_count = stats->nonmatching;
	stats->bad_bbox.found = stats->nonmatching != 0u ? 1 : 0;
	stats->nonmatching_fraction = stats->region_pixels > 0u ? (f32)stats->nonmatching / (f32)stats->region_pixels : 0.0f;

	if (stats->nonmatching != 0u) {
		ui_si_fail("cpu_image_assert_solid: FAIL — region=[%u,%u]..[%u,%u] expected=rgba(%u,%u,%u,%u)±%u "
				   "nonmatching=%u/%u (%.2f%%) max_channel_delta=%u bad_bbox=[%u,%u]..[%u,%u]",
				   r.x0, r.y0, r.x1 - 1u, r.y1 - 1u, color->r, color->g, color->b, color->a, (u32)color->tolerance, stats->nonmatching, stats->region_pixels,
				   (f64)stats->nonmatching_fraction * 100.0, stats->max_channel_delta, stats->bad_bbox.min_x, stats->bad_bbox.min_y, stats->bad_bbox.max_x, stats->bad_bbox.max_y);
		return SK_UI_IMAGE_ASSERT_FAIL;
	}
	return SK_UI_IMAGE_ASSERT_OK;
}

/* -------------------------------------------------------------------------- */
/* Coverage assertion                                                         */
/* -------------------------------------------------------------------------- */

i32 ui_cpu_image_assert_coverage_impl(const sk_ui_cpu_image_t* img, sk_ui_region_t region, const sk_ui_color_match_t* color, f32 min_fraction, f32 max_fraction,
									  sk_ui_coverage_stats_t* out_stats) {
	sk_ui_coverage_stats_t local;
	sk_ui_coverage_stats_t* stats = (out_stats != NULL) ? out_stats : &local;
	sk_ui_region_t r;
	f32 min_f;
	f32 max_f;
	u32 x;
	u32 y;

	memset(stats, 0, sizeof(*stats));
	if (ui_si_validate_rgb(img, region, color, "cpu_image_assert_coverage", &r) != 0) {
		return SK_UI_IMAGE_ASSERT_ERROR;
	}
	min_f = min_fraction < 0.0f ? 0.0f : (min_fraction > 1.0f ? 1.0f : min_fraction);
	max_f = max_fraction < 0.0f ? 0.0f : (max_fraction > 1.0f ? 1.0f : max_fraction);
	if (min_f > max_f) {
		ui_si_fail("cpu_image_assert_coverage: invalid range [%.3f, %.3f]", (f64)min_f, (f64)max_f);
		return SK_UI_IMAGE_ASSERT_ERROR;
	}

	stats->region_pixels = (r.x1 - r.x0) * (r.y1 - r.y0);
	for (y = r.y0; y < r.y1; ++y) {
		for (x = r.x0; x < r.x1; ++x) {
			const u8* px = img->pixels + ((size_t)y * img->width + x) * img->channels;
			if (ui_si_pixel_matches(px, img->channels, color) != 0) {
				stats->matching += 1u;
			}
		}
	}
	stats->coverage = stats->region_pixels > 0u ? (f32)stats->matching / (f32)stats->region_pixels : 0.0f;

	if (stats->coverage < min_f - 1e-6f || stats->coverage > max_f + 1e-6f) {
		ui_si_fail("cpu_image_assert_coverage: FAIL — region=[%u,%u]..[%u,%u] color=rgba(%u,%u,%u,%u)±%u "
				   "expected_fraction=[%.3f,%.3f] measured=%.4f (%u/%u pixels)",
				   r.x0, r.y0, r.x1 - 1u, r.y1 - 1u, color->r, color->g, color->b, color->a, (u32)color->tolerance, (f64)min_f, (f64)max_f, (f64)stats->coverage, stats->matching,
				   stats->region_pixels);
		return SK_UI_IMAGE_ASSERT_FAIL;
	}
	return SK_UI_IMAGE_ASSERT_OK;
}

/* -------------------------------------------------------------------------- */
/* Bounding-box extraction + assertion                                        */
/* -------------------------------------------------------------------------- */

i32 ui_cpu_image_find_bbox_impl(const sk_ui_cpu_image_t* img, sk_ui_region_t region, const sk_ui_color_match_t* color, sk_ui_bbox_t* out_bbox) {
	sk_ui_region_t r;
	u32 x;
	u32 y;
	i32 bbox_init = 0;

	if (out_bbox == NULL) {
		ui_si_fail("cpu_image_find_bbox: null out_bbox");
		return SK_UI_IMAGE_ASSERT_ERROR;
	}
	memset(out_bbox, 0, sizeof(*out_bbox));
	if (ui_si_validate_rgb(img, region, color, "cpu_image_find_bbox", &r) != 0) {
		return SK_UI_IMAGE_ASSERT_ERROR;
	}

	for (y = r.y0; y < r.y1; ++y) {
		for (x = r.x0; x < r.x1; ++x) {
			const u8* px = img->pixels + ((size_t)y * img->width + x) * img->channels;
			if (ui_si_pixel_matches(px, img->channels, color) == 0) {
				continue;
			}
			out_bbox->pixel_count += 1u;
			if (bbox_init == 0) {
				out_bbox->min_x = x;
				out_bbox->min_y = y;
				out_bbox->max_x = x;
				out_bbox->max_y = y;
				bbox_init = 1;
			} else {
				if (x < out_bbox->min_x) {
					out_bbox->min_x = x;
				}
				if (y < out_bbox->min_y) {
					out_bbox->min_y = y;
				}
				if (x > out_bbox->max_x) {
					out_bbox->max_x = x;
				}
				if (y > out_bbox->max_y) {
					out_bbox->max_y = y;
				}
			}
		}
	}
	out_bbox->found = bbox_init;
	return 0;
}

i32 ui_cpu_image_assert_bbox_impl(const sk_ui_cpu_image_t* img, sk_ui_region_t region, const sk_ui_color_match_t* color, const sk_ui_bbox_expected_t* expected,
								  sk_ui_bbox_assert_stats_t* out_stats) {
	sk_ui_bbox_assert_stats_t local;
	sk_ui_bbox_assert_stats_t* stats = (out_stats != NULL) ? out_stats : &local;
	i32 fail = 0;

	memset(stats, 0, sizeof(*stats));
	if (expected == NULL) {
		ui_si_fail("cpu_image_assert_bbox: null expected");
		return SK_UI_IMAGE_ASSERT_ERROR;
	}
	stats->expected_min_x = expected->min_x;
	stats->expected_min_y = expected->min_y;
	stats->expected_max_x = expected->max_x;
	stats->expected_max_y = expected->max_y;
	stats->expected_min_pixels = expected->min_pixels;
	stats->expect_empty = expected->expect_empty;

	/* Keep the region/spec validation error path identical to the other APIs. */
	{
		sk_ui_region_t r;
		if (ui_si_validate_rgb(img, region, color, "cpu_image_assert_bbox", &r) != 0) {
			return SK_UI_IMAGE_ASSERT_ERROR;
		}
	}
	if (ui_cpu_image_find_bbox_impl(img, region, color, &stats->actual) != 0) {
		return SK_UI_IMAGE_ASSERT_ERROR;
	}

	if (expected->expect_empty != 0) {
		if (stats->actual.found != 0) {
			stats->empty_mismatch = 1;
			fail = 1;
		}
	} else {
		if (stats->actual.found == 0) {
			stats->missing = 1;
			fail = 1;
		} else {
			u32 exp_w = expected->max_x >= expected->min_x ? expected->max_x - expected->min_x : 0u;
			u32 exp_h = expected->max_y >= expected->min_y ? expected->max_y - expected->min_y : 0u;
			u32 act_w = stats->actual.max_x >= stats->actual.min_x ? stats->actual.max_x - stats->actual.min_x : 0u;
			u32 act_h = stats->actual.max_y >= stats->actual.min_y ? stats->actual.max_y - stats->actual.min_y : 0u;

			stats->delta_x = ui_si_abs_delta(stats->actual.min_x, expected->min_x);
			stats->delta_y = ui_si_abs_delta(stats->actual.min_y, expected->min_y);
			stats->delta_w = ui_si_abs_delta(act_w, exp_w);
			stats->delta_h = ui_si_abs_delta(act_h, exp_h);

			if (expected->min_pixels != 0u && stats->actual.pixel_count < expected->min_pixels) {
				stats->min_pixels_fail = 1;
				fail = 1;
			}
			if (stats->delta_x > expected->position_tolerance || stats->delta_y > expected->position_tolerance) {
				stats->position_mismatch = 1;
				fail = 1;
			}
			if (stats->delta_w > expected->size_tolerance || stats->delta_h > expected->size_tolerance) {
				stats->size_mismatch = 1;
				fail = 1;
			}
		}
	}

	if (fail != 0) {
		ui_si_fail("cpu_image_assert_bbox: FAIL — region=[%u,%u]..[%u,%u] color=rgba(%u,%u,%u,%u)±%u "
				   "expected=[%u,%u]..[%u,%u] pos_tol=%u size_tol=%u min_pixels=%u expect_empty=%d "
				   "actual=[%u,%u]..[%u,%u] pixels=%u found=%d "
				   "pos_delta=(%u,%u) size_delta=(%u,%u) empty_mismatch=%d missing=%d min_pixels_fail=%d",
				   region.x0, region.y0, region.x1 - 1u, region.y1 - 1u, color->r, color->g, color->b, color->a, (u32)color->tolerance, expected->min_x, expected->min_y,
				   expected->max_x, expected->max_y, expected->position_tolerance, expected->size_tolerance, expected->min_pixels, expected->expect_empty, stats->actual.min_x,
				   stats->actual.min_y, stats->actual.max_x, stats->actual.max_y, stats->actual.pixel_count, stats->actual.found, stats->delta_x, stats->delta_y, stats->delta_w,
				   stats->delta_h, stats->empty_mismatch, stats->missing, stats->min_pixels_fail);
		return SK_UI_IMAGE_ASSERT_FAIL;
	}
	return SK_UI_IMAGE_ASSERT_OK;
}

/* -------------------------------------------------------------------------- */
/* Dominant-color histogram                                                   */
/* -------------------------------------------------------------------------- */

#define UI_SI_MAX_BUCKETS 256

typedef struct ui_si_bucket_t {
	u8 r;
	u8 g;
	u8 b;
	u8 a;
	u32 count;
} ui_si_bucket_t;

/** Whether a pixel is within @p tol of the bucket's representative color. */
static i32 ui_si_bucket_matches(const ui_si_bucket_t* b, const u8* px, u32 channels, u8 tol) {
	u32 c;
	for (c = 0u; c < 3u; ++c) {
		if (ui_si_abs_delta((u32)px[c], c == 0u ? (u32)b->r : (c == 1u ? (u32)b->g : (u32)b->b)) > (u32)tol) {
			return 0;
		}
	}
	if (channels >= 4u && ui_si_abs_delta((u32)px[3], (u32)b->a) > (u32)tol) {
		return 0;
	}
	return 1;
}

/** Sum of per-channel deltas to the bucket representative (merge distance). */
static u32 ui_si_bucket_dist(const ui_si_bucket_t* b, const u8* px, u32 channels) {
	u32 c;
	u32 d = 0u;
	for (c = 0u; c < 3u; ++c) {
		d += ui_si_abs_delta((u32)px[c], c == 0u ? (u32)b->r : (c == 1u ? (u32)b->g : (u32)b->b));
	}
	if (channels >= 4u) {
		d += ui_si_abs_delta((u32)px[3], (u32)b->a);
	}
	return d;
}

static int ui_si_bucket_cmp(const void* a, const void* b) {
	const ui_si_bucket_t* ba = (const ui_si_bucket_t*)a;
	const ui_si_bucket_t* bb = (const ui_si_bucket_t*)b;
	if (ba->count != bb->count) {
		return ba->count > bb->count ? -1 : 1;
	}
	/* Tie-break ascending RGBA so results are deterministic. */
	if (ba->r != bb->r) {
		return ba->r < bb->r ? -1 : 1;
	}
	if (ba->g != bb->g) {
		return ba->g < bb->g ? -1 : 1;
	}
	if (ba->b != bb->b) {
		return ba->b < bb->b ? -1 : 1;
	}
	if (ba->a != bb->a) {
		return ba->a < bb->a ? -1 : 1;
	}
	return 0;
}

i32 ui_cpu_image_histogram_impl(const sk_ui_cpu_image_t* img, sk_ui_region_t region, u8 merge_tolerance, u32 max_entries, sk_ui_color_histogram_t* out_hist) {
	sk_ui_region_t r;
	ui_si_bucket_t buckets[UI_SI_MAX_BUCKETS];
	ui_si_bucket_t sorted[UI_SI_MAX_BUCKETS];
	u32 used = 0u;
	u32 x;
	u32 y;
	u32 i;

	if (out_hist == NULL) {
		ui_si_fail("cpu_image_histogram: null out_hist");
		return SK_UI_IMAGE_ASSERT_ERROR;
	}
	memset(out_hist, 0, sizeof(*out_hist));
	if (ui_si_validate_image_region(img, region, "cpu_image_histogram", &r) != 0) {
		return SK_UI_IMAGE_ASSERT_ERROR;
	}

	memset(buckets, 0, sizeof(buckets));
	for (y = r.y0; y < r.y1; ++y) {
		for (x = r.x0; x < r.x1; ++x) {
			const u8* px = img->pixels + ((size_t)y * img->width + x) * img->channels;
			u32 best = 0u;
			u32 best_d = 0xFFFFFFFFu;
			i32 found = 0;
			for (i = 0u; i < used; ++i) {
				if (ui_si_bucket_matches(&buckets[i], px, img->channels, merge_tolerance) != 0) {
					buckets[i].count += 1u;
					found = 1;
					break;
				}
				{
					u32 d = ui_si_bucket_dist(&buckets[i], px, img->channels);
					if (d < best_d) {
						best_d = d;
						best = i;
					}
				}
			}
			if (found != 0) {
				continue;
			}
			if (used < UI_SI_MAX_BUCKETS) {
				/* New bucket; representative is the first pixel seen. */
				buckets[used].r = px[0];
				buckets[used].g = px[1];
				buckets[used].b = px[2];
				buckets[used].a = img->channels >= 4u ? px[3] : 255u;
				buckets[used].count = 1u;
				used += 1u;
				continue;
			}
			/* Bucket table full: merge into the nearest representative. */
			buckets[best].count += 1u;
		}
	}

	/* Sort used buckets by count descending (deterministic tie-break). */
	memcpy(sorted, buckets, sizeof(sorted));
	qsort(sorted, used, sizeof(sorted[0]), ui_si_bucket_cmp);

	if (max_entries == 0u || max_entries > SK_UI_COLOR_HIST_MAX_ENTRIES) {
		max_entries = SK_UI_COLOR_HIST_MAX_ENTRIES;
	}
	out_hist->total_pixels = (r.x1 - r.x0) * (r.y1 - r.y0);
	out_hist->entry_count = used < max_entries ? used : max_entries;
	for (i = 0u; i < out_hist->entry_count; ++i) {
		sk_ui_color_hist_entry_t* e = &out_hist->entries[i];
		e->r = sorted[i].r;
		e->g = sorted[i].g;
		e->b = sorted[i].b;
		e->a = sorted[i].a;
		e->count = sorted[i].count;
		e->fraction = out_hist->total_pixels > 0u ? (f32)sorted[i].count / (f32)out_hist->total_pixels : 0.0f;
	}
	return 0;
}

i32 ui_cpu_image_assert_histogram_impl(const sk_ui_cpu_image_t* img, sk_ui_region_t region, const sk_ui_hist_assert_params_t* params, const sk_ui_hist_expectation_t* expected,
									   u32 expected_count, sk_ui_hist_assert_stats_t* out_stats) {
	sk_ui_hist_assert_stats_t local;
	sk_ui_hist_assert_stats_t* stats = (out_stats != NULL) ? out_stats : &local;
	sk_ui_hist_assert_params_t def;
	const sk_ui_hist_assert_params_t* p;
	u8 claimed[SK_UI_COLOR_HIST_MAX_ENTRIES];
	u32 max_entries;
	u8 merge_tol;
	f32 max_unexpected;
	char msg[2048];
	u32 msg_len = 0u;
	u32 i;
	i32 fail = 0;

	memset(stats, 0, sizeof(*stats));
	if (expected == NULL && expected_count != 0u) {
		ui_si_fail("cpu_image_assert_histogram: null expected with count=%u", expected_count);
		return SK_UI_IMAGE_ASSERT_ERROR;
	}
	if (expected_count > SK_UI_COLOR_HIST_MAX_ENTRIES) {
		ui_si_fail("cpu_image_assert_histogram: expected_count=%u exceeds max %u", expected_count, (u32)SK_UI_COLOR_HIST_MAX_ENTRIES);
		return SK_UI_IMAGE_ASSERT_ERROR;
	}

	memset(&def, 0, sizeof(def));
	def.max_entries = SK_UI_COLOR_HIST_MAX_ENTRIES;
	def.merge_tolerance = 16u;
	def.max_unexpected_fraction = 0.05f;
	p = (params != NULL) ? params : &def;
	max_entries = p->max_entries == 0u || p->max_entries > SK_UI_COLOR_HIST_MAX_ENTRIES ? (u32)SK_UI_COLOR_HIST_MAX_ENTRIES : p->max_entries;
	merge_tol = p->merge_tolerance;
	max_unexpected = p->max_unexpected_fraction < 0.0f ? 0.0f : p->max_unexpected_fraction;
	if (max_entries < expected_count) {
		max_entries = expected_count;
	}

	/* Validate expectations up front (clamp fractions; reject inverted ranges). */
	for (i = 0u; i < expected_count; ++i) {
		f32 mn = expected[i].min_fraction < 0.0f ? 0.0f : (expected[i].min_fraction > 1.0f ? 1.0f : expected[i].min_fraction);
		f32 mx = expected[i].max_fraction < 0.0f ? 0.0f : (expected[i].max_fraction > 1.0f ? 1.0f : expected[i].max_fraction);
		if (mn > mx) {
			ui_si_fail("cpu_image_assert_histogram: expectation %u has inverted fraction range [%.3f, %.3f]", i, (f64)mn, (f64)mx);
			return SK_UI_IMAGE_ASSERT_ERROR;
		}
	}

	if (ui_cpu_image_histogram_impl(img, region, merge_tol, max_entries, &stats->actual) != 0) {
		return SK_UI_IMAGE_ASSERT_ERROR;
	}

	stats->expected_count = expected_count;
	memset(claimed, 0, sizeof(claimed));

	/* Greedy claim: each expected color takes the measured entries within its
	 * own tolerance (first-come order); entries no expectation claims are
	 * "unexpected". */
	for (i = 0u; i < expected_count; ++i) {
		f32 mn = expected[i].min_fraction < 0.0f ? 0.0f : (expected[i].min_fraction > 1.0f ? 1.0f : expected[i].min_fraction);
		f32 mx = expected[i].max_fraction < 0.0f ? 0.0f : (expected[i].max_fraction > 1.0f ? 1.0f : expected[i].max_fraction);
		u32 m;
		for (m = 0u; m < stats->actual.entry_count; ++m) {
			if (claimed[m] != 0u) {
				continue;
			}
			{
				const sk_ui_color_hist_entry_t* e = &stats->actual.entries[m];
				sk_ui_color_match_t mc = expected[i].color;
				if (ui_si_pixel_matches(&e->r, 4u, &mc) == 0) {
					continue;
				}
			}
			claimed[m] = 1u;
			stats->measured_count[i] += stats->actual.entries[m].count;
			stats->measured_fraction[i] += stats->actual.entries[m].fraction;
		}
		if (stats->measured_count[i] == 0u) {
			stats->missing_count += 1u;
			fail = 1;
		} else if (stats->measured_fraction[i] < mn - 1e-6f || stats->measured_fraction[i] > mx + 1e-6f) {
			stats->out_of_range_count += 1u;
			fail = 1;
		}
	}
	for (i = 0u; i < stats->actual.entry_count; ++i) {
		if (claimed[i] == 0u) {
			stats->unexpected_count += 1u;
			stats->unexpected_fraction += stats->actual.entries[i].fraction;
		}
	}
	if (stats->unexpected_fraction > max_unexpected + 1e-6f) {
		fail = 1;
	}

	if (fail == 0) {
		return SK_UI_IMAGE_ASSERT_OK;
	}

	/* Failure message: per-expected measured values + measured dominants. */
	msg_len += (u32)snprintf(msg + msg_len, sizeof(msg) - msg_len, "cpu_image_assert_histogram: FAIL — expected=%u colors:", expected_count);
	for (i = 0u; i < expected_count && msg_len + 220u < sizeof(msg); ++i) {
		f32 mn = expected[i].min_fraction < 0.0f ? 0.0f : (expected[i].min_fraction > 1.0f ? 1.0f : expected[i].min_fraction);
		f32 mx = expected[i].max_fraction < 0.0f ? 0.0f : (expected[i].max_fraction > 1.0f ? 1.0f : expected[i].max_fraction);
		const_chr_t tag = stats->measured_count[i] == 0u ? " MISSING" :
														   (stats->measured_fraction[i] < mn - 1e-6f || stats->measured_fraction[i] > mx + 1e-6f ? " OUT_OF_RANGE" : "");
		msg_len += (u32)snprintf(msg + msg_len, sizeof(msg) - msg_len, " [%u]=rgba(%u,%u,%u,%u) range=[%.3f,%.3f] measured=%.4f (%u)%s;", i, expected[i].color.r,
								 expected[i].color.g, expected[i].color.b, expected[i].color.a, (f64)mn, (f64)mx, (f64)stats->measured_fraction[i], stats->measured_count[i], tag);
	}
	msg_len += (u32)snprintf(msg + msg_len, sizeof(msg) - msg_len, " missing=%u out_of_range=%u unexpected=%u (%.2f%%) dominant:", stats->missing_count, stats->out_of_range_count,
							 stats->unexpected_count, (f64)stats->unexpected_fraction * 100.0);
	for (i = 0u; i < stats->actual.entry_count && msg_len + 120u < sizeof(msg); ++i) {
		msg_len += (u32)snprintf(msg + msg_len, sizeof(msg) - msg_len, " [%u]=rgba(%u,%u,%u,%u) %.2f%% (%u);", i, stats->actual.entries[i].r, stats->actual.entries[i].g,
								 stats->actual.entries[i].b, stats->actual.entries[i].a, (f64)stats->actual.entries[i].fraction * 100.0, stats->actual.entries[i].count);
	}
	ui_si_fail("%s", msg);
	return SK_UI_IMAGE_ASSERT_FAIL;
}

/* -------------------------------------------------------------------------- */
/* Stable per-region hash                                                     */
/* -------------------------------------------------------------------------- */

#define UI_SI_FNV_OFFSET_BASIS 0xcbf29ce484222325ULL
#define UI_SI_FNV_PRIME 0x100000001b3ULL

static void ui_si_hash_fold_u32(u64* h, u32 v) {
	*h = (*h ^ (u64)(v & 0xffu)) * UI_SI_FNV_PRIME;
	*h = (*h ^ (u64)((v >> 8) & 0xffu)) * UI_SI_FNV_PRIME;
	*h = (*h ^ (u64)((v >> 16) & 0xffu)) * UI_SI_FNV_PRIME;
	*h = (*h ^ (u64)((v >> 24) & 0xffu)) * UI_SI_FNV_PRIME;
}

i32 ui_cpu_image_region_hash_impl(const sk_ui_cpu_image_t* img, sk_ui_region_t region, u64 seed, u64* out_hash) {
	sk_ui_region_t r;
	u64 h;
	u32 x;
	u32 y;

	if (out_hash == NULL) {
		ui_si_fail("cpu_image_region_hash: null out_hash");
		return SK_UI_IMAGE_ASSERT_ERROR;
	}
	if (img == NULL || img->pixels == NULL) {
		ui_si_fail("cpu_image_region_hash: null image");
		return SK_UI_IMAGE_ASSERT_ERROR;
	}
	if (img->width == 0u || img->height == 0u || img->channels == 0u || img->channels > 4u) {
		ui_si_fail("cpu_image_region_hash: unsupported image %ux%u channels=%u", img->width, img->height, img->channels);
		return SK_UI_IMAGE_ASSERT_ERROR;
	}
	if (ui_si_clamp_region(img, region, &r) != 0) {
		ui_si_fail("cpu_image_region_hash: invalid or empty region [%u,%u]..[%u,%u] for %ux%u image", region.x0, region.y0, region.x1, region.y1, img->width, img->height);
		return SK_UI_IMAGE_ASSERT_ERROR;
	}

	h = UI_SI_FNV_OFFSET_BASIS ^ seed;
	ui_si_hash_fold_u32(&h, img->width);
	ui_si_hash_fold_u32(&h, img->height);
	ui_si_hash_fold_u32(&h, img->channels);
	for (y = r.y0; y < r.y1; ++y) {
		for (x = r.x0; x < r.x1; ++x) {
			const u8* px = img->pixels + ((size_t)y * img->width + x) * img->channels;
			u32 c;
			for (c = 0u; c < img->channels; ++c) {
				h = (h ^ (u64)px[c]) * UI_SI_FNV_PRIME;
			}
		}
	}
	*out_hash = h;
	return 0;
}

i32 ui_cpu_image_assert_region_hash_impl(const sk_ui_cpu_image_t* img, sk_ui_region_t region, u64 expected_hash, u64* out_actual_hash) {
	u64 actual = 0u;
	if (ui_cpu_image_region_hash_impl(img, region, 0u, &actual) != 0) {
		return SK_UI_IMAGE_ASSERT_ERROR;
	}
	if (out_actual_hash != NULL) {
		*out_actual_hash = actual;
	}
	if (actual != expected_hash) {
		ui_si_fail("cpu_image_assert_region_hash: FAIL — expected=0x%016llx actual=0x%016llx", expected_hash, actual);
		return SK_UI_IMAGE_ASSERT_FAIL;
	}
	return SK_UI_IMAGE_ASSERT_OK;
}

/* -------------------------------------------------------------------------- */
/* Unit tests — synthetic images (pass + near-miss fail per assertion)        */
/* -------------------------------------------------------------------------- */

#ifdef SK_TESTS

#include "test.h"

static const sk_ui_api_t* ui_si_test_api(void) {
	return ui_get_api_table();
}

static sk_ui_cpu_image_t ui_si_make_image(u8* px, u32 w, u32 h, u32 channels) {
	sk_ui_cpu_image_t img;
	memset(&img, 0, sizeof(img));
	img.width = w;
	img.height = h;
	img.channels = channels;
	img.pixels = px;
	return img;
}

/** Fill [x0,x1) x [y0,y1) with a color; region outside is left untouched. */
static void ui_si_fill_rect(u8* px, u32 w, u32 h, u32 ch, u32 x0, u32 y0, u32 x1, u32 y1, u8 r, u8 g, u8 b, u8 a) {
	(void)h;
	u32 x;
	u32 y;
	for (y = y0; y < y1; ++y) {
		for (x = x0; x < x1; ++x) {
			u8* p = px + ((size_t)y * w + x) * ch;
			p[0] = r;
			p[1] = g;
			if (ch >= 3u) {
				p[2] = b;
			}
			if (ch >= 4u) {
				p[3] = a;
			}
		}
	}
}

static void ui_si_fill_solid(u8* px, u32 w, u32 h, u32 ch, u8 r, u8 g, u8 b, u8 a) {
	ui_si_fill_rect(px, w, h, ch, 0u, 0u, w, h, r, g, b, a);
}

SK_TEST(ui_image_structure_solid_pass_and_near_miss) {
	const sk_ui_api_t* ui = ui_si_test_api();
	u8 px[8u * 8u * 4u];
	sk_ui_cpu_image_t img;
	sk_ui_color_match_t color;
	sk_ui_solid_stats_t stats;
	sk_ui_region_t region;
	i32 rc;

	ui_si_fill_solid(px, 8u, 8u, 4u, 0u, 0u, 0u, 255u);
	ui_si_fill_rect(px, 8u, 8u, 4u, 2u, 2u, 6u, 6u, 40u, 80u, 120u, 255u);
	img = ui_si_make_image(px, 8u, 8u, 4u);
	region.x0 = 2u;
	region.y0 = 2u;
	region.x1 = 6u;
	region.y1 = 6u;
	color.r = 40u;
	color.g = 80u;
	color.b = 120u;
	color.a = 255u;
	color.tolerance = 0u;
	color.include_alpha = 1;

	/* Passing case: uniform fill. */
	rc = ui->cpu_image_assert_solid(&img, region, &color, &stats);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, rc);
	TEST_ASSERT_EQUAL_UINT(16u, stats.region_pixels);
	TEST_ASSERT_EQUAL_UINT(0u, stats.nonmatching);
	TEST_ASSERT_EQUAL_UINT(0u, stats.max_channel_delta);
	TEST_ASSERT_EQUAL_INT(0, stats.bad_bbox.found);

	/* Near-miss: single pixel off by 1 in R, tolerance 0. */
	px[(3u * 8u + 3u) * 4u + 0u] = 41u;
	rc = ui->cpu_image_assert_solid(&img, region, &color, &stats);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_FAIL, rc);
	TEST_ASSERT_EQUAL_UINT(1u, stats.nonmatching);
	TEST_ASSERT_EQUAL_UINT(1u, stats.max_channel_delta);
	TEST_ASSERT_EQUAL_UINT(3u, stats.bad_bbox.min_x);
	TEST_ASSERT_EQUAL_UINT(3u, stats.bad_bbox.min_y);
	TEST_ASSERT_EQUAL_UINT(3u, stats.bad_bbox.max_x);
	TEST_ASSERT_EQUAL_UINT(3u, stats.bad_bbox.max_y);
	TEST_ASSERT_TRUE(stats.nonmatching_fraction > 0.05f && stats.nonmatching_fraction < 0.07f);

	/* Same defect accepted when tolerance covers it. */
	color.tolerance = 1u;
	rc = ui->cpu_image_assert_solid(&img, region, &color, &stats);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, rc);
}

SK_TEST(ui_image_structure_coverage_pass_and_near_miss) {
	const sk_ui_api_t* ui = ui_si_test_api();
	u8 px[8u * 8u * 4u];
	sk_ui_cpu_image_t img;
	sk_ui_color_match_t red;
	sk_ui_region_t region;
	sk_ui_coverage_stats_t stats;
	i32 rc;

	/* Red 6x6 block (36 px = 56.25%), rest blue. */
	ui_si_fill_solid(px, 8u, 8u, 4u, 0u, 0u, 255u, 255u);
	ui_si_fill_rect(px, 8u, 8u, 4u, 0u, 0u, 6u, 6u, 255u, 0u, 0u, 255u);
	img = ui_si_make_image(px, 8u, 8u, 4u);
	region.x0 = 0u;
	region.y0 = 0u;
	region.x1 = 8u;
	region.y1 = 8u;
	red.r = 255u;
	red.g = 0u;
	red.b = 0u;
	red.a = 255u;
	red.tolerance = 0u;
	red.include_alpha = 0;

	/* Passing case: measured 0.5625 inside [0.50, 0.60]. */
	rc = ui->cpu_image_assert_coverage(&img, region, &red, 0.50f, 0.60f, &stats);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, rc);
	TEST_ASSERT_EQUAL_UINT(36u, stats.matching);
	TEST_ASSERT_EQUAL_UINT(64u, stats.region_pixels);
	TEST_ASSERT_TRUE(stats.coverage > 0.56f && stats.coverage < 0.57f);

	/* Near-miss below the lower bound. */
	rc = ui->cpu_image_assert_coverage(&img, region, &red, 0.57f, 0.60f, &stats);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_FAIL, rc);
	TEST_ASSERT_TRUE(stats.coverage > 0.56f && stats.coverage < 0.57f);

	/* Near-miss above the upper bound. */
	rc = ui->cpu_image_assert_coverage(&img, region, &red, 0.30f, 0.50f, &stats);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_FAIL, rc);
	TEST_ASSERT_EQUAL_UINT(36u, stats.matching);
}

SK_TEST(ui_image_structure_bbox_extract_and_assert) {
	const sk_ui_api_t* ui = ui_si_test_api();
	u8 px[16u * 16u * 4u];
	u8 empty_px[16u * 16u * 4u];
	sk_ui_cpu_image_t img;
	sk_ui_cpu_image_t empty_img;
	sk_ui_color_match_t blue;
	sk_ui_region_t region;
	sk_ui_bbox_t bbox;
	sk_ui_bbox_expected_t expected;
	sk_ui_bbox_assert_stats_t stats;
	i32 rc;

	/* Blue rect (4,3)..(11,9) inclusive = 8x7 = 56 px on black. */
	ui_si_fill_solid(px, 16u, 16u, 4u, 0u, 0u, 0u, 255u);
	ui_si_fill_rect(px, 16u, 16u, 4u, 4u, 3u, 12u, 10u, 0u, 0u, 255u, 255u);
	img = ui_si_make_image(px, 16u, 16u, 4u);
	ui_si_fill_solid(empty_px, 16u, 16u, 4u, 0u, 0u, 0u, 255u);
	empty_img = ui_si_make_image(empty_px, 16u, 16u, 4u);
	region.x0 = 0u;
	region.y0 = 0u;
	region.x1 = 16u;
	region.y1 = 16u;
	blue.r = 0u;
	blue.g = 0u;
	blue.b = 255u;
	blue.a = 255u;
	blue.tolerance = 0u;
	blue.include_alpha = 0;

	/* Extraction: exact tight bbox. */
	rc = ui->cpu_image_find_bbox(&img, region, &blue, &bbox);
	TEST_ASSERT_EQUAL_INT(0, rc);
	TEST_ASSERT_EQUAL_INT(1, bbox.found);
	TEST_ASSERT_EQUAL_UINT(4u, bbox.min_x);
	TEST_ASSERT_EQUAL_UINT(3u, bbox.min_y);
	TEST_ASSERT_EQUAL_UINT(11u, bbox.max_x);
	TEST_ASSERT_EQUAL_UINT(9u, bbox.max_y);
	TEST_ASSERT_EQUAL_UINT(56u, bbox.pixel_count);

	/* Assert: exact match passes. */
	memset(&expected, 0, sizeof(expected));
	expected.min_x = 4u;
	expected.min_y = 3u;
	expected.max_x = 11u;
	expected.max_y = 9u;
	expected.min_pixels = 56u;
	rc = ui->cpu_image_assert_bbox(&img, region, &blue, &expected, &stats);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, rc);
	TEST_ASSERT_EQUAL_UINT(0u, stats.delta_x);
	TEST_ASSERT_EQUAL_UINT(0u, stats.delta_w);

	/* Near-miss: expected shifted 1 px right, zero position tolerance. */
	expected.min_x = 5u;
	expected.max_x = 12u;
	rc = ui->cpu_image_assert_bbox(&img, region, &blue, &expected, &stats);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_FAIL, rc);
	TEST_ASSERT_EQUAL_INT(1, stats.position_mismatch);
	TEST_ASSERT_EQUAL_UINT(1u, stats.delta_x);
	TEST_ASSERT_EQUAL_UINT(0u, stats.delta_y);

	/* Same shift accepted with position tolerance 2. */
	expected.position_tolerance = 2u;
	rc = ui->cpu_image_assert_bbox(&img, region, &blue, &expected, &stats);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, rc);

	/* Near-miss: size off by one column, zero size tolerance. */
	memset(&expected, 0, sizeof(expected));
	expected.min_x = 4u;
	expected.min_y = 3u;
	expected.max_x = 10u;
	expected.max_y = 9u;
	expected.min_pixels = 1u;
	rc = ui->cpu_image_assert_bbox(&img, region, &blue, &expected, &stats);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_FAIL, rc);
	TEST_ASSERT_EQUAL_INT(1, stats.size_mismatch);
	TEST_ASSERT_EQUAL_UINT(1u, stats.delta_w);
	TEST_ASSERT_EQUAL_UINT(0u, stats.delta_h);

	/* Below min_pixels. */
	memset(&expected, 0, sizeof(expected));
	expected.min_x = 4u;
	expected.min_y = 3u;
	expected.max_x = 11u;
	expected.max_y = 9u;
	expected.min_pixels = 100u;
	rc = ui->cpu_image_assert_bbox(&img, region, &blue, &expected, &stats);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_FAIL, rc);
	TEST_ASSERT_EQUAL_INT(1, stats.min_pixels_fail);

	/* expect_empty: passes on the empty image, fails on the populated one. */
	memset(&expected, 0, sizeof(expected));
	expected.expect_empty = 1;
	rc = ui->cpu_image_assert_bbox(&empty_img, region, &blue, &expected, &stats);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, rc);
	rc = ui->cpu_image_assert_bbox(&img, region, &blue, &expected, &stats);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_FAIL, rc);
	TEST_ASSERT_EQUAL_INT(1, stats.empty_mismatch);
}

SK_TEST(ui_image_structure_histogram_pass_and_near_miss) {
	const sk_ui_api_t* ui = ui_si_test_api();
	u8 px[8u * 8u * 4u];
	sk_ui_cpu_image_t img;
	sk_ui_region_t region;
	sk_ui_color_histogram_t hist;
	sk_ui_hist_expectation_t expect[SK_UI_COLOR_HIST_MAX_ENTRIES];
	sk_ui_hist_assert_params_t params;
	sk_ui_hist_assert_stats_t stats;
	sk_ui_color_match_t red;
	sk_ui_color_match_t green;
	sk_ui_color_match_t blue;
	sk_ui_color_match_t yellow;
	i32 rc;

	/* 8x8: top 4 rows red (32 px = 50%), bottom-left green (16 = 25%), bottom-right blue (16 = 25%). */
	ui_si_fill_solid(px, 8u, 8u, 4u, 0u, 0u, 0u, 255u);
	ui_si_fill_rect(px, 8u, 8u, 4u, 0u, 0u, 8u, 4u, 255u, 0u, 0u, 255u);
	ui_si_fill_rect(px, 8u, 8u, 4u, 0u, 4u, 4u, 8u, 0u, 255u, 0u, 255u);
	ui_si_fill_rect(px, 8u, 8u, 4u, 4u, 4u, 8u, 8u, 0u, 0u, 255u, 255u);
	img = ui_si_make_image(px, 8u, 8u, 4u);
	region.x0 = 0u;
	region.y0 = 0u;
	region.x1 = 8u;
	region.y1 = 8u;

	/* Extraction: three dominants, sorted by count desc. */
	rc = ui->cpu_image_histogram(&img, region, 0u, 16u, &hist);
	TEST_ASSERT_EQUAL_INT(0, rc);
	TEST_ASSERT_EQUAL_UINT(3u, hist.entry_count);
	TEST_ASSERT_EQUAL_UINT(64u, hist.total_pixels);
	TEST_ASSERT_EQUAL_UINT(255u, (u32)hist.entries[0].r);
	TEST_ASSERT_EQUAL_UINT(0u, (u32)hist.entries[0].g);
	TEST_ASSERT_EQUAL_UINT(0u, (u32)hist.entries[0].b);
	TEST_ASSERT_EQUAL_UINT(32u, hist.entries[0].count);
	TEST_ASSERT_TRUE(hist.entries[0].fraction > 0.49f && hist.entries[0].fraction < 0.51f);

	red.r = 255u;
	red.g = 0u;
	red.b = 0u;
	red.a = 255u;
	red.tolerance = 0u;
	red.include_alpha = 0;
	green = red;
	green.g = 255u;
	green.r = 0u;
	blue = red;
	blue.b = 255u;
	blue.r = 0u;
	yellow = red;
	yellow.r = 255u;
	yellow.g = 255u;

	memset(&params, 0, sizeof(params));
	params.max_entries = 16u;
	params.merge_tolerance = 0u;
	params.max_unexpected_fraction = 0.05f;

	/* Passing case: red ~50%, green/blue ~25% each. */
	memset(expect, 0, sizeof(expect));
	expect[0].color = red;
	expect[0].min_fraction = 0.45f;
	expect[0].max_fraction = 0.55f;
	expect[1].color = green;
	expect[1].min_fraction = 0.20f;
	expect[1].max_fraction = 0.30f;
	expect[2].color = blue;
	expect[2].min_fraction = 0.20f;
	expect[2].max_fraction = 0.30f;
	rc = ui->cpu_image_assert_histogram(&img, region, &params, expect, 3u, &stats);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, rc);
	TEST_ASSERT_EQUAL_UINT(0u, stats.missing_count);
	TEST_ASSERT_EQUAL_UINT(0u, stats.out_of_range_count);
	TEST_ASSERT_EQUAL_UINT(0u, stats.unexpected_count);
	TEST_ASSERT_TRUE(stats.measured_fraction[0] > 0.49f && stats.measured_fraction[0] < 0.51f);
	TEST_ASSERT_EQUAL_UINT(32u, stats.measured_count[0]);

	/* Near-miss: green's proportion shrank out of the expected range. */
	expect[1].max_fraction = 0.24f;
	rc = ui->cpu_image_assert_histogram(&img, region, &params, expect, 3u, &stats);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_FAIL, rc);
	TEST_ASSERT_EQUAL_UINT(1u, stats.out_of_range_count);
	TEST_ASSERT_TRUE(stats.measured_fraction[1] > 0.24f && stats.measured_fraction[1] < 0.26f);
	TEST_ASSERT_EQUAL_UINT(16u, stats.measured_count[1]);

	/* Missing widget: an expected color is absent. */
	memset(expect, 0, sizeof(expect));
	expect[0].color = red;
	expect[0].min_fraction = 0.45f;
	expect[0].max_fraction = 0.55f;
	expect[1].color = yellow;
	expect[1].min_fraction = 0.10f;
	expect[1].max_fraction = 0.40f;
	rc = ui->cpu_image_assert_histogram(&img, region, &params, expect, 2u, &stats);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_FAIL, rc);
	TEST_ASSERT_EQUAL_UINT(1u, stats.missing_count);
	TEST_ASSERT_EQUAL_UINT(0u, stats.measured_count[1]);

	/* Wrong theming: unlisted colors dominate; zero unexpected allowance. */
	memset(expect, 0, sizeof(expect));
	expect[0].color = red;
	expect[0].min_fraction = 0.45f;
	expect[0].max_fraction = 0.55f;
	params.max_unexpected_fraction = 0.0f;
	rc = ui->cpu_image_assert_histogram(&img, region, &params, expect, 1u, &stats);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_FAIL, rc);
	TEST_ASSERT_EQUAL_UINT(2u, stats.unexpected_count);
	TEST_ASSERT_TRUE(stats.unexpected_fraction > 0.49f && stats.unexpected_fraction < 0.51f);

	/* The same check passes once unexpected colors are allowed for. */
	params.max_unexpected_fraction = 0.6f;
	rc = ui->cpu_image_assert_histogram(&img, region, &params, expect, 1u, &stats);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, rc);
}

SK_TEST(ui_image_structure_region_hash_pass_and_near_miss) {
	const sk_ui_api_t* ui = ui_si_test_api();
	u8 px[8u * 8u * 4u];
	u8 other[8u * 8u * 4u];
	sk_ui_cpu_image_t img;
	sk_ui_region_t region;
	u64 h1;
	u64 h2;
	u64 h3;
	u64 h4;
	u64 actual;
	i32 rc;

	ui_si_fill_solid(px, 8u, 8u, 4u, 10u, 20u, 30u, 255u);
	img = ui_si_make_image(px, 8u, 8u, 4u);
	region.x0 = 0u;
	region.y0 = 0u;
	region.x1 = 8u;
	region.y1 = 8u;

	/* Stable: same input, same hash, any number of runs. */
	TEST_ASSERT_EQUAL_INT(0, ui->cpu_image_region_hash(&img, region, 0u, &h1));
	TEST_ASSERT_EQUAL_INT(0, ui->cpu_image_region_hash(&img, region, 0u, &h2));
	TEST_ASSERT_EQUAL_UINT64(h1, h2);
	TEST_ASSERT_TRUE(h1 != 0u);

	/* Different seed / region / content all change the hash. */
	TEST_ASSERT_EQUAL_INT(0, ui->cpu_image_region_hash(&img, region, 123u, &h3));
	TEST_ASSERT_TRUE(h3 != h1);
	{
		sk_ui_region_t sub;
		sub.x0 = 0u;
		sub.y0 = 0u;
		sub.x1 = 4u;
		sub.y1 = 4u;
		TEST_ASSERT_EQUAL_INT(0, ui->cpu_image_region_hash(&img, sub, 0u, &h4));
		TEST_ASSERT_TRUE(h4 != h1);
	}
	memcpy(other, px, sizeof(px));
	other[(3u * 8u + 3u) * 4u + 0u] = 11u;
	{
		sk_ui_cpu_image_t changed = ui_si_make_image(other, 8u, 8u, 4u);
		TEST_ASSERT_EQUAL_INT(0, ui->cpu_image_region_hash(&changed, region, 0u, &h4));
		TEST_ASSERT_TRUE(h4 != h1);
	}

	/* Assertion: matching hash passes and reports the value. */
	rc = ui->cpu_image_assert_region_hash(&img, region, h1, &actual);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, rc);
	TEST_ASSERT_EQUAL_UINT64(h1, actual);

	/* Near-miss: single-bit drift fails with the actual hash reported. */
	rc = ui->cpu_image_assert_region_hash(&img, region, h1 ^ 1u, &actual);
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_FAIL, rc);
	TEST_ASSERT_EQUAL_UINT64(h1, actual);
}

SK_TEST(ui_image_structure_error_paths) {
	const sk_ui_api_t* ui = ui_si_test_api();
	u8 px[4u * 4u * 4u];
	u8 gray[4u * 4u * 2u];
	sk_ui_cpu_image_t img;
	sk_ui_cpu_image_t bad_channels;
	sk_ui_color_match_t color;
	sk_ui_region_t region;
	sk_ui_region_t degenerate;
	sk_ui_solid_stats_t solid_stats;
	sk_ui_coverage_stats_t cov_stats;
	sk_ui_bbox_t bbox;
	sk_ui_bbox_expected_t expected;
	sk_ui_color_histogram_t hist;
	u64 hash;

	ui_si_fill_solid(px, 4u, 4u, 4u, 1u, 2u, 3u, 255u);
	img = ui_si_make_image(px, 4u, 4u, 4u);
	ui_si_fill_solid(gray, 4u, 4u, 2u, 1u, 2u, 3u, 255u);
	bad_channels = ui_si_make_image(gray, 4u, 4u, 2u);
	region.x0 = 0u;
	region.y0 = 0u;
	region.x1 = 4u;
	region.y1 = 4u;
	degenerate = region;
	degenerate.x1 = 0u; /* x1 <= x0: degenerate (empty) region */
	degenerate.y1 = 0u;
	color.r = 1u;
	color.g = 2u;
	color.b = 3u;
	color.a = 255u;
	color.tolerance = 0u;
	color.include_alpha = 0;
	memset(&expected, 0, sizeof(expected));
	expected.min_x = 0u;
	expected.min_y = 0u;
	expected.max_x = 3u;
	expected.max_y = 3u;

	/* Null image. */
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_ERROR, ui->cpu_image_assert_solid(NULL, region, &color, NULL));
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_ERROR, ui->cpu_image_assert_coverage(NULL, region, &color, 0.0f, 1.0f, NULL));
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_ERROR, ui->cpu_image_find_bbox(NULL, region, &color, NULL));
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_ERROR, ui->cpu_image_assert_bbox(NULL, region, &color, &expected, NULL));
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_ERROR, ui->cpu_image_histogram(NULL, region, 0u, 16u, NULL));
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_ERROR, ui->cpu_image_assert_histogram(NULL, region, NULL, NULL, 0u, NULL));
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_ERROR, ui->cpu_image_region_hash(NULL, region, 0u, NULL));

	/* Degenerate region. */
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_ERROR, ui->cpu_image_assert_solid(&img, degenerate, &color, &solid_stats));
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_ERROR, ui->cpu_image_assert_coverage(&img, degenerate, &color, 0.0f, 1.0f, &cov_stats));
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_ERROR, ui->cpu_image_find_bbox(&img, degenerate, &color, &bbox));
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_ERROR, ui->cpu_image_assert_bbox(&img, degenerate, &color, &expected, NULL));
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_ERROR, ui->cpu_image_histogram(&img, degenerate, 0u, 16u, &hist));
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_ERROR, ui->cpu_image_region_hash(&img, degenerate, 0u, &hash));

	/* <3 channels for color assertions; hash still accepts 2 channels. */
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_ERROR, ui->cpu_image_assert_solid(&bad_channels, region, &color, NULL));
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_ERROR, ui->cpu_image_assert_coverage(&bad_channels, region, &color, 0.0f, 1.0f, NULL));
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_ERROR, ui->cpu_image_find_bbox(&bad_channels, region, &color, &bbox));
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_ERROR, ui->cpu_image_histogram(&bad_channels, region, 0u, 16u, &hist));
	TEST_ASSERT_EQUAL_INT(0, ui->cpu_image_region_hash(&bad_channels, region, 0u, &hash));

	/* Out-of-range coverage bounds are rejected. */
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_ERROR, ui->cpu_image_assert_coverage(&img, region, &color, 0.8f, 0.2f, &cov_stats));
}

#endif /* SK_TESTS */
